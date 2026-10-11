#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <cpuid.h>
static inline uint64_t rd(void){unsigned a,d;__asm__ volatile("lfence;rdtsc":"=a"(a),"=d"(d));return ((uint64_t)d<<32)|a;}
#define REP 20000000ULL
__attribute__((target("avx2")))
int main(void){
    unsigned eax,ebx,ecx,edx;
    __cpuid(1,eax,ebx,ecx,edx);
    printf("CPUID.1  EAX=0x%08x (family=0x%x model=0x%x stepping=%u)\n",eax,((eax>>8)&0xf)+((eax>>20)&0xff),((eax>>4)&0xf)+(((eax>>16)&0xf)<<4),eax&0xf);
    char brand[49]={0};
    for(int i=0;i<3;i++){ unsigned a,b,c,d; __cpuid(0x80000002u+i,a,b,c,d); memcpy(brand+i*16,&a,4);memcpy(brand+i*16+4,&b,4);memcpy(brand+i*16+8,&c,4);memcpy(brand+i*16+12,&d,4);}
    printf("brand   = '%s'\n",brand);
    unsigned a4,b4,c4,d4; __cpuid(0x40000000u,a4,b4,c4,d4); char hv[13]={0}; memcpy(hv,&b4,4);memcpy(hv+4,&c4,4);memcpy(hv+8,&d4,4);
    printf("hypervisor = '%s'\n",hv);
    __cpuid(0x80000008u,a4,b4,c4,d4); printf("CPUID.80000008 ECX[7:0]=%u (cores-1)... addr bits phys=%u virt=%u\n",c4&0xff,a4&0xff,(a4>>8)&0xff);

    /* --- throughput fingerprints (pinned, rdtsc-based) --- */
    uint64_t t0,t1;
    /* A: independent 256-bit vaddps: Zen1 = 2/ymm (0.5/cyc), Zen2+ = 0.5 (2/cyc) */
    __asm__ volatile(
        "vxorps %%ymm0,%%ymm0,%%ymm0\n vxorps %%ymm1,%%ymm1,%%ymm1\n"
        "vxorps %%ymm2,%%ymm2,%%ymm2\n vxorps %%ymm3,%%ymm3,%%ymm3\n"
        "mov %[n],%%rcx\n"
        "1:\n"
        "vaddps %%ymm1,%%ymm0,%%ymm4\n vaddps %%ymm2,%%ymm0,%%ymm5\n"
        "vaddps %%ymm3,%%ymm0,%%ymm6\n vaddps %%ymm1,%%ymm0,%%ymm7\n"
        "dec %%rcx\n jnz 1b\n"
        : : [n]"r"(REP) : "rcx","ymm0","ymm1","ymm2","ymm3","ymm4","ymm5","ymm6","ymm7");
    t0=rd();
    __asm__ volatile(
        "vxorps %%ymm0,%%ymm0,%%ymm0\n vxorps %%ymm1,%%ymm1,%%ymm1\n"
        "vxorps %%ymm2,%%ymm2,%%ymm2\n vxorps %%ymm3,%%ymm3,%%ymm3\n"
        "mov %[n],%%rcx\n"
        "1:\n"
        "vaddps %%ymm1,%%ymm0,%%ymm4\n vaddps %%ymm2,%%ymm0,%%ymm5\n"
        "vaddps %%ymm3,%%ymm0,%%ymm6\n vaddps %%ymm1,%%ymm0,%%ymm7\n"
        "dec %%rcx\n jnz 1b\n"
        : : [n]"r"(REP) : "rcx","ymm0","ymm1","ymm2","ymm3","ymm4","ymm5","ymm6","ymm7");
    t1=rd();
    double cyc_per_add=(double)(t1-t0)/(double)REP/4.0;
    printf("AVX2 vaddps ymm : %.3f cycles/add  (%.2f ymm adds/cycle)\n",cyc_per_add,1.0/cyc_per_add);

    /* B: 128-bit vaddps xmm */
    __asm__ volatile(
        "vxorps %%xmm0,%%xmm0,%%xmm0\n vxorps %%xmm1,%%xmm1,%%xmm1\n vxorps %%xmm2,%%xmm2,%%xmm2\n vxorps %%xmm3,%%xmm3,%%xmm3\n"
        "mov %[n],%%rcx\n" "1:\n"
        "vaddps %%xmm1,%%xmm0,%%xmm4\n vaddps %%xmm2,%%xmm0,%%xmm5\n vaddps %%xmm3,%%xmm0,%%xmm6\n vaddps %%xmm1,%%xmm0,%%xmm7\n"
        "dec %%rcx\n jnz 1b\n" : : [n]"r"(REP) : "rcx","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7");
    t0=rd();
    __asm__ volatile(
        "vxorps %%xmm0,%%xmm0,%%xmm0\n vxorps %%xmm1,%%xmm1,%%xmm1\n vxorps %%xmm2,%%xmm2,%%xmm2\n vxorps %%xmm3,%%xmm3,%%xmm3\n"
        "mov %[n],%%rcx\n" "1:\n"
        "vaddps %%xmm1,%%xmm0,%%xmm4\n vaddps %%xmm2,%%xmm0,%%xmm5\n vaddps %%xmm3,%%xmm0,%%xmm6\n vaddps %%xmm1,%%xmm0,%%xmm7\n"
        "dec %%rcx\n jnz 1b\n" : : [n]"r"(REP) : "rcx","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7");
    t1=rd();
    printf("SSE  vaddps xmm : %.3f cycles/add  (%.2f xmm adds/cycle)\n",(double)(t1-t0)/REP/4.0,4.0/((double)(t1-t0)/REP));
    return 0;
}
