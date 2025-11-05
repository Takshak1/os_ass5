#include "types.h"
#include "stat.h"
#include "user.h"
#include "pstat.h"

int
main(void)
{
    struct pstat ps;
    int pid = getpid();
    
    if(settickets(pid, 10) < 0) {
        printf(1, "settickets(%d, 10) failed\n", pid);
        exit();
    }

    if(getpinfo(&ps) < 0) {
        printf(1, "getpinfo failed\n");
        exit();
    }

    // Simple tabbed formatting instead of width specifiers
    printf(1, "\nProcess Information:\n");
    printf(1, "PID \t InUse \t Tickets \t RunTicks \t BoostLeft\n");
    printf(1, "--------------------------------------------------------------\n");
    
    for(int i = 0; i < NPROC; i++) {
        if(ps.inuse[i]) {
            printf(1, "%d \t %d \t %d \t \t %d \t \t %d\n",
                   ps.pid[i],
                   ps.inuse[i],
                   ps.tickets[i],
                   ps.runticks[i],
                   ps.boostsleft[i]);
        }
    }
    
    exit();
}