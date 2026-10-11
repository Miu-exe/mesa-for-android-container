/* In-process amdgpu IB capture via seccomp SIGSYS trapping (aarch64 Android/Linux).
 *
 * Every ioctl traps into a SIGSYS handler, which records GEM_VA/GEM_MMAP state,
 * dumps IBs of DRM_AMDGPU_CS submissions to ib_NNNNNN.bin (read directly through
 * this process's CPU mappings, found via /proc/self/maps), then re-issues the
 * ioctl with a magic value in x5 that the filter lets through.
 *
 * Linked into a program, call ibdump_install(log). Built with -DIBDUMP_PRELOAD as a shared
 * library it installs itself from a constructor, so any Vulkan program can be captured:
 *
 *   LD_PRELOAD=libibdump.so IBDUMP=cs.log <program>
 *
 * Environment:
 *   IBDUMP=<log>      enable, log file name (relative to IBDUMP_DIR)
 *   IBDUMP_DIR=<dir>  output directory (default: current directory)
 *   IBDUMP_SYNC=1     flight recorder: fsync the IBs and log *before* the submission reaches the
 *                     GPU, so the last stream survives a GPU hang that reboots the device
 *   IBDUMP_KEEP=<n>   keep only the last n submissions (ring of ib_NNNNNN.bin files)
 *   IBDUMP_BOS=1      also snapshot every BO (<= 4 MiB) on each submission (bo_<va>.bin)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "drm.h"
#include "amdgpu_drm.h"

#define IBDUMP_MAGIC 0x1bd0a9ull

#define MAX_BOS 4096
static struct { uint32_t handle; uint64_t mmap_off, va, size; } bos[MAX_BOS];
static int nbos, ib_count, cs_seq;
static FILE *log_f;
static const char *out_dir = ".";
static int sync_mode, keep;
static int lock_word;

static void lock(void) { while (__atomic_exchange_n(&lock_word, 1, __ATOMIC_ACQUIRE)) ; }
static void unlock(void) { __atomic_store_n(&lock_word, 0, __ATOMIC_RELEASE); }

/* Write a whole file into out_dir; with sync_mode it is on disk when this returns. */
static void write_file(const char *name, const void *data, size_t len, int only_if_missing)
{
   char path[512];
   snprintf(path, sizeof(path), "%s/%s", out_dir, name);
   int fd = open(path, O_WRONLY | O_CREAT | (only_if_missing ? O_EXCL : O_TRUNC) | O_CLOEXEC, 0644);
   if (fd < 0) return;
   for (size_t done = 0; done < len;) {
      ssize_t n = write(fd, (const char *)data + done, len - done);
      if (n <= 0) break;
      done += n;
   }
   if (sync_mode) fsync(fd);
   close(fd);
}
static long drm_fd = -1;
static long real_ioctl(long fd, long cmd, long arg);

static int bo_idx(uint32_t handle)
{
   for (int i = 0; i < nbos; i++)
      if (bos[i].handle == handle) return i;
   if (nbos == MAX_BOS) return -1;
   bos[nbos].handle = handle;
   return nbos++;
}

/* Find this process's CPU address of the renderD mapping at file offset off. */
static uint64_t cpu_for_mmap_off(uint64_t off)
{
   FILE *f = fopen("/proc/self/maps", "r");
   char line[512];
   uint64_t res = 0;
   while (f && fgets(line, sizeof(line), f)) {
      unsigned long long lo, hi, o;
      char perms[8];
      if (!strstr(line, "/dev/dri/")) continue;
      if (sscanf(line, "%llx-%llx %7s %llx", &lo, &hi, perms, &o) == 4 && o == off) { res = lo; break; }
   }
   if (f) fclose(f);
   return res;
}

static void *cpu_for_va(uint64_t va, uint64_t *avail)
{
   for (int i = nbos - 1; i >= 0; i--) {
      if (!bos[i].size || va < bos[i].va || va >= bos[i].va + bos[i].size) continue;
      uint64_t cpu = bos[i].mmap_off ? cpu_for_mmap_off(bos[i].mmap_off) : 0;
      if (!cpu && drm_fd >= 0) {
         /* Not mapped by the driver: map it ourselves. */
         union drm_amdgpu_gem_mmap m = {.in.handle = bos[i].handle};
         if (!real_ioctl(drm_fd, DRM_IOCTL_AMDGPU_GEM_MMAP, (long)&m)) {
            void *p = mmap(NULL, bos[i].size, PROT_READ, MAP_SHARED, drm_fd, m.out.addr_ptr);
            if (p != MAP_FAILED) cpu = (uint64_t)(uintptr_t)p;
         }
      }
      if (!cpu) return NULL;
      *avail = bos[i].va + bos[i].size - va;
      return (void *)(uintptr_t)(cpu + (va - bos[i].va));
   }
   return NULL;
}

/* Save shader code referenced by SET_SH_REG PGM_LO registers in this IB. */
static void dump_shaders(const uint32_t *ib, uint32_t ndw)
{
   /* SH register offsets (dwords from 0xb000) of *_PGM_LO: PS, VS, GS, ES, HS, LS, CS */
   static const struct { uint32_t reg; const char *name; } pgm[] = {
      {0x008, "ps"}, {0x048, "vs"}, {0x088, "gs"}, {0x0c8, "es"}, {0x108, "hs"}, {0x148, "ls"}, {0x20c, "cs"}};
   for (uint32_t i = 0; i + 2 < ndw; i++) {
      if ((ib[i] & 0xc000ff00) != 0xc0007600) continue;
      uint32_t cnt = (ib[i] >> 16) & 0x3fff, start = ib[i + 1] & 0xffff;
      for (uint32_t k = 0; k < cnt && i + 2 + k < ndw; k++) {
         for (unsigned p = 0; p < sizeof(pgm) / sizeof(pgm[0]); p++) {
            if (start + k != pgm[p].reg) continue;
            uint64_t va = (uint64_t)ib[i + 2 + k] << 8, avail = 0;
            const void *code = cpu_for_va(va, &avail);
            if (!code) { fprintf(log_f, "       (%s shader va 0x%llx not mapped)\n", pgm[p].name, (unsigned long long)va); continue; }
            char name[64];
            snprintf(name, sizeof(name), "shader_%s_%llx.bin", pgm[p].name, (unsigned long long)va);
            write_file(name, code, avail < 4096 ? avail : 4096, 1);
            fprintf(log_f, "       %s shader va=0x%llx -> %s\n", pgm[p].name, (unsigned long long)va, name);
         }
      }
   }
}

static void dump_ib(uint64_t va, uint32_t bytes)
{
   for (int i = nbos - 1; i >= 0; i--) {
      if (!bos[i].size || va < bos[i].va || va >= bos[i].va + bos[i].size) continue;
      uint64_t cpu = bos[i].mmap_off ? cpu_for_mmap_off(bos[i].mmap_off) : 0;
      if (!cpu) { fprintf(log_f, "       (IB bo %u not CPU-mapped)\n", bos[i].handle); return; }
      char name[64];
      snprintf(name, sizeof(name), "ib_%06d.bin", keep ? ib_count % keep : ib_count);
      ib_count++;
      write_file(name, (void *)(uintptr_t)(cpu + (va - bos[i].va)), bytes, 0);
      fprintf(log_f, "       -> %s\n", name);
      dump_shaders((const uint32_t *)(uintptr_t)(cpu + (va - bos[i].va)), bytes / 4);
      return;
   }
   fprintf(log_f, "       (IB va 0x%llx not in any known BO)\n", (unsigned long long)va);
}

static long real_ioctl(long fd, long cmd, long arg)
{
   register long x0 __asm__("x0") = fd;
   register long x1 __asm__("x1") = cmd;
   register long x2 __asm__("x2") = arg;
   register long x3 __asm__("x3") = 0;
   register long x4 __asm__("x4") = 0;
   register long x5 __asm__("x5") = IBDUMP_MAGIC;
   register long x8 __asm__("x8") = __NR_ioctl;
   __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
   return x0;
}

static void on_sigsys(int sig, siginfo_t *si, void *uc_)
{
   ucontext_t *uc = uc_;
   uint64_t *r = (uint64_t *)uc->uc_mcontext.regs;
   long fd = r[0], cmd = r[1], arg = r[2];
   unsigned type = (cmd >> 8) & 0xff, nr = cmd & 0xff;
   int saved_errno = errno;

   if (type == DRM_IOCTL_BASE && nr >= DRM_COMMAND_BASE) {
      unsigned anr = nr - DRM_COMMAND_BASE;
      if (anr == DRM_AMDGPU_CS) {
         lock();
         drm_fd = fd;
         union drm_amdgpu_cs *a = (void *)arg;
         uint64_t *ptrs = (uint64_t *)(uintptr_t)a->in.chunks;
         struct timespec ts;
         clock_gettime(CLOCK_MONOTONIC, &ts);
         fprintf(log_f, "CS #%d t=%lld.%06ld tid=%d ctx=%u nchunks=%u\n", cs_seq++, (long long)ts.tv_sec,
                 ts.tv_nsec / 1000, (int)syscall(__NR_gettid), a->in.ctx_id, a->in.num_chunks);
         for (unsigned i = 0; i < a->in.num_chunks; i++) {
            struct drm_amdgpu_cs_chunk *c = (void *)(uintptr_t)ptrs[i];
            if (c->chunk_id != AMDGPU_CHUNK_ID_IB) continue;
            struct drm_amdgpu_cs_chunk_ib *ib = (void *)(uintptr_t)c->chunk_data;
            fprintf(log_f, "   IB ip=%u flags=0x%x va=0x%llx bytes=%u\n", ib->ip_type, ib->flags,
                    (unsigned long long)ib->va_start, ib->ib_bytes);
            dump_ib(ib->va_start, ib->ib_bytes);
         }
         if (getenv("IBDUMP_BOS")) {
            /* Snapshot every (small) BO so descriptors etc. can be inspected offline. */
            for (int b = 0; b < nbos; b++) {
               uint64_t avail = 0;
               if (!bos[b].size || bos[b].size > (4 << 20)) continue;
               const void *p = cpu_for_va(bos[b].va, &avail);
               if (!p) continue;
               char name[64];
               snprintf(name, sizeof(name), "bo_%llx.bin", (unsigned long long)bos[b].va);
               write_file(name, p, avail, 0);
            }
         }
         fflush(log_f);
         if (sync_mode) fsync(fileno(log_f));
         unlock();
      } else if (anr == DRM_AMDGPU_GEM_VA) {
         struct drm_amdgpu_gem_va *a = (void *)arg;
         if (a->operation == AMDGPU_VA_OP_MAP) {
            lock();
            int i = bo_idx(a->handle);
            if (i >= 0) { bos[i].va = a->va_address; bos[i].size = a->map_size; }
            unlock();
         }
      } else if (anr == DRM_AMDGPU_GEM_MMAP) {
         union drm_amdgpu_gem_mmap *a = (void *)arg;
         uint32_t handle = a->in.handle;
         long ret = real_ioctl(fd, cmd, arg);
         if (!ret) {
            lock();
            int i = bo_idx(handle);
            if (i >= 0) bos[i].mmap_off = a->out.addr_ptr;
            unlock();
         }
         r[0] = ret;
         errno = saved_errno;
         return;
      }
   }
   r[0] = real_ioctl(fd, cmd, arg);
   errno = saved_errno;
}

void ibdump_install(const char *log_path)
{
   if (getenv("IBDUMP_DIR")) out_dir = getenv("IBDUMP_DIR");
   sync_mode = getenv("IBDUMP_SYNC") && atoi(getenv("IBDUMP_SYNC"));
   keep = getenv("IBDUMP_KEEP") ? atoi(getenv("IBDUMP_KEEP")) : 0;
   char path[512];
   snprintf(path, sizeof(path), "%s/%s", out_dir, log_path);
   log_f = fopen(path, "w");
   if (!log_f) log_f = stderr;

   struct sigaction sa = {0};
   sa.sa_sigaction = on_sigsys;
   sa.sa_flags = SA_SIGINFO;
   sigaction(SIGSYS, &sa, NULL);

   struct sock_filter prog_insns[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ioctl, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[5])),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)IBDUMP_MAGIC, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
   };
   struct sock_fprog prog = {sizeof(prog_insns) / sizeof(prog_insns[0]), prog_insns};
   prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
   if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog))
      fprintf(stderr, "ibdump: seccomp failed: %s\n", strerror(errno));
}

#ifdef IBDUMP_PRELOAD
__attribute__((constructor)) static void ibdump_preload_init(void)
{
   if (getenv("IBDUMP"))
      ibdump_install(getenv("IBDUMP"));
}
#endif
