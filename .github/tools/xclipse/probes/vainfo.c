/* Print the GPU virtual address layout (low/high VA ranges) reported by the amdgpu kernel driver. */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <xf86drm.h>
#include <amdgpu_drm.h>
int main(void){
  int fd=open("/dev/dri/renderD128",O_RDWR);
  struct drm_amdgpu_info_device d; memset(&d,0,sizeof d);
  struct drm_amdgpu_info r; memset(&r,0,sizeof r); r.return_pointer=(uintptr_t)&d; r.return_size=sizeof d; r.query=AMDGPU_INFO_DEV_INFO;
  printf("ret=%d\n",drmCommandWrite(fd,DRM_AMDGPU_INFO,&r,sizeof r));
  printf("va_offset=0x%llx va_max=0x%llx high_va_offset=0x%llx high_va_max=0x%llx\n",(unsigned long long)d.virtual_address_offset,(unsigned long long)d.virtual_address_max,(unsigned long long)d.high_va_offset,(unsigned long long)d.high_va_max);
  printf("va_alignment=%u pte_fragment_size=%u gart_page_size=%u\n",d.virtual_address_alignment,d.pte_fragment_size,d.gart_page_size);
  printf("ce_ram=%u prim_buf=%u pos_buf=%u cntl_sb=%u param_buf=%u\n",d.ce_ram_size,d.prim_buf_gpu_addr?1:0,d.pos_buf_gpu_addr?1:0,d.cntl_sb_buf_gpu_addr?1:0,d.param_buf_gpu_addr?1:0);
}
