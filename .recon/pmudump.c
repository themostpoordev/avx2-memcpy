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
static uint64_t rd(void){uint32_t a,d;__asm__ volatile("lfence;rdtsc":"=a"(a),"=d"(d)::"memory");return ((uint64_t)d<<32)|a;}
static void one(const char*tag,int group_flag,int fmt){
    struct perf_event_attr pe; memset(&pe,0,sizeof pe);
    pe.type=PERF_TYPE_HARDWARE; pe.size=sizeof pe; pe.config=PERF_COUNT_HW_CPU_CYCLES;
    pe.disabled=1; pe.inherit=0; pe.exclude_kernel=1; pe.exclude_hv=1;
    pe.read_format=fmt;
    int fd=syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
    if(fd<0){printf("%s: open fail\n",tag);return;}
    if(ioctl(fd,PERF_EVENT_IOC_RESET,group_flag)<0) printf("%s reset fail\n",tag);
    if(ioctl(fd,PERF_EVENT_IOC_ENABLE,group_flag)<0) printf("%s enable fail\n",tag);
    volatile uint64_t acc=0;
    uint64_t t0=rd(); double w0=ns_now();
    for(uint64_t i=0;i<100000000ull;i++){ acc+=i; __asm__ volatile("":::"memory"); }
    double w1=ns_now(); uint64_t t1=rd();
    if(ioctl(fd,PERF_EVENT_IOC_DISABLE,group_flag)<0) printf("%s disable fail\n",tag);
    unsigned long long b[6]; memset(b,0xEE,sizeof b);
    ssize_t got=read(fd,b,sizeof b);
    close(fd);
    printf("%-34s got=%2zd  b[0]=%-14llu b[1]=%-14llu b[2]=%-12llu b[3]=%-12llu\n",tag,got,b[0],b[1],b[2],b[3]);
    printf("    wall_ns=%.0f tsc=%.0f  b1/wall=%.4f GHz  b1/tsc=%.4f  tsc/wall=%.4f GHz\n",
           w1-w0,(double)(t1-t0),(double)b[1]/(w1-w0),(double)b[1]/(double)(t1-t0),(double)(t1-t0)/(w1-w0));
}
int main(void){
    const int G=PERF_FORMAT_GROUP, E=PERF_FORMAT_TOTAL_TIME_ENABLED, R=PERF_FORMAT_TOTAL_TIME_RUNNING;
    for(int i=0;i<2;i++){
      one("plain (no flags, no group fmt)", 0, E|R);
      one("GROUP fmt, no ioctl group flag",  0, G|E|R);
      one("GROUP fmt + ioctl group flag",   PERF_IOC_FLAG_GROUP, G|E|R);
      one("no fmt, ioctl group flag",       PERF_IOC_FLAG_GROUP, 0);
      printf("----\n");
    }
    return 0;
}
