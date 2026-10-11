/* Query amdgpu AMDGPU_INFO_DEV_INFO / MEMORY / HW_IP_INFO on renderD128 (family, CUs, rings, alignments). */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <xf86drm.h>
#include <amdgpu_drm.h>
static int q(int fd,unsigned id,void*out,unsigned sz){
  struct drm_amdgpu_info r; memset(&r,0,sizeof r);
  r.return_pointer=(uintptr_t)out; r.return_size=sz; r.query=id;
  return drmCommandWrite(fd,DRM_AMDGPU_INFO,&r,sizeof r);
}
int main(void){
  int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);
  struct drm_amdgpu_info_device d; memset(&d,0,sizeof d);
  int r=q(fd,AMDGPU_INFO_DEV_INFO,&d,sizeof d);
  printf("DEV_INFO ret=%d errno=%s\n",r,r?strerror(errno):"ok");
  if(!r){
    printf("device_id=0x%x chip_rev=%u external_rev=%u pci_rev=%u family=%u\n",d.device_id,d.chip_rev,d.external_rev,d.pci_rev,d.family);
    printf("num_se=%u cu_active=%u sh_per_se=%u max_engine_clock=%llu max_memory_clock=%llu\n",d.num_shader_engines,d.cu_active_number,d.num_shader_arrays_per_engine,(unsigned long long)d.max_engine_clock,(unsigned long long)d.max_memory_clock);
    printf("vram_type=%u vram_bit_width=%u ids_flags=0x%llx gc_double_offchip_lds=%u wave_front=%u num_rb_pipes=%u gart_page=%u tcc_disabled_mask=0x%x\n",d.vram_type,d.vram_bit_width,(unsigned long long)d.ids_flags,d.gc_double_offchip_lds_buf,d.wave_front_size,d.num_rb_pipes,d.gart_page_size,d.tcc_disabled_mask);
  }
  struct drm_amdgpu_memory_info m; memset(&m,0,sizeof m);
  r=q(fd,AMDGPU_INFO_MEMORY,&m,sizeof m);
  printf("MEMORY ret=%d vram=%llu MB gtt=%llu MB cpu_vis_vram=%llu MB\n",r,(unsigned long long)m.vram.total_heap_size>>20,(unsigned long long)m.gtt.total_heap_size>>20,(unsigned long long)m.cpu_accessible_vram.total_heap_size>>20);
  uint32_t v=0; for(unsigned t=0;t<=AMDGPU_HW_IP_VCN_JPEG;t++){ struct drm_amdgpu_info_hw_ip ip; memset(&ip,0,sizeof ip);
    struct drm_amdgpu_info rq; memset(&rq,0,sizeof rq); rq.return_pointer=(uintptr_t)&ip; rq.return_size=sizeof ip; rq.query=AMDGPU_INFO_HW_IP_INFO; rq.query_hw_ip.type=t;
    r=drmCommandWrite(fd,DRM_AMDGPU_INFO,&rq,sizeof rq);
    printf("HW_IP type=%u ret=%d ver=%u.%u rings=0x%x start_align=%u size_align=%u ipdisc=0x%x\n",t,r,ip.hw_ip_version_major,ip.hw_ip_version_minor,ip.available_rings,ip.ib_start_alignment,ip.ib_size_alignment,ip.ip_discovery_version);}
  r=q(fd,AMDGPU_INFO_ACCEL_WORKING,&v,4); printf("ACCEL_WORKING ret=%d val=%u\n",r,v);
}
