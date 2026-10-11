/* drmtrace: ptrace a program and decode its amdgpu DRM ioctls (no root needed, traces own child).
 * Usage: drmtrace [-x] <program> [args...]    (-x: hex-dump every ioctl payload)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <elf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/ptrace.h>

#include "drm.h"
#include "amdgpu_drm.h"

#define MAX_TIDS 4096
static struct { pid_t tid; int in_sc; uint64_t cmd, arg; } tids[MAX_TIDS];
static int dump_all;
static FILE *out;
static int peek(pid_t pid, uint64_t addr, void *buf, size_t len);
static int ib_count;
static uint32_t last_mmap_handle;

#define MAX_BOS 4096
static struct bo { uint32_t handle; uint64_t mmap_off, cpu, va, size; } bos[MAX_BOS];
static int nbos;

static struct bo *bo_get(uint32_t handle)
{
   for (int i = 0; i < nbos; i++)
      if (bos[i].handle == handle) return &bos[i];
   if (nbos == MAX_BOS) return NULL;
   bos[nbos].handle = handle;
   return &bos[nbos++];
}

static struct bo *bo_by_va(uint64_t va)
{
   for (int i = nbos - 1; i >= 0; i--)
      if (bos[i].size && va >= bos[i].va && va < bos[i].va + bos[i].size) return &bos[i];
   return NULL;
}

/* Save an IB (read through the traced process's CPU mapping) to ib_<n>.bin. */
static void dump_ib(pid_t pid, uint64_t va, uint32_t bytes)
{
   struct bo *b = bo_by_va(va);
   if (!b || !b->cpu) { fprintf(out, "       (IB not CPU-mapped, can't dump)\n"); return; }
   void *buf = malloc(bytes);
   if (!peek(pid, b->cpu + (va - b->va), buf, bytes)) {
      char name[64];
      snprintf(name, sizeof(name), "ib_%03d.bin", ib_count++);
      FILE *f = fopen(name, "wb");
      if (f) { fwrite(buf, 1, bytes, f); fclose(f); }
      fprintf(out, "       -> saved %s\n", name);
   } else {
      fprintf(out, "       (read failed: bo=%u va=0x%llx cpu=0x%llx off=0x%llx: %s)\n", b->handle,
              (unsigned long long)b->va, (unsigned long long)b->cpu, (unsigned long long)b->mmap_off, strerror(errno));
   }
   free(buf);
}

static int *tid_slot_in(pid_t tid, uint64_t **cmd, uint64_t **arg)
{
   for (int i = 0; i < MAX_TIDS; i++)
      if (tids[i].tid == tid) { *cmd = &tids[i].cmd; *arg = &tids[i].arg; return &tids[i].in_sc; }
   for (int i = 0; i < MAX_TIDS; i++)
      if (!tids[i].tid) { tids[i].tid = tid; *cmd = &tids[i].cmd; *arg = &tids[i].arg; return &tids[i].in_sc; }
   return NULL;
}

static int peek(pid_t pid, uint64_t addr, void *buf, size_t len)
{
   struct iovec l = {buf, len}, r = {(void *)(uintptr_t)addr, len};
   if (process_vm_readv(pid, &l, 1, &r, 1, 0) == (ssize_t)len)
      return 0;
   /* Device (VM_IO/PFNMAP) mappings such as GPU BOs: go through /proc/pid/mem,
    * which uses the driver's vm_ops->access. */
   char path[64];
   snprintf(path, sizeof(path), "/proc/%d/mem", pid);
   int fd = open(path, O_RDONLY);
   if (fd < 0) return -1;
   ssize_t n = pread(fd, buf, len, (off_t)addr);
   close(fd);
   return n == (ssize_t)len ? 0 : -1;
}

static void hexdump(pid_t pid, uint64_t addr, size_t len)
{
   uint32_t buf[64];
   if (len > sizeof(buf)) len = sizeof(buf);
   if (peek(pid, addr, buf, len)) { fprintf(out, "   <unreadable>\n"); return; }
   fprintf(out, "   ");
   for (size_t i = 0; i < len / 4; i++) fprintf(out, "%08x%s", buf[i], (i % 8 == 7) ? "\n   " : " ");
   fprintf(out, "\n");
}

static const char *domains(uint64_t d)
{
   static char s[64];
   s[0] = 0;
   if (d & AMDGPU_GEM_DOMAIN_CPU) strcat(s, "CPU|");
   if (d & AMDGPU_GEM_DOMAIN_GTT) strcat(s, "GTT|");
   if (d & AMDGPU_GEM_DOMAIN_VRAM) strcat(s, "VRAM|");
   if (d & AMDGPU_GEM_DOMAIN_GDS) strcat(s, "GDS|");
   if (d & AMDGPU_GEM_DOMAIN_GWS) strcat(s, "GWS|");
   if (d & AMDGPU_GEM_DOMAIN_OA) strcat(s, "OA|");
   if (d & ~0x3full) sprintf(s + strlen(s), "0x%llx|", (unsigned long long)(d & ~0x3full));
   return s;
}

