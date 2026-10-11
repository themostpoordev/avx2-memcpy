#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

static long long rd(long long *v){ long long r; __asm__ volatile("rdtsc" : "=A"(r)); (void)v; return r; }

static int openhw(unsigned type, unsigned long long cfg, const char *name, int *fdout){
    struct perf_event_attr pe; memset(&pe,0,sizeof pe);
    pe.type=type; pe.size=sizeof pe; pe.config=cfg; pe.disabled=0;
    pe.exclude_kernel=1; pe.exclude_hv=1;
    int fd=syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
    if(fd<0){ printf("  %-30s OPEN FAIL errno=%d (%s)\n",name,errno,strerror(errno)); return -1; }
    long long z=0; if(read(fd,&z,sizeof z)!=sizeof z){ printf("  %-30s READ FAIL\n",name); return -1;}
    *fdout=fd; return 0;
}
#define HW(c) openhw(PERF_TYPE_HARDWARE,(c),#c,&fd[i])
#define HWCACHE(cid,op,res,name) openhw(PERF_TYPE_HW_CACHE,((unsigned long long)(cid))|((op)<<8)|((res)<<16),name,&fd[i])
int main(void){
    int fd[12]; const char *names[12]; int i=0,n=0;
    if(HW(PERF_COUNT_HW_CPU_CYCLES)==0){names[n++]="hw:cycles";i++;}
    if(HW(PERF_COUNT_HW_INSTRUCTIONS)==0){names[n++]="hw:instructions";i++;}
    if(HW(PERF_COUNT_HW_CACHE_REFERENCES)==0){names[n++]="hw:cache-refs";i++;}
    if(HW(PERF_COUNT_HW_CACHE_MISSES)==0){names[n++]="hw:cache-misses";i++;}
    if(HW(PERF_COUNT_HW_BRANCH_INSTRUCTIONS)==0){names[n++]="hw:branch-insns";i++;}
    if(HW(PERF_COUNT_HW_BRANCH_MISSES)==0){names[n++]="hw:branch-misses";i++;}
    if(HWCACHE(PERF_COUNT_HW_CACHE_L1D,PERF_COUNT_HW_CACHE_OP_READ,PERF_COUNT_HW_CACHE_RESULT_ACCESS,"L1-dcache-loads")==0){names[n++]="L1-dcache-loads";i++;}
    if(HWCACHE(PERF_COUNT_HW_CACHE_L1D,PERF_COUNT_HW_CACHE_OP_READ,PERF_COUNT_HW_CACHE_RESULT_MISS,"L1-dcache-load-misses")==0){names[n++]="L1-dcache-load-misses";i++;}
    if(HWCACHE(PERF_COUNT_HW_CACHE_L1D,PERF_COUNT_HW_CACHE_OP_WRITE,PERF_COUNT_HW_CACHE_RESULT_MISS,"L1-dcache-store-misses")==0){names[n++]="L1-dcache-store-misses";i++;}
    if(HWCACHE(PERF_COUNT_HW_CACHE_LL,PERF_COUNT_HW_CACHE_OP_READ,PERF_COUNT_HW_CACHE_RESULT_ACCESS,"LLC-loads")==0){names[n++]="LLC-loads";i++;}
    if(HWCACHE(PERF_COUNT_HW_CACHE_LL,PERF_COUNT_HW_CACHE_OP_READ,PERF_COUNT_HW_CACHE_RESULT_MISS,"LLC-load-misses")==0){names[n++]="LLC-load-misses";i++;}
    if(HWCACHE(PERF_COUNT_HW_CACHE_L1I,PERF_COUNT_HW_CACHE_OP_READ,PERF_COUNT_HW_CACHE_RESULT_MISS,"L1-icache-load-misses")==0){names[n++]="L1-icache-load-misses";i++;}
    printf("opened %d events\n",n);
    long long t0=rd(0);
    volatile unsigned long s=0; for(int k=0;k<200000000;k++) s+=k;
    long long t1=rd(0);
    printf("busy loop: %.3f Gcycles(TSC)\n",(double)(t1-t0)/1e9);
    for(int j=0;j<n;j++){ long long v=0; read(fd[j],&v,sizeof v); printf("  %-24s = %lld\n",names[j],v); close(fd[j]); }
    (void)s;
    return 0;
}
