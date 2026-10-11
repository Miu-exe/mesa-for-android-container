/* List DRM devices as libdrm sees them (bus type, nodes, platform name) - shows Xclipse is DRM_BUS_PLATFORM. */
#include <stdio.h>
#include <xf86drm.h>
int main(void){
  drmDevicePtr d[16]; int n=drmGetDevices2(0,d,16);
  printf("drmGetDevices2 -> %d\n",n);
  for(int i=0;i<n;i++){ printf("[%d] bustype=%d nodes=0x%x",i,d[i]->bustype,d[i]->available_nodes);
    for(int k=0;k<DRM_NODE_MAX;k++) if(d[i]->available_nodes&(1<<k)) printf(" %s",d[i]->nodes[k]);
    if(d[i]->bustype==DRM_BUS_PLATFORM) printf(" fullname=%s",d[i]->businfo.platform->fullname);
    printf("\n");}
  drmDevicePtr one; int r=drmGetDeviceFromDevId(makedev(226,128),0,&one); printf("FromDevId(226:128) -> %d\n",r);
}