static void on_entry(pid_t pid, uint64_t cmd, uint64_t arg)
{
   unsigned type = (cmd >> 8) & 0xff, nr = cmd & 0xff, size = (cmd >> 16) & 0x3fff;
   if (type != DRM_IOCTL_BASE) return;
   if (nr < DRM_COMMAND_BASE) {
      fprintf(out, "[%d] DRM core ioctl nr=0x%02x size=%u\n", pid, nr, size);
      if (dump_all) hexdump(pid, arg, size);
      return;
   }
   unsigned anr = nr - DRM_COMMAND_BASE;
   switch (anr) {
   case DRM_AMDGPU_GEM_CREATE: {
      union drm_amdgpu_gem_create a;
      if (peek(pid, arg, &a, sizeof(a))) break;
      fprintf(out, "[%d] GEM_CREATE size=0x%llx align=0x%llx domains=%s flags=0x%llx\n", pid,
              (unsigned long long)a.in.bo_size, (unsigned long long)a.in.alignment, domains(a.in.domains),
              (unsigned long long)a.in.domain_flags);
      return;
   }
   case DRM_AMDGPU_GEM_VA: {
      struct drm_amdgpu_gem_va a;
      if (peek(pid, arg, &a, sizeof(a))) break;
      if (a.operation == AMDGPU_VA_OP_MAP) {
         struct bo *b = bo_get(a.handle);
         if (b) { b->va = a.va_address; b->size = a.map_size; }
      }
      fprintf(out, "[%d] GEM_VA op=%u handle=%u flags=0x%x (mtype=%u) va=0x%llx off=0x%llx size=0x%llx\n", pid,
              a.operation, a.handle, a.flags, (a.flags >> 5) & 0xf, (unsigned long long)a.va_address,
              (unsigned long long)a.offset_in_bo, (unsigned long long)a.map_size);
      return;
   }
   case DRM_AMDGPU_CS: {
      union drm_amdgpu_cs a;
      if (peek(pid, arg, &a, sizeof(a))) break;
      fprintf(out, "[%d] CS ctx=%u bo_list=%u nchunks=%u flags=0x%x\n", pid, a.in.ctx_id, a.in.bo_list_handle,
              a.in.num_chunks, a.in.flags);
      uint64_t ptrs[32];
      unsigned n = a.in.num_chunks > 32 ? 32 : a.in.num_chunks;
      if (peek(pid, a.in.chunks, ptrs, n * 8)) return;
      for (unsigned i = 0; i < n; i++) {
         struct drm_amdgpu_cs_chunk c;
         if (peek(pid, ptrs[i], &c, sizeof(c))) continue;
         fprintf(out, "     chunk id=%u len_dw=%u\n", c.chunk_id, c.length_dw);
         if (c.chunk_id == AMDGPU_CHUNK_ID_IB) {
            struct drm_amdgpu_cs_chunk_ib ib;
            if (!peek(pid, c.chunk_data, &ib, sizeof(ib)))
               fprintf(out, "       IB ip=%u inst=%u ring=%u flags=0x%x va=0x%llx bytes=%u\n", ib.ip_type,
                       ib.ip_instance, ib.ring, ib.flags, (unsigned long long)ib.va_start, ib.ib_bytes);
            dump_ib(pid, ib.va_start, ib.ib_bytes);
         } else {
            hexdump(pid, c.chunk_data, c.length_dw * 4 > 64 ? 64 : c.length_dw * 4);
         }
      }
      return;
   }
   case DRM_AMDGPU_CTX: {
      union drm_amdgpu_ctx a;
      if (peek(pid, arg, &a, sizeof(a))) break;
      fprintf(out, "[%d] CTX op=%u flags=0x%x ctx=%u prio=%d\n", pid, a.in.op, a.in.flags, a.in.ctx_id,
              a.in.priority);
      return;
   }
   case DRM_AMDGPU_BO_LIST: {
      union drm_amdgpu_bo_list a;
      if (peek(pid, arg, &a, sizeof(a))) break;
      fprintf(out, "[%d] BO_LIST op=%u list=%u n=%u\n", pid, a.in.operation, a.in.list_handle, a.in.bo_number);
      return;
   }
   case DRM_AMDGPU_INFO: {
      struct drm_amdgpu_info a;
      if (peek(pid, arg, &a, sizeof(a))) break;
      fprintf(out, "[%d] INFO query=0x%x size=%u\n", pid, a.query, a.return_size);
      return;
   }
   case DRM_AMDGPU_GEM_MMAP: {
      union drm_amdgpu_gem_mmap a;
      if (!peek(pid, arg, &a, sizeof(a))) last_mmap_handle = a.in.handle;
      return;
   }
   case DRM_AMDGPU_WAIT_CS:
   case DRM_AMDGPU_GEM_WAIT_IDLE:
      fprintf(out, "[%d] amdgpu ioctl 0x%02x\n", pid, anr);
      return;
   default:
      break;
   }
   /* Unknown/vendor or undecoded amdgpu ioctl: always dump payload. */
   fprintf(out, "[%d] amdgpu/vendor ioctl nr=0x%02x (base+0x%02x) dir=%llu size=%u\n", pid, nr, anr,
           (unsigned long long)(cmd >> 30), size);
   hexdump(pid, arg, size);
}

