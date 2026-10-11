#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <stdint.h>
static uint64_t rdtsc(void){unsigned a,d;__asm__ volatile("rdtsc":"=a"(a),"=d"(d));return ((uint64_t)d<<32)|a;}
int main(void){
    struct timespec a,b; clock_gettime(CLOCK_MONOTONIC_RAW,&a); uint64_t t0=rdtsc();
    struct timespec req={2,0}; nanosleep(&req,0);
    uint64_t t1=rdtsc(); clock_gettime(CLOCK_MONOTONIC_RAW,&b);
    double sec=(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9;
    printf("TSC freq ~ %.4f GHz (measured over %.6f s wall, %llu ticks)\n",(t1-t0)/sec/1e9,sec,(unsigned long long)(t1-t0));
    printf("CLOCK_MONOTONIC_RAW resolution: "); struct timespec r; clock_getres(CLOCK_MONOTONIC_RAW,&r); printf("%ld ns\n",r.tv_nsec);
    return 0;
}
