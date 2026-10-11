#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc,char**argv){
    size_t n=argc>1?(size_t)atol(argv[1]):4096;
    char *a=malloc(n+64),*b=malloc(n+64);
    void *(*f)(void*,const void*,size_t)=memcpy;      /* runtime indirection keeps call */
    void *(*g)(void*,int,size_t)=memset;
    if(argc>2){ f((void*)b,a,n); g(a,1,n); }
    printf("%p %p\n",(void*)f,(void*)g); (void)b;(void)a; return 0;
}
