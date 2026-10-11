#include <stdlib.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
static uint64_t rdtsc(void){unsigned a,d;__asm__ volatile("rdtsc":"=a"(a),"=d"(d));return ((uint64_t)d<<32)|a;}
int main(int argc,char**argv){
    int nscale = argc>1?atoi(argv[1]):1;
    int fds[16]; int n=0;
    const char *nm[16]; unsigned long long cfg[16]; unsigned ty[16];
    int want=argc>2?atoi(argv[2]):1;
    for(int k=0;k<want;k++){ ty[n]=PERF_TYPE_HARDWARE; cfg[n]=PERF_COUNT_HW_CPU_CYCLES; nm[n]="cycles"; n++; }
    if(want>1){ ty[n]=PERF_TYPE_HARDWARE; cfg[n]=PERF_COUNT_HW_INSTRUCTIONS; nm[n]="instructions"; n++; }
    for(int k=0;k<n;k++){ struct perf_event_attr pe; memset(&pe,0,sizeof pe); pe.type=ty[k]; pe.size=sizeof pe; pe.config=cfg[k]; pe.disabled=0; pe.exclude_kernel=1; pe.exclude_hv=1;
        int fd=syscall(__NR_perf_event_open,&pe,0,-1,-1,0); if(fd<0){printf("open fail %s errno=%d\n",nm[k],errno);return 1;} fds[k]=fd; }
    (void)nscale;
    uint64_t t0=rdtsc(); long long c0[16]; for(int k=0;k<n;k++){c0[k]=0; read(fds[k],&c0[k],8);}
    volatile uint64_t sink=0; uint64_t acc=0;
    for(long i=0;i<300000000L;i++){ acc += (uint64_t)i; __asm__ volatile("" : "+r"(acc)); }
    sink=acc;
    long long c1[16]; for(int k=0;k<n;k++){c1[k]=0; read(fds[k],&c1[k],8);}
    uint64_t t1=rdtsc();
    printf("TSC ticks = %llu  (wall %.4f s @2.7947GHz)\n",(unsigned long long)(t1-t0),(t1-t0)/2.7947e9);
    for(int k=0;k<n;k++) printf("  %-14s = %lld   ratio perf/TSC = %.4f\n",nm[k],(long long)(c1[k]-c0[k]),(double)(c1[k]-c0[k])/(double)(t1-t0));
    (void)sink;
    return 0;
}
