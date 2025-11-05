#include "user.h"

int main() {
    struct uproc procs[64];
    int n, i;
    n = getprocs(procs, 64);
    printf(1, "PID    PPID    NAME    STATE    SYSCALLS\n");
    for(i = 0; i < n; i++) {
    printf(1, "%d    %d    %s    %s    %d\n", procs[i].pid, procs[i].ppid, procs[i].name, procs[i].state, procs[i].syscalls);
    }
    exit();
}