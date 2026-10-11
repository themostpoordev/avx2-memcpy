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
static int open_cyc(void){
    struct perf_event_attr pe; memset(&pe,0,sizeof pe);
    pe.type=PERF_TYPE_HARDWARE; pe.size=sizeof pe; pe.config=PERF_COUNT_HW_CPU_CYCLES;
    pe.disabled=1; pe.exclude_kernel=1; pe.exclude_hv=1; pe.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    int fd=syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
    if(fd<0){perror("perf_event_open");return -1;}
    return fd;
}
static void run(const char*name, long iters, int kind){
    volatile uint64_t vacc=0; uint64_t acc=0;
    int fd=open_cyc(); if(fd<0) return;
    ioctl(fd,PERF_EVENT_IOC_RESET,0); ioctl(fd,PERF_EVENT_IOC_ENABLE,0);
    uint64_t t0=rd(); double n0=ns_now();
    if(kind==0){ for(long i=0;i<iters;i++){ vacc += (uint64_t)i; } }                       /* volatile RMW chain */
    else if(kind==1){ for(long i=0;i<iters;i++){ acc += (uint64_t)i; __asm__ volatile("" : "+r"(acc)); } }  /* register chain */
    else if(kind==2){ for(long i=0;i<iters;i++){ __asm__ volatile("" ::: "memory"); } }    /* empty loop */
    else { for(long i=0;i<iters;i++){ __asm__ volatile("pause"); } }                       /* pause loop */
    double n1=ns_now(); uint64_t t1=rd();
    ioctl(fd,PERF_EVENT_IOC_DISABLE,0);
    unsigned long long buf[5]; read(fd,&buf,sizeof buf);
    uint64_t cyc=buf[0]; unsigned long long en=buf[2],ru=buf[3];
    close(fd);
    double wall=(n1-n0), tsc=(double)(t1-t0);
    printf("%-18s iters=%-11ld wall=%8.4f s tsc=%12.0f (%7.3f GHz) cycles=%12llu  cyc/iter=%.3f  cyc/tsc=%.4f  enable/run=%llu/%llu  cyc/wall_ns=%.4f\n",
        name, iters, wall/1e9, tsc, tsc/wall, cyc, (double)cyc/(double)iters, (double)cyc/tsc, en, ru, (double)cyc/wall);
    if(acc) printf("");
}
int main(void){
    run("volatile RMW",   50000000L, 0);
    run("register chain",200000000L, 1);
    run("empty loop",    200000000L, 2);
    run("pause loop",      2000000L, 3);
    return 0;
}
