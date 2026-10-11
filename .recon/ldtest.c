#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
int main(void){char a[64],b[64];memset(a,0,64);memcpy(b,a,64);memset(b,1,64);printf("%p\n",(void*)b);return 0;}
