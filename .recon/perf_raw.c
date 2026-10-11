#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
struct spec { const char*name; unsigned type; unsigned long long cfg; };
int main(void){
    struct spec s[] = {
        {"RAW 0x076 ex_ret_ops",0x4,0x076},
        {"RAW 0x0C0 ex_ret_instructions",0x4,0x0C0},
        {"RAW 0x064 l2_request_g1",0x4,0x064},
        {"RAW 0x043 ls_l1_dtlb",0x4,0x043},
        {"RAW 0x060 ls_l1_dc",0x4,0x060},
        {"RAW 0x04FF",0x4,0x04FF},
        {"RAW 0x00FF",0x4,0x00FF},
        {"HWCACHE L1D/read/miss",PERF_TYPE_HW_CACHE,(PERF_COUNT_HW_CACHE_L1D)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16)},
        {"HWCACHE L1D/read/access",PERF_TYPE_HW_CACHE,(PERF_COUNT_HW_CACHE_L1D)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS<<16)},
        {"HWCACHE L1D/prefetch/miss",PERF_TYPE_HW_CACHE,(PERF_COUNT_HW_CACHE_L1D)|(PERF_COUNT_HW_CACHE_OP_PREFETCH<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16)},
        {"HWCACHE L1D/write/access",PERF_TYPE_HW_CACHE,(PERF_COUNT_HW_CACHE_L1D)|(PERF_COUNT_HW_CACHE_OP_WRITE<<8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS<<16)},
        {"HWCACHE LL/read/access",PERF_TYPE_HW_CACHE,(PERF_COUNT_HW_CACHE_LL)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS<<16)},
        {"HWCACHE DTLB/read/miss",PERF_TYPE_HW_CACHE,(PERF_COUNT_HW_CACHE_DTLB)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16)},
        {"HW CACHE_REFERENCES",PERF_TYPE_HARDWARE,PERF_COUNT_HW_CACHE_REFERENCES},
    };
    for(unsigned i=0;i<sizeof s/sizeof*s;i++){
        struct perf_event_attr pe; memset(&pe,0,sizeof pe);
        pe.type=s[i].type; pe.size=sizeof pe; pe.config=s[i].cfg; pe.disabled=0; pe.exclude_kernel=1; pe.exclude_hv=1;
        int fd=syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
        if(fd<0){ printf("  %-30s OPEN FAIL errno=%d (%s)\n",s[i].name,errno,strerror(errno)); continue; }
        long long a=0,b=0; if(read(fd,&a,8)!=8) puts("readfail");
        volatile unsigned long acc=0; for(int k=0;k<20000000;k++) acc+=k;
        if(read(fd,&b,8)!=8) puts("readfail"); close(fd);
        printf("  %-30s OPEN OK  delta=%lld %s\n",s[i].name,b-a,(b-a)==0?"<-- ZERO, NOT COUNTING":"");
    }
    return 0;
}
