#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
int main(void){
    size_t N = 32u<<20; unsigned char *buf = aligned_alloc(64,N); memset(buf,1,N);
    unsigned long long cfgs[] = {0x0160,0xFF60,0x0164,0xFF64,0x0170,0xFF70,0x0178,0xFF78,
                                 0x0104,0xFF04,0x019C,0x01E0,0x0140,0x0143,0x0144,0x0145,0x0146,
                                 0x0168,0x016C,0x010B,0x01B0,0x01B8,0x0187,0x01A0};
    for(unsigned i=0;i<sizeof cfgs/sizeof*cfgs;i++){
        struct perf_event_attr pe; memset(&pe,0,sizeof pe);
        pe.type=PERF_TYPE_RAW; pe.size=sizeof pe; pe.config=cfgs[i]; pe.disabled=0; pe.exclude_kernel=1; pe.exclude_hv=1;
        int fd=syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
        if(fd<0){ printf("  raw 0x%04llX  OPEN FAIL errno=%d\n",cfgs[i],errno); continue; }
        long long a=0,b=0; read(fd,&a,8);
        volatile unsigned long s=0; for(int r=0;r<8;r++) for(size_t k=0;k<N;k+=64) s+=buf[k];
        read(fd,&b,8); close(fd);
        printf("  raw 0x%04llX  delta=%-12lld %s\n",cfgs[i],b-a,(b-a)==0?"<-- zero":"");
    }
    return 0;
}
