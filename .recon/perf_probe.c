#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <errno.h>

static int probe(unsigned long config, const char *name) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 0;
    pe.exclude_hv = 0;
    int fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
    if (fd < 0) { printf("  %-28s FAIL errno=%d (%s)\n", name, errno, strerror(errno)); return -1; }
    printf("  %-28s OK fd=%d\n", name, fd);
    close(fd);
    return 0;
}

int main(void) {
    printf("perf_event_open probe (pid=0, cpu=-1, no group):\n");
    probe(PERF_COUNT_HW_CPU_CYCLES, "PERF_COUNT_HW_CPU_CYCLES");
    probe(PERF_COUNT_HW_INSTRUCTIONS, "PERF_COUNT_HW_INSTRUCTIONS");
    probe(PERF_COUNT_HW_CACHE_REFERENCES, "CACHE_REFERENCES");
    probe(PERF_COUNT_HW_CACHE_MISSES, "CACHE_MISSES");
    /* software fallback */
    struct perf_event_attr pe; memset(&pe,0,sizeof pe);
    pe.type = PERF_TYPE_SOFTWARE; pe.size=sizeof pe; pe.config=PERF_COUNT_SW_TASK_CLOCK; pe.disabled=1;
    int fd = syscall(__NR_perf_event_open,&pe,0,-1,-1,0);
    if (fd<0) printf("  %-28s FAIL errno=%d (%s)\n","SW_TASK_CLOCK",errno,strerror(errno));
    else { printf("  %-28s OK fd=%d\n","SW_TASK_CLOCK",fd); close(fd); }
    return 0;
}