static void on_exit_sc(pid_t pid, uint64_t cmd, uint64_t arg, int64_t ret)
{
   unsigned type = (cmd >> 8) & 0xff, nr = cmd & 0xff;
   if (type != DRM_IOCTL_BASE || nr < DRM_COMMAND_BASE) return;
   unsigned anr = nr - DRM_COMMAND_BASE;
   if (ret) fprintf(out, "[%d]   -> ret=%lld\n", pid, (long long)ret);
   if (anr == DRM_AMDGPU_GEM_CREATE && !ret) {
      union drm_amdgpu_gem_create a;
      if (!peek(pid, arg, &a, sizeof(a))) fprintf(out, "[%d]   -> handle=%u\n", pid, a.out.handle);
   } else if (anr == DRM_AMDGPU_GEM_MMAP && !ret) {
      union drm_amdgpu_gem_mmap a;
      if (!peek(pid, arg, &a, sizeof(a))) {
         struct bo *b = bo_get(last_mmap_handle);
         if (b) b->mmap_off = a.out.addr_ptr;
      }
   } else if (anr == DRM_AMDGPU_CTX && !ret) {
      union drm_amdgpu_ctx a;
      if (!peek(pid, arg, &a, sizeof(a))) fprintf(out, "[%d]   -> ctx=%u\n", pid, a.out.alloc.ctx_id);
   }
}

int main(int argc, char **argv)
{
   int ai = 1;
   out = stderr;
   if (argc > 1 && !strcmp(argv[1], "-x")) { dump_all = 1; ai++; }
   if (ai >= argc) { fprintf(stderr, "usage: %s [-x] prog args...\n", argv[0]); return 1; }

   pid_t child = fork();
   if (!child) {
      ptrace(PTRACE_TRACEME, 0, 0, 0);
      raise(SIGSTOP);
      execvp(argv[ai], argv + ai);
      _exit(127);
   }
   int st;
   waitpid(child, &st, 0);
   ptrace(PTRACE_SETOPTIONS, child, 0,
          PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL);
   ptrace(PTRACE_SYSCALL, child, 0, 0);

   int exit_code = 0;
   for (;;) {
      pid_t pid = waitpid(-1, &st, __WALL);
      if (pid < 0) break;
      if (WIFEXITED(st) || WIFSIGNALED(st)) {
         if (pid == child) exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
         for (int i = 0; i < MAX_TIDS; i++) if (tids[i].tid == pid) tids[i].tid = 0;
         continue;
      }
      int sig = 0;
      if (WIFSTOPPED(st)) {
         int s = WSTOPSIG(st);
         if (s == (SIGTRAP | 0x80)) {
            struct user_pt_regs regs;
            struct iovec iov = {&regs, sizeof(regs)};
            uint64_t *cmd, *arg;
            int *in = tid_slot_in(pid, &cmd, &arg);
            if (in && !ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov)) {
               if (!*in) {
                  *in = 1;
                  *cmd = regs.regs[1];
                  *arg = regs.regs[2];
                  if (regs.regs[8] == 29 /* ioctl */) on_entry(pid, *cmd, *arg);
                  else if (regs.regs[8] == 222 /* mmap */) { *cmd = 1ull << 63; *arg = regs.regs[5]; }
                  else *cmd = 0;
               } else {
                  *in = 0;
                  if (*cmd == 1ull << 63) {
                     for (int i = 0; i < nbos; i++)
                        if (bos[i].mmap_off && bos[i].mmap_off == *arg) bos[i].cpu = regs.regs[0];
                  } else if (*cmd) on_exit_sc(pid, *cmd, *arg, (int64_t)regs.regs[0]);
               }
            }
         } else if (s == SIGTRAP && (st >> 16)) {
            /* clone/fork event: new thread starts traced */
         } else if (s != SIGSTOP) {
            sig = s;
         }
      }
      ptrace(PTRACE_SYSCALL, pid, 0, sig);
   }
   return exit_code;
}
