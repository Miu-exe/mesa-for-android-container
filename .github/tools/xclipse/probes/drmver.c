/* Print the DRM driver name/version of every /dev/dri node (build in Termux or with the NDK, -ldrm). */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <xf86drm.h>
int main(void){
  const char *n[]={"/dev/dri/card0","/dev/dri/card1","/dev/dri/renderD128","/dev/dri/renderD129"};
  for(int i=0;i<4;i++){
    int fd=open(n[i],O_RDWR|O_CLOEXEC);
    if(fd<0){printf("%s: open failed: %s\n",n[i],strerror(errno));continue;}
    drmVersionPtr v=drmGetVersion(fd);
    if(!v){printf("%s: DRM_IOCTL_VERSION failed: %s\n",n[i],strerror(errno));close(fd);continue;}
    printf("%s: driver=%s %d.%d.%d date=%s desc=%s\n",n[i],v->name,v->version_major,v->version_minor,v->version_patchlevel,v->date,v->desc);
    drmFreeVersion(v);close(fd);
  }
}
