#include "types.h"
#include "user.h"
#include "memlayout.h"
#include "mmu.h"
#include "arm.h"

#define N (8 * (1 << 20))

char *testname = "";

// Function prototypes
void print_pt(void);
void print_kpt(void);
void ugetpid_test(void);
void superpg_test(void);
void err(char *why);
void print_pte(uint va);
void supercheck(uint s);

int printpte(uint);
int pgpte(void *);
int kpt(void);
int ugetpid(void);

int main(void)
{
    print_pt();
    ugetpid_test();
    print_kpt();
    superpg_test();
    printf(1, "pttest: all tests succeeded\n");
    exit();
}

void err(char *why)
{
    printf(1, "pttest: %s failed: %s, pid=%d\n", testname, why, getpid());
    exit();
}

void print_pte(uint va)
{
    // This will be handled by system call
    printpte(va);
}

void print_pt(void)
{
    int i;
    printf(1, "print_pt starting\n");
    for (i = 0; i < 10; i++)
    {
        print_pte(i * PTE_SZ);
    }
    uint top = KERNBASE / PTE_SZ;
    for (i = top - 10; i < top; i++)
    {
        print_pte(i * PTE_SZ);
    }
    printf(1, "print_pt: OK\n");
}

void ugetpid_test(void)
{
    int i, ret;
    printf(1, "ugetpid_test starting\n");
    testname = "ugetpid_test";

    for (i = 0; i < 64; i++)
    {
        ret = fork();
        if (ret != 0)
        {
            wait();
            continue;
        }
        if (getpid() != ugetpid())
            err("mismatched PID");
        exit();
    }
    printf(1, "ugetpid_test: OK\n");
}

void print_kpt(void)
{
    printf(1, "print_kpt starting\n");
    kpt(); // System call to print kernel page table
    printf(1, "print_kpt: OK\n");
}

void supercheck(uint s)
{
    // pte_t last_pte = 0;
    uint p;

    for (p = s; p < s + 512 * PTE_SZ; p += PTE_SZ)
    {
        pte_t pte = (pte_t)pgpte((void *)p); // Need to implement pgpte
        if (pte == 0)
            err("no pte");
        if ((pte & PTE_TYPE) == 0)
            err("pte wrong");
        // last_pte = pte;
    }

    for (p = 0; p < 512; p += PTE_SZ)
    {
        *(int *)(s + p) = p;
    }

    for (p = 0; p < 512; p += PTE_SZ)
    {
        if (*(int *)(s + p) != p)
            err("wrong value");
    }
}

void superpg_test(void)
{
    int pid;
    printf(1, "superpg_test starting\n");
    testname = "superpg_test";

    char *end = sbrk(N);
    if (end == 0)
        err("sbrk failed");

    uint s = ((uint)end + PTE_SZ - 1) & ~(PTE_SZ - 1);
    supercheck(s);

    if ((pid = fork()) < 0)
    {
        err("fork");
    }
    else if (pid == 0)
    {
        supercheck(s);
        exit();
    }
    else
    {
        wait();
    }
    printf(1, "superpg_test: OK\n");
}