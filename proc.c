#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "arm.h"
#include "proc.h"
#include "spinlock.h"
#include "procinfo.h"
#include "pstat.h"

#define RAND_MAX 0x7fffffff

uint rseed = 0;

uint rand() {
    return rseed = (rseed * 1103515245 + 12345) & RAND_MAX;
}

//
// Process initialization:
// process initialize is somewhat tricky.
//  1. We need to fake the kernel stack of a new process as if the process
//     has been interrupt (a trapframe on the stack), this would allow us
//     to "return" to the correct user instruction.
//  2. We also need to fake the kernel execution for this new process. When
//     swtch switches to this (new) process, it will switch to its stack,
//     and reload registers with the saved context. We use forkret as the
//     return address (in lr register). (In x86, it will be the return address
//     pushed on the stack by the process.)
//
// The design of context switch in xv6 is interesting: after initialization,
// each CPU executes in the scheduler() function. The context switch is not
// between two processes, but instead, between the scheduler. Think of scheduler
// as the idle process.
//
struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

static struct proc *initproc;
struct proc *proc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// returns a pointer to the lottery winner.
struct proc *hold_lottery(int total_tickets) {
    if (total_tickets <= 0) {
        cprintf("this function should only be called when at least 1 process is RUNNABLE");
        return 0;
    }

    uint random_number = rand();
    uint winner_ticket_number = random_number % total_tickets;
    
    struct proc *p;
    int ticket_count = 0;
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE) {
            continue;
        }
        
        // Use boosted tickets if process has boosts remaining
        int effective_tickets = p->tickets;
        if(p->boostsleft > 0) {
            effective_tickets = p->tickets * 2;
        }
        
        ticket_count += effective_tickets;
        if(ticket_count > winner_ticket_number) {
            // Decrement boost if this process was boosted
            if(p->boostsleft > 0) {
                p->boostsleft--;
            }
            return p;
        }
    }
    
    return 0;
}

void pinit(void)
{
    initlock(&ptable.lock, "ptable");
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc* allocproc(void)
{
    struct proc *p;
    char *sp;

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == UNUSED) {
            goto found;
        }

    }

    release(&ptable.lock);
    return 0;

    found:
    p->state = EMBRYO;
    p->syscalls = 0;
    p->pid = nextpid++;
    p->tickets = 1;  // Initialize with 1 ticket
    p->boostsleft = 0;  // No boosts initially
    p->sleepstart = 0;  // Not sleeping initially
    p->sleepuntil = 0;  // No sleep target initially
    release(&ptable.lock);

    // Allocate kernel stack.
    if((p->kstack = alloc_page ()) == 0){
        p->state = UNUSED;
        return 0;
    }

    sp = p->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof (*p->tf);
    p->tf = (struct trapframe*)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint*)sp = (uint)trapret;

    sp -= 4;
    *(uint*)sp = (uint)p->kstack + KSTACKSIZE;

    sp -= sizeof (*p->context);
    p->context = (struct context*)sp;
    memset(p->context, 0, sizeof(*p->context));

    // skip the push {fp, lr} instruction in the prologue of forkret.
    // This is different from x86, in which the harderware pushes return
    // address before executing the callee. In ARM, return address is
    // loaded into the lr register, and push to the stack by the callee
    // (if and when necessary). We need to skip that instruction and let
    // it use our implementation.
    p->context->lr = (uint)forkret+4;

    return p;
}

void error_init ()
{
    panic ("failed to craft first process\n");
}


//PAGEBREAK: 32
// hand-craft the first user process. We link initcode.S into the kernel
// as a binary, the linker will generate __binary_initcode_start/_size
void userinit(void)
{
    struct proc *p;
    extern char _binary_initcode_start[], _binary_initcode_size[];

    p = allocproc();
    initproc = p;

    if((p->pgdir = kpt_alloc()) == NULL) {
        panic("userinit: out of memory?");
    }

    inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);

    p->sz = PTE_SZ;

    // craft the trapframe as if
    memset(p->tf, 0, sizeof(*p->tf));

    p->tf->r14_svc = (uint)error_init;
    p->tf->spsr = spsr_usr ();
    p->tf->sp_usr = PTE_SZ;	// set the user stack
    p->tf->lr_usr = 0;

    // set the user pc. The actual pc loaded into r15_usr is in
    // p->tf, the trapframe.
    p->tf->pc = 0;					// beginning of initcode.S

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint sz;

    sz = proc->sz;

    if(n > 0){
        if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0) {
            return -1;
        }

    } else if(n < 0){
        if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0) {
            return -1;
        }
    }

    proc->sz = sz;
    switchuvm(proc);

    return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
    int i, pid;
    struct proc *np;

    // Allocate process.
    if((np = allocproc()) == 0) {
        return -1;
    }

    // Copy process state from p.
    if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
        free_page(np->kstack);
        np->kstack = 0;
        np->state = UNUSED;
        return -1;
    }

    np->sz = proc->sz;
    np->parent = proc;
    np->tickets = proc->tickets;  // Inherit tickets from parent
    np->boostsleft = 0;  // Children don't inherit boosts
    np->sleepstart = 0;  // Not sleeping initially
    np->sleepuntil = 0;  // No sleep target initially
    *np->tf = *proc->tf;

    // Clear r0 so that fork returns 0 in the child.
    np->tf->r0 = 0;

    for(i = 0; i < NOFILE; i++) {
        if(proc->ofile[i]) {
            np->ofile[i] = filedup(proc->ofile[i]);
        }
    }

    np->cwd = idup(proc->cwd);

    pid = np->pid;
    np->state = RUNNABLE;
    safestrcpy(np->name, proc->name, sizeof(proc->name));

    return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
    struct proc *p;
    int fd;

    if(proc == initproc) {
        panic("init exiting");
    }

    // Close all open files.
    for(fd = 0; fd < NOFILE; fd++){
        if(proc->ofile[fd]){
            fileclose(proc->ofile[fd]);
            proc->ofile[fd] = 0;
        }
    }

    iput(proc->cwd);
    proc->cwd = 0;

    acquire(&ptable.lock);

    // Parent might be sleeping in wait().
    wakeup1(proc->parent);

    // Pass abandoned children to init.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->parent == proc){
            p->parent = initproc;

            if(p->state == ZOMBIE) {
                wakeup1(initproc);
            }
        }
    }

    // Jump into the scheduler, never to return.
    proc->state = ZOMBIE;
    sched();

    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
    struct proc *p;
    int havekids, pid;

    acquire(&ptable.lock);

    for(;;){
        // Scan through table looking for zombie children.
        havekids = 0;

        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->parent != proc) {
                continue;
            }

            havekids = 1;

            if(p->state == ZOMBIE){
                // Found one.
                pid = p->pid;
                free_page(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);
                p->state = UNUSED;
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                release(&ptable.lock);

                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if(!havekids || proc->killed){
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(proc, &ptable.lock);  //DOC: wait-sleep
    }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
    struct proc *p;

    for(;;){
        // Enable interrupts on this processor.
        sti();

        // Calculate total tickets for runnable processes
        acquire(&ptable.lock);
        
        int total_tickets = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state == RUNNABLE) {
                // Count boosted tickets if process has boosts remaining
                int effective_tickets = p->tickets;
                if(p->boostsleft > 0) {
                    effective_tickets = p->tickets * 2;
                }
                total_tickets += effective_tickets;
            }
        }
        
        if(total_tickets > 0) {
            // Hold lottery to choose process
            p = hold_lottery(total_tickets);
            
            if(p && p->state == RUNNABLE) {
                // Switch to chosen process
                proc = p;
                switchuvm(p);
                p->state = RUNNING;
                p->runticks++;
                if(p->boostsleft > 0) {
                    p->boostsleft--;  // Decrement boost when used
            }
                swtch(&cpu->scheduler, proc->context);
                // Process is done running for now
                proc = 0;
            }
        }

        release(&ptable.lock);
    }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void sched(void)
{
    int intena;

    //show_callstk ("sched");

    if(!holding(&ptable.lock)) {
        panic("sched ptable.lock");
    }

    if(cpu->ncli != 1) {
        panic("sched locks");
    }

    if(proc->state == RUNNING) {
        panic("sched running");
    }

    if(int_enabled ()) {
        panic("sched interruptible");
    }

    intena = cpu->intena;
    swtch(&proc->context, cpu->scheduler);
    cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    acquire(&ptable.lock);  //DOC: yieldlock
    proc->state = RUNNABLE;
    sched();
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
    static int first = 1;

    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first) {
        // Some initialization functions must be run in the context
        // of a regular process (e.g., they call sleep), and thus cannot
        // be run from main().
        first = 0;
        initlog();
    }

    // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
    //show_callstk("sleep");

    if(proc == 0) {
        panic("sleep");
    }

    if(lk == 0) {
        panic("sleep without lk");
    }

    // Must acquire ptable.lock in order to change p->state and then call
    // sched. Once we hold ptable.lock, we can be guaranteed that we won't
    // miss any wakeup (wakeup runs with ptable.lock locked), so it's okay
    // to release lk.
    if(lk != &ptable.lock){  //DOC: sleeplock0
        acquire(&ptable.lock);  //DOC: sleeplock1
        release(lk);
    }

    // Go to sleep.
    proc->chan = chan;
    proc->sleepstart = ticks;  // Record when process starts sleeping
    proc->state = SLEEPING;
    sched();

    // Tidy up.
    proc->chan = 0;

    // Reacquire original lock.
    if(lk != &ptable.lock){  //DOC: sleeplock2
        release(&ptable.lock);
        acquire(lk);
    }
}

// Sleep for a specific number of ticks
void sleepticks(int n)
{
    if(n <= 0)
        return;
        
    acquire(&ptable.lock);
    
    proc->chan = &ticks;
    proc->sleepstart = ticks;
    proc->sleepuntil = ticks + n;
    proc->state = SLEEPING;
    
    while(proc->state == SLEEPING && !proc->killed) {
        sched();
    }
    
    // Tidy up
    proc->chan = 0;
    proc->sleepuntil = 0;
    
    release(&ptable.lock);
}

//PAGEBREAK!
// Wake up all processes sleeping on chan. The ptable lock must be held.
static void wakeup1(void *chan)
{
    struct proc *p;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == SLEEPING && p->chan == chan) {
            // Special handling for tick-based sleeping
            if(chan == &ticks && p->sleepuntil > 0) {
                // Only wake up if enough time has passed
                if(ticks < p->sleepuntil)
                    continue;
            }
            
            // Calculate how long process was sleeping
            int sleep_ticks = ticks - p->sleepstart;
            
            // Add sleep ticks to existing boosts (Example 2 handling)
            p->boostsleft += sleep_ticks;
            
            p->state = RUNNABLE;
        }
    }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

// Kill the process with the given pid. Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
    struct proc *p;

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            p->killed = 1;

            // Wake process from sleep if necessary.
            if(p->state == SLEEPING) {
                p->state = RUNNABLE;
            }

            release(&ptable.lock);
            return 0;
        }
    }

    release(&ptable.lock);
    return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging. Runs when user
// types ^P on console. No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
            [UNUSED]    "unused",
            [EMBRYO]    "embryo",
            [SLEEPING]  "sleep ",
            [RUNNABLE]  "runble",
            [RUNNING]   "run   ",
            [ZOMBIE]    "zombie"
    };

    struct proc *p;
    char *state;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state == UNUSED) {
            continue;
        }

        if(p->state >= 0 && p->state < NELEM(states) && states[p->state]) {
            state = states[p->state];
        } else {
            state = "???";
        }

        cprintf("%d %s %s\n", p->pid, state, p->name);
    }

    show_callstk("procdump: \n");
}

int getprocs(void *table, int max) {
    struct uproc *utbl = (struct uproc *)table;
    struct proc *p;
    int n = 0;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC] && n < max; p++) {
        if(p->state == UNUSED)
            continue;
        utbl[n].pid = p->pid;
        utbl[n].ppid = p->parent ? p->parent->pid : 0;
        safestrcpy(utbl[n].name, p->name, sizeof(utbl[n].name));
        switch(p->state) {
            case UNUSED: safestrcpy(utbl[n].state, "UNUSED", 16); break;
            case EMBRYO: safestrcpy(utbl[n].state, "EMBRYO", 16); break;
            case SLEEPING: safestrcpy(utbl[n].state, "SLEEPING", 16); break;
            case RUNNABLE: safestrcpy(utbl[n].state, "RUNNABLE", 16); break;
            case RUNNING: safestrcpy(utbl[n].state, "RUNNING", 16); break;
            case ZOMBIE: safestrcpy(utbl[n].state, "ZOMBIE", 16); break;
        }
        utbl[n].syscalls = p->syscalls;
        utbl[n].tickets = p->tickets;
        utbl[n].boostsleft = p->boostsleft;
        n++;
    }
    release(&ptable.lock);
    return n;
}

