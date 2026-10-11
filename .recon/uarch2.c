#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
static inline uint64_t rd(void){unsigned a,d;__asm__ volatile("lfence;rdtsc":"=a"(a),"=d"(d));return ((uint64_t)d<<32)|a;}
#define REP 5000000ULL
int main(void){
    unsigned char *b; if(posix_memalign((void**)&b,4096,4096))return 1;
    uint64_t t0,t1;
    /* 3 independent 256-bit loads per iteration group of 3; Zen3=3/cyc, Zen2=2/cyc */
    __asm__ volatile("mov %[p],%%rsi\n mov %[n],%%rcx\n1:\n"
      "vmovaps (%%rsi),%%ymm4\n vmovaps 32(%%rsi),%%ymm5\n vmovaps 64(%%rsi),%%ymm6\n"
      "vmovaps 96(%%rsi),%%ymm7\n vmovaps 128(%%rsi),%%ymm8\n vmovaps 160(%%rsi),%%ymm9\n"
      "dec %%rcx\n jnz 1b\n" : : [p]"r"(b),[n]"r"(REP) : "rsi","rcx","ymm4","ymm5","ymm6","ymm7","ymm8","ymm9","memory");
    t0=rd();
    __asm__ volatile("mov %[p],%%rsi\n mov %[n],%%rcx\n1:\n"
      "vmovaps (%%rsi),%%ymm4\n vmovaps 32(%%rsi),%%ymm5\n vmovaps 64(%%rsi),%%ymm6\n"
      "vmovaps 96(%%rsi),%%ymm7\n vmovaps 128(%%rsi),%%ymm8\n vmovaps 160(%%rsi),%%ymm9\n"
      "dec %%rcx\n jnz 1b\n" : : [p]"r"(b),[n]"r"(REP) : "rsi","rcx","ymm4","ymm5","ymm6","ymm7","ymm8","ymm9","memory");
    t1=rd();
    printf("256b loads: %.3f cycles/load -> %.2f loads/cycle  (Zen1:2, Zen2:2, Zen3:3)\n",(double)(t1-t0)/REP/6.0,6.0*REP/(double)(t1-t0));
    /* 256-bit store throughput: 2 stores/iter; Zen3=2/cyc, Zen2=1/cyc */
    __asm__ volatile("mov %[p],%%rsi\n mov %[n],%%rcx\n1:\n"
      "vmovaps %%ymm4,(%%rsi)\n vmovaps %%ymm5,32(%%rsi)\n"
      "dec %%rcx\n jnz 1b\n" : : [p]"r"(b),[n]"r"(REP) : "rsi","rcx","memory");
    t0=rd();
    __asm__ volatile("mov %[p],%%rsi\n mov %[n],%%rcx\n1:\n"
      "vmovaps %%ymm4,(%%rsi)\n vmovaps %%ymm5,32(%%rsi)\n"
      "dec %%rcx\n jnz 1b\n" : : [p]"r"(b),[n]"r"(REP) : "rsi","rcx","memory");
    t1=rd();
    printf("256b stores: %.3f cycles/store -> %.2f stores/cycle (Zen1:1(128b), Zen2:1, Zen3:2)\n",(double)(t1-t0)/REP/2.0,2.0*REP/(double)(t1-t0));
    return 0;
}
