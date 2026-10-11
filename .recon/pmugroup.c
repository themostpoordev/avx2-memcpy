#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
static double ns_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC_RAW,&t);return t.tv_sec*1e9+t.tv_nsec;}
static void trial(int n_ev, int use_raw_l2){
    struct perf_event_attr pe; memset(&pe,0,sizeof pe);
    pe.size=sizeof pe; pe.disabled=1; pe.inherit=0; pe.exclude_kernel=1; pe.exclude_hv=1;
    pe.read_format=PERF_FORMAT_GROUP|PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    pe.type=PERF_TYPE_HARDWARE; pe.config=PERF_COUNT_HW_CPU_CYCLES;
    int lead=syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
    if(lead<0){printf("lead open fail\n");return;}
    /* cache L1D read access */
    memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1;
    pe.read_format=PERF_FORMAT_GROUP;
    pe.type=PERF_TYPE_HW_CACHE;
    pe.config=PERF_COUNT_HW_CACHE_L1D|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS<<16);
    int f1=syscall(__NR_perf_event_open,&pe,0,-1,lead,0);
    memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
    pe.type=PERF_TYPE_HARDWARE; pe.config=PERF_COUNT_HW_INSTRUCTIONS;
    int f2=syscall(__NR_perf_event_open,&pe,0,-1,lead,0);
    memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
    pe.type=PERF_TYPE_HARDWARE; pe.config=PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
    int f3=syscall(__NR_perf_event_open,&pe,0,-1,lead,0);
    memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
    pe.type=PERF_TYPE_HARDWARE; pe.config=PERF_COUNT_HW_CACHE_MISSES;
    int f4=syscall(__NR_perf_event_open,&pe,0,-1,lead,0);
    int extra[6]; int ne=0;
    if(n_ev>4){
      memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
      pe.type=PERF_TYPE_HW_CACHE;
      pe.config=PERF_COUNT_HW_CACHE_L1D|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16);
      extra[ne]=syscall(__NR_perf_event_open,&pe,0,-1,lead,0); ne++;
      memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
      pe.type=PERF_TYPE_HARDWARE; pe.config=PERF_COUNT_HW_BRANCH_MISSES;
      extra[ne]=syscall(__NR_perf_event_open,&pe,0,-1,lead,0); ne++;
      memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
      pe.type=PERF_TYPE_HW_CACHE;
      pe.config=PERF_COUNT_HW_CACHE_DTLB|(PERF_COUNT_HW_CACHE_OP_READ<<8)|(PERF_COUNT_HW_CACHE_RESULT_MISS<<16);
      extra[ne]=syscall(__NR_perf_event_open,&pe,0,-1,lead,0); ne++;
    }
    if(use_raw_l2){
      memset(&pe,0,sizeof pe); pe.size=sizeof pe; pe.disabled=1; pe.read_format=PERF_FORMAT_GROUP;
      pe.type=PERF_TYPE_RAW; pe.config=0xFF60;
      extra[ne]=syscall(__NR_perf_event_open,&pe,0,-1,lead,0); ne++;
    }
    int total=4+ne;
    printf("--- n_events=%d raw_l2=%d (lead=%d f1=%d f2=%d f3=%d f4=%d)\n",total,use_raw_l2,lead,f1,f2,f3,f4);
    ioctl(lead,PERF_EVENT_IOC_RESET,PERF_IOC_FLAG_GROUP);
    ioctl(lead,PERF_EVENT_IOC_ENABLE,PERF_IOC_FLAG_GROUP);
    volatile uint64_t acc=0; double w0=ns_now();
    for(uint64_t i=0;i<30000000ull;i++){ acc+=i; __asm__ volatile("":::"memory"); }
    double w1=ns_now();
    ioctl(lead,PERF_EVENT_IOC_DISABLE,PERF_IOC_FLAG_GROUP);
    unsigned long long b[24]; memset(b,0xEE,sizeof b);
    ssize_t got=read(lead,b,sizeof b);
    printf("    got=%zd bytes wall_ns=%.0f\n",got,w1-w0);
    for(int i=0;i<(int)(got/8)&&i<16;i++)
        printf("      b[%2d]=%llu%s\n",i,b[i],(b[i]<200000000ull)?"":"");
    close(lead); for(int i=0;i<ne;i++) if(extra[i]>=0) close(extra[i]);
    close(f1);close(f2);close(f3);close(f4);
}
int main(void){ trial(1,0); trial(4,0); trial(7,0); trial(8,1); return 0; }
