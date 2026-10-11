#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
static double ns_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC_RAW,&t);return t.tv_sec*1e9+t.tv_nsec;}
struct ev { unsigned type; unsigned long long cfg; const char*name; };
static struct ev EV[] = {
  {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,        "cycles"},
  {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,      "instructions"},
  {PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS<<16), "l1d_loads"},
  {PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_L1D)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16),   "l1d_load_miss"},
  {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES, "cache_refs"},
  {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,     "cache_miss"},
  {PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,"branch_insn"},
  {PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,    "branch_miss"},
  {PERF_TYPE_HW_CACHE, (PERF_COUNT_HW_CACHE_DTLB)|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16), "dtlb_miss"},
  {PERF_TYPE_RAW, 0xFF60, "raw_l2_g1"},
  {PERF_TYPE_RAW, 0x0076, "raw_ret_ops"},
};
static void trial(int n){
    int fd[16];
    for(int i=0;i<n;i++){
        struct perf_event_attr pe; memset(&pe,0,sizeof pe);
        pe.size=sizeof pe; pe.disabled=1; pe.inherit=0; pe.exclude_kernel=1; pe.exclude_hv=1;
        pe.type=EV[i].type; pe.config=EV[i].cfg;
        pe.read_format=PERF_FORMAT_GROUP;
        if(i==0) pe.read_format|=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
        fd[i]=syscall(__NR_perf_event_open,&pe,0,-1,i==0?-1:fd[0],0);
        if(fd[i]<0){ printf("  n=%d open FAIL at %s\n",n,EV[i].name); for(int j=0;j<i;j++)close(fd[j]); return; }
    }
    enum { N=30000000 };
    ioctl(fd[0],PERF_EVENT_IOC_RESET,PERF_IOC_FLAG_GROUP);
    ioctl(fd[0],PERF_EVENT_IOC_ENABLE,PERF_IOC_FLAG_GROUP);
    volatile uint64_t acc=0; double w0=ns_now();
    for(uint64_t i=0;i<N;i++){ acc+=i; __asm__ volatile("":::"memory"); }
    double w1=ns_now();
    ioctl(fd[0],PERF_EVENT_IOC_DISABLE,PERF_IOC_FLAG_GROUP);
    unsigned long long b[20]; memset(b,0xEE,sizeof b);
    ssize_t got=read(fd[0],b,sizeof b);
    unsigned long long nr=b[0], en=b[1], ru=b[2];
    printf("  n_events=%d got=%zd  enabled=%llu running=%llu  running%%=%.1f%%  wall_ns=%.0f\n",
           n,got,en,ru,en?100.0*(double)ru/(double)en:0.0,w1-w0);
    for(unsigned long long i=0;i<nr && i<n;i++)
      printf("      %-14s value=%-12llu  per_wall_ns=%.4f\n",EV[i].name,b[3+i],(double)b[3+i]/(w1-w0));
    for(int i=0;i<n;i++) close(fd[i]);
}
int main(void){ for(int n=1;n<=11;n++) trial(n); return 0; }
