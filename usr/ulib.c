#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

char*
strcpy(char *s, char *t)
{
    char *os;
    
    os = s;
    while((*s++ = *t++) != 0)
        ;
    return os;
}

int
strcmp(const char *p, const char *q)
{
    while(*p && *p == *q)
        p++, q++;
    return (uchar)*p - (uchar)*q;
}

uint
strlen(char *s)
{
    int n;
    
    for(n = 0; s[n]; n++)
        ;
    return n;
}

void*
memset(void *dst, int v, uint n)
{
	uint8	*p;
	uint8	c;
	uint32	val;
	uint32	*p4;

	p   = dst;
	c   = v & 0xff;
	val = (c << 24) | (c << 16) | (c << 8) | c;

	// set bytes before whole uint32
	for (; (n > 0) && ((uint)p % 4); n--, p++){
		*p = c;
	}

	// set memory 4 bytes a time
	p4 = (uint*)p;

	for (; n >= 4; n -= 4, p4++) {
		*p4 = val;
	}

	// set leftover one byte a time
	p = (uint8*)p4;

	for (; n > 0; n--, p++) {
		*p = c;
	}

	return dst;
}

char*
strchr(const char *s, char c)
{
    for(; *s; s++)
        if(*s == c)
            return (char*)s;
    return 0;
}

char*
gets(char *buf, int max)
{
    int i, cc;
    char c;
    
    for(i=0; i+1 < max; ){
        cc = read(0, &c, 1);
        if(cc < 1)
            break;
        buf[i++] = c;
        if(c == '\n' || c == '\r')
            break;
    }
    buf[i] = '\0';
    return buf;
}

int
stat(char *n, struct stat *st)
{
    int fd;
    int r;
    
    fd = open(n, O_RDONLY);
    if(fd < 0)
        return -1;
    r = fstat(fd, st);
    close(fd);
    return r;
}

int
atoi(const char *s)
{
    int n;
    
    n = 0;
    while('0' <= *s && *s <= '9')
        n = n*10 + *s++ - '0';
    return n;
}

void*
memmove(void *vdst, void *vsrc, int n)
{
    char *dst, *src;
    
    dst = vdst;
    src = vsrc;
    while(n-- > 0)
        *dst++ = *src++;
    return vdst;
}


// Atomic exchange - swap *addr with newval, return old *addr
static inline uint
xchg(volatile uint *addr, uint newval)
{
    uint result;
    uint tmp;
    
    // ARMv6 atomic exchange using ldrex/strex
    asm volatile(
        "1: ldrex %0, [%2]\n"           // Load exclusive from addr into result
        "   strex %1, %3, [%2]\n"       // Try to store newval to addr
        "   cmp %1, #0\n"               // Check if store succeeded
        "   bne 1b\n"                   // If failed (tmp != 0), retry
        : "=&r" (result), "=&r" (tmp)
        : "r" (addr), "r" (newval)
        : "cc", "memory"
    );
    
    return result;
}

// Lock implementation
void
initiateLock(struct lock* l)
{
    l->lockvar = 0;
}

void
acquireLock(struct lock* l)
{
    // Spin until we acquire the lock
    while(xchg(&l->lockvar, 1) != 0)
        ;
}

void
releaseLock(struct lock* l)
{
    // Release the lock
    xchg(&l->lockvar, 0);
}

// Condition variable implementation
void
initiateCondVar(struct condvar* cv)
{
    cv->var = getChannel();
    cv->initialized = 1;
}

void
condWait(struct condvar* cv, struct lock* l)
{
    if(!cv->initialized)
        return;
    
    // Release lock and sleep
    releaseLock(l);
    sleepChan(cv->var);
    // Reacquire lock after waking up
    acquireLock(l);
}

void
broadcast(struct condvar* cv)
{
    if(!cv->initialized)
        return;
    
    sigChan(cv->var);
}

void
signal(struct condvar* cv)
{
    if(!cv->initialized)
        return;
    
    sigOneChan(cv->var);
}

void semInit(struct semaphore* s, int initVal) {
    s->ctr = initVal;
    initiateLock(&s->l);
    initiateCondVar(&s->cv);
    s->isInitiated = 1;
}

void semUp(struct semaphore* s) {
    if (!s->isInitiated) {
        printf(1, "Error: semaphore not initialized\n");
        return;
    }

    acquireLock(&s->l);
    s->ctr++;

    // Wake up exactly one waiting thread
    signal(&s->cv);

    releaseLock(&s->l);    
}

void semDown(struct semaphore* s) {
    if (!s->isInitiated) {
        printf(1, "Error: semaphore not initialized\n");
        return;
    }

    acquireLock(&s->l);
    s->ctr--;

    if (s->ctr < 0) {
        // Wait until another process signals
        condWait(&s->cv, &s->l);
    }

    releaseLock(&s->l);
}