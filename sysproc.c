#include "types.h"
#include "arm.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "pstat.h"
#include "spinlock.h"

int sys_fork(void)
{
    return fork();
}

int sys_exit(void)
{
    exit();
    return 0;  // not reached
}

int sys_wait(void)
{
    return wait();
}

int sys_kill(void)
{
    int pid;

    if(argint(0, &pid) < 0) {
        return -1;
    }

    return kill(pid);
}

int sys_getpid(void)
{
    return proc->pid;
}

int sys_sbrk(void)
{
    int addr;
    int n;

    if(argint(0, &n) < 0) {
        return -1;
    }

    addr = proc->sz;

    if(growproc(n) < 0) {
        return -1;
    }

    return addr;
}

int sys_sleep(void)
{
    int n;
    uint ticks0;

    if(argint(0, &n) < 0) {
        return -1;
    }

    // OS-ASSIGNMENT-2-3
    acquire(&tickslock);
    ticks0 = ticks;
    proc->sleepuntil = ticks0 + n;
    while(ticks - ticks0 < n){
        if(proc->killed){
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }

    release(&tickslock);
    
    return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);

    return xticks;
}

int sys_getprocs(void)
{
    void *table;
    int max;
    if(argptr(0, (char**)&table, sizeof(table)) < 0)
        return -1;
    if(argint(1, &max) < 0)
        return -1;
    return getprocs(table, max);
}

extern int sys_settickets(void);
extern int sys_srand(void);
extern int sys_getpinfo(void);


int sys_settickets(void)
{
    int pid, tickets;
    
    if(argint(0, &pid) < 0)
        return -1;
    if(argint(1, &tickets) < 0)
        return -1;
        
    return settickets(pid, tickets);
}


int sys_srand(void)
{
  int seed;
  
  if(argint(0, &seed) < 0)
    return -1;
    
  srand((uint)seed);
  return 0;
}

int sys_getpinfo(void)
{
  struct pstat *ps;

  if(argptr(0, (void*)&ps, sizeof(*ps)) < 0)
    return -1;
    
  return getpinfo(ps);
}

// ----------------------
// System call wrappers
// ----------------------

int
sys_thread_create(void)
{
  uint thread_id;
  uint start_routine;  // function pointer
  uint arg;

  // Fetch arguments from user space
  if (argptr(0, (void*)&thread_id, sizeof(uint)) < 0)
    return -1;
  if (argptr(1, (void*)&start_routine, sizeof(void*)) < 0)
    return -1;
  if (argptr(2, (void*)&arg, sizeof(void*)) < 0)
    return -1;

  // Call kernel-level function
  return thread_create((uint*)thread_id, (void*(*)(void*))start_routine, (void*)arg);
}


int
sys_thread_exit(void)
{
    exit();  
    return 0; 
}


int
sys_thread_join(void)
{
  uint thread_id;
  void **retval;

  // Fetch thread id and return value pointer
  if (argint(0, (int*)&thread_id) < 0)
    return -1;
  if (argptr(1, (void*)&retval, sizeof(void**)) < 0)
    return -1;

  return thread_join(thread_id, retval);
}


int
sys_barrier_init(void)
{
    int n;
    
    if(argint(0, &n) < 0)
        return -1;
    
    barrier_init_impl(n);
    return 0;
}

int
sys_barrier_check(void)
{
    return barrier_check_impl();
}

// Channel management

static int next_channel = 0x10000;  
static struct spinlock channel_lock;


void
init_channel_lock(void)
{
    initlock(&channel_lock, "channel");
}

int
sys_getChannel(void)
{
    int channel;
    acquire(&channel_lock);
    channel = next_channel++;
    next_channel += 4; 
    release(&channel_lock);
    return channel;
}

int
sys_sleepChan(void)
{
    int channel;
    
    if(argint(0, &channel) < 0)
        return -1;
    
    if(channel <= 0)
        return -1;
    
    // Sleep on the channel address (cast channel to pointer)
    // Use a dummy lock since sleep requires one
    struct spinlock lk;
    initlock(&lk, "sleepchan");
    acquire(&lk);
    sleep((void*)(uint)channel, &lk);
    release(&lk);
    
    return 0;
}


int
sys_sigChan(void)
{
    int channel;
    
    if(argint(0, &channel) < 0)
        return -1;
    
    if(channel <= 0)
        return -1;
    
    // Wake up all processes on this channel
    wakeup((void*)channel);
    
    return 0;
}


int
sys_sigOneChan(void)
{
    int channel;
    
    if(argint(0, &channel) < 0)
        return -1;
    
    if(channel <= 0)
        return -1;
    
    // Wake up one process sleeping on this channel
    wakeup1_chan((void*)channel);
    
    return 0;
}