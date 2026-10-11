/* Dry-run preload for RADV (glibc, works under PRoot): a seccomp filter makes every
 * DRM_IOCTL_AMDGPU_CS fail with EPERM, so nothing reaches the GPU while the driver still
 * compiles all shaders and builds all command buffers. Combine with RADV_DEBUG=dumpibs,shaders
 * to get RADV's full streams for a test that would otherwise hang (and reboot) the phone:
 *
 *   LD_PRELOAD=/opt/radv/libnosubmit.so RADV_DEBUG=dumpibs,shaders,ir ./deqp-vk --deqp-case=...
 *
 * SECCOMP_RET_ERRNO needs no signal handler, unlike ibdump.c, so PRoot doesn't interfere.
 */
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "drm.h"
#include "amdgpu_drm.h"

__attribute__((constructor)) static void nosubmit_init(void)
{
   const unsigned int cs = (unsigned int)DRM_IOCTL_AMDGPU_CS;
   struct sock_filter insns[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ioctl, 0, 3),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, cs, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
   };
   struct sock_fprog prog = {sizeof(insns) / sizeof(insns[0]), insns};
   prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
   if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog))
      fprintf(stderr, "nosubmit: seccomp failed: %s\n", strerror(errno));
   else
      fprintf(stderr, "nosubmit: GPU submissions disabled (DRM_IOCTL_AMDGPU_CS -> EPERM)\n");
}
