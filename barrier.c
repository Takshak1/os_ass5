#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"

struct {
    struct spinlock lock;
    int count;          // Total number of processes that should arrive
    int arrived;        // Number of processes that have arrived so far
    int initialized;    // Whether barrier has been initialized
} barrier;

void
barrier_init_impl(int n)
{
    acquire(&barrier.lock);
    
    if(n <= 0) {
        release(&barrier.lock);
        return;
    }
    
    barrier.count = n;
    barrier.arrived = 0;
    barrier.initialized = 1;
    
    release(&barrier.lock);
}

int
barrier_check_impl(void)
{
    acquire(&barrier.lock);
    
    // Check if barrier is initialized
    if(!barrier.initialized) {
        release(&barrier.lock);
        return -1;
    }
    
    // Increment arrival count
    barrier.arrived++;
    
    // If this is not the last process, sleep
    if(barrier.arrived < barrier.count) {
        // Sleep on the barrier structure itself
        sleep(&barrier, &barrier.lock);
        release(&barrier.lock);
        return 0;
    }
    
    // This is the last process (N-th process)
    // Wake up all waiting processes
    wakeup(&barrier);
    
    // Reset for potential future use (though problem says single use)
    barrier.arrived = 0;
    barrier.initialized = 0;
    
    release(&barrier.lock);
    return 0;
}

void
barrier_init_lock(void)
{
    initlock(&barrier.lock, "barrier");
    barrier.count = 0;
    barrier.arrived = 0;
    barrier.initialized = 0;
}