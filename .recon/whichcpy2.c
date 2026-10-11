#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
int main(void){
    void *f=(void*)memcpy,*g=(void*)memset;
    Dl_info i,j; dladdr(f,&i); dladdr(g,&j);
    printf("libc base      = %p\n", i.dli_fbase);
    printf("memcpy resolved= %p off=0x%lx name=%s\n", f, (unsigned long)((char*)f-(char*)i.dli_fbase), i.dli_sname?i.dli_sname:"(ifunc, hidden)");
    printf("memset resolved= %p off=0x%lx name=%s\n", g, (unsigned long)((char*)g-(char*)j.dli_fbase), j.dli_sname?j.dli_sname:"(ifunc, hidden)");
    return 0;
}