int settickets(int pid, int tickets)
{
    struct proc *p;
    
    if(tickets <= 0)
        return -1;
        
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            p->tickets = tickets;
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

void srand(uint seed)
{
  rseed = seed;
}

int getpinfo(struct pstat *ps)
{
  struct proc *p;
  int i = 0;
  
  if(ps == 0)
    return -1;
    
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++, i++) {
    ps->inuse[i] = (p->state != UNUSED);
    ps->pid[i] = p->pid;
    ps->tickets[i] = p->tickets;
    ps->runticks[i] = p->runticks;
    ps->boostsleft[i] = p->boostsleft;
  }
  release(&ptable.lock);
  
  return 0;
}
int thread_create(uint *thread_id, void *(*start_routine)(void *), void *arg)
{
    struct proc *curproc = proc;
    struct proc *np;
    uint sp, stack_top;

    // Allocate new proc struct
    if ((np = allocproc()) == 0)
        return -1;

    // Share address space with parent
    np->pgdir = curproc->pgdir;
    np->sz = curproc->sz;

    // Copy open files and cwd
    for (int i = 0; i < NOFILE; i++)
        if (curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);
    safestrcpy(np->name, curproc->name, sizeof(curproc->name));

    // Allocate one page for the thread's stack
    uint old_sz = np->sz;
    uint new_sz = allocuvm(np->pgdir, old_sz, old_sz + PGSIZE);
    if (new_sz == 0) {
        np->state = UNUSED;
        return -1;
    }
    stack_top = new_sz;
    np->sz = new_sz;
    curproc->sz = new_sz;  // threads share same address space
    np->ustack = (char*)old_sz;  // Save stack base for cleanup

    // Initialize stack - make sure it's properly aligned
    sp = stack_top & ~(PGSIZE-1);  // Align to page boundary
    
    // Save room for stack guard
    sp -= 4;
    
    // Push argument onto stack
    sp -= 4;
    if(copyout(np->pgdir, sp, &arg, sizeof(arg)) < 0) {
        np->state = UNUSED;
        return -1;
    }
    
    // Setup trapframe carefully
    *np->tf = *curproc->tf;  // Copy parent's trapframe
    np->tf->r0 = (uint)arg;  // First argument in r0
    np->tf->pc = (uint)start_routine;  // Set program counter to start_routine
    np->tf->sp_usr = sp;  // Set user stack pointer
    np->tf->lr_usr = (uint)thread_exit;  // Set return address to thread_exit

    // Initialize thread metadata
    np->parent = curproc;
    np->mainthread = curproc;  // Point to the main thread (parent)
    np->is_thread = 1;  // Mark as thread
    np->tid = np->pid;  // Use pid as thread id
    np->retval = 0;
    np->state = RUNNABLE;

    // Store thread ID in user-provided location
    *thread_id = np->tid;
    
    return 0;
}

void
freeproc(struct proc *p)
{
    // 1. Free kernel stack if allocated
    if(p->kstack) {
        kfree_kalloc(p->kstack);   // release kernel stack page
        p->kstack = 0;
    }

    // 2. Free user stack if it's a thread
    if(p->is_thread && p->ustack) {
        kfree_kalloc((char*)p->ustack);  // release thread's user stack
        p->ustack = 0;
    }

    // 3. Close open files
    for(int i = 0; i < NOFILE; i++) {
        if(p->ofile[i]) {
            fileclose(p->ofile[i]);
            p->ofile[i] = 0;
        }
    }

    // 4. Release current working directory
    if(p->cwd) {
        iput(p->cwd);
        p->cwd = 0;
    }

    // 5. Reset thread metadata
    p->is_thread = 0;
    p->tid = 0;
    p->mainthread = 0;

    // 6. Reset process metadata
    p->pid = 0;
    safestrcpy(p->name, "", sizeof(p->name));
    p->sz = 0;

    // 7. Mark as UNUSED
    p->state = UNUSED;
    p->chan = 0;
    p->killed = 0;
}
void
thread_exit(void *retval)
{
    struct proc *curproc = proc;

    if (!curproc->is_thread) {
        return;  // No-op for non-threads
    }

    acquire(&ptable.lock);

    // Close all open files
    for(int fd = 0; fd < NOFILE; fd++){
        if(curproc->ofile[fd]){
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    // Save return value
    curproc->retval = retval;

    // Update state
    curproc->state = ZOMBIE;

    // Wake up main thread
    wakeup1(curproc->mainthread);

    // Switch to scheduler
    sched();

    panic("zombie thread exit");
}


// Wait for a thread to finish
int
thread_join(uint tid, void **retval)
{
    struct proc *p;
    int found;

    // Cannot join self or if not a main thread
    if (proc->tid == tid || proc->is_thread) {
        return -1;
    }

    acquire(&ptable.lock);
    for(;;) {
        found = 0;
        
        // Search for the thread
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->tid == tid && p->mainthread == proc && p->is_thread) {
                found = 1;
                if(p->state == ZOMBIE) {
                    // Copy return value if pointer provided
                    if(retval != 0)
                        *retval = p->retval;

                    // Clean up thread resources
                    if(p->kstack) {
                        kfree_kalloc(p->kstack);
                        p->kstack = 0;
                    }

                    // Clear thread state
                    p->pid = 0;
                    p->tid = 0;
                    p->parent = 0;
                    p->mainthread = 0;
                    p->is_thread = 0;
                    p->killed = 0;
                    p->state = UNUSED;
                    
                    release(&ptable.lock);
                    return 0;
                }
            }
        }

        // Thread not found
        if(!found) {
            release(&ptable.lock);
            return -1;
        }

        // Wait for thread to exit
        sleep(proc, &ptable.lock);
    }
    // Should never reach here
    release(&ptable.lock);
    return -1;
}

// Wake up one process sleeping on chan
void
wakeup1_chan(void *chan)
{
    struct proc *p;
    
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state == SLEEPING && p->chan == chan){
            p->state = RUNNABLE;
           
            break;
        }
    }
    release(&ptable.lock);
}