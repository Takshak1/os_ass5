// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

static char *commands[] = {
    "cat",
    "echo",
    "grep",
    "init",
    "kill",
    "ln",
    "ls",
    "mkdir",
    "rm",
    "sh",
    "stressfs",
    "usertests",
    "wc",
    "zombie",
    "uptime",
    "pause",
    "ps",
    "getpinfo",
    "test",
    "t_threads",
    "t_barrier",
    "t_lock",
    "t_sleepwake",
    "t_l_cv1",
    "t_l_cv2",
    "t_sem1",
    "t_sem2",
    0
};

static void consputc(int);

static int panicked = 0;

static struct
{
    struct spinlock lock;
    int locking;
} cons;

static void printint(int xx, int base, int sign)
{
    static char digits[] = "0123456789abcdef";
    char buf[16];
    int i;
    uint x;

    if (sign && (sign = xx < 0))
    {
        x = -xx;
    }
    else
    {
        x = xx;
    }

    i = 0;

    do
    {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
    {
        buf[i++] = '-';
    }

    while (--i >= 0)
    {
        consputc(buf[i]);
    }
}
// PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...)
{
    int i, c, locking;
    uint *argp;
    char *s;

    locking = cons.locking;

    if (locking)
    {
        acquire(&cons.lock);
    }

    if (fmt == 0)
    {
        panic("null fmt");
    }

    argp = (uint *)(void *)(&fmt + 1);

    for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
    {
        if (c != '%')
        {
            consputc(c);
            continue;
        }

        c = fmt[++i] & 0xff;

        if (c == 0)
        {
            break;
        }

        switch (c)
        {
        case 'd':
            printint(*argp++, 10, 1);
            break;

        case 'x':
        case 'p':
            printint(*argp++, 16, 0);
            break;

        case 's':
            if ((s = (char *)*argp++) == 0)
            {
                s = "(null)";
            }

            for (; *s; s++)
            {
                consputc(*s);
            }
            break;

        case '%':
            consputc('%');
            break;

        default:
            // Print unknown % sequence to draw attention.
            consputc('%');
            consputc(c);
            break;
        }
    }

    if (locking)
    {
        release(&cons.lock);
    }
}

void panic(char *s)
{
    cli();

    cons.locking = 0;

    cprintf("cpu%d: panic: ", cpu->id);

    show_callstk(s);
    panicked = 1; // freeze other CPU

    while (1)
        ;
}

// PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4

void consputc(int c)
{
    if (panicked)
    {
        cli();
        while (1)
            ;
    }

    if (c == BACKSPACE)
    {
        uartputc('\b');
        uartputc(' ');
        uartputc('\b');
    }
    else
    {
        uartputc(c);
    }

    // cgaputc(c);
}

#define INPUT_BUF 512
struct
{
    struct spinlock lock;
    char buf[INPUT_BUF];
    uint r; // Read index
    uint w; // Write index
    uint e; // Edit index
} input;

#define C(x) ((x) - '@') // Control-x

void consoleintr(int (*getc)(void))
{
    int c;

    acquire(&input.lock);

    while ((c = getc()) >= 0)
    {
        switch (c)
        {
        case C('P'):
            procdump();
            break;

        case C('U'):
            while ((input.e != input.w) && (input.buf[(input.e - 1) % INPUT_BUF] != '\n'))
            {
                input.e--;
                consputc(BACKSPACE);
            }
            break;

        case C('H'):
        case '\x7f':
            if (input.e != input.w)
            {
                input.e--;
                consputc(BACKSPACE);
            }
            break;

        case '\t':
        {
            char buf[INPUT_BUF];
            int len;
            len = input.e - input.w;
            if (len < 0)
            {
                len = 0;
            }
            int i = 0;
            while (i < len)
            {
                int index;
                index = (input.w + i) % INPUT_BUF;
                buf[i] = input.buf[index];
                i = i + 1;
            }
            buf[len] = '\0';
            int multiple = 0;
            len = 0;
            char *match;
            i = 0;
            int cmp;

            len = strlen(buf);
            match = 0;
            multiple = 0;

            while (commands[i] != 0)
            {
                cmp = strncmp(commands[i], buf, len);
                if (cmp == 0)
                {
                    if (match == 0)
                    {
                        match = commands[i];
                    }
                    else
                    {
                        multiple = 1;
                    }
                }
                i = i + 1;
            }
            char *cmd = match;
            if (cmd && !multiple)
            {
                int i = len;
                while (cmd[i])
                {
                    consputc(cmd[i]);
                    int index;
                    index = input.e % INPUT_BUF;
                    input.buf[index] = cmd[i];
                    input.e = input.e + 1;
                    i++;
                }
            }
            else if (multiple)
            {
                cprintf("\n");
                int i = 0;
                while (commands[i])
                {
                    if (strncmp(commands[i], buf, len) == 0)
                    {
                        cprintf("%s ", commands[i]);
                    }
                    i++;
                }
                cprintf("\n%s", buf);
            }
            break;
        }

        default:
            if ((c != 0) && (input.e - input.r < INPUT_BUF))
            {
                c = (c == '\r') ? '\n' : c;

                input.buf[input.e++ % INPUT_BUF] = c;
                consputc(c);

                if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF)
                {
                    input.w = input.e;
                    wakeup(&input.r);
                }
            }
            break;
        }
    }

    release(&input.lock);
}

int consoleread(struct inode *ip, char *dst, int n)
{
    uint target;
    int c;

    iunlock(ip);

    target = n;
    acquire(&input.lock);

    while (n > 0)
    {
        while (input.r == input.w)
        {
            if (proc->killed)
            {
                release(&input.lock);
                ilock(ip);
                return -1;
            }

            sleep(&input.r, &input.lock);
        }

        c = input.buf[input.r++ % INPUT_BUF];

        if (c == C('D'))
        { // EOF
            if (n < target)
            {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                input.r--;
            }

            break;
        }

        *dst++ = c;
        --n;

        if (c == '\n')
        {
            break;
        }
    }

    release(&input.lock);
    ilock(ip);

    return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
    int i;

    iunlock(ip);

    acquire(&cons.lock);

    for (i = 0; i < n; i++)
    {
        consputc(buf[i] & 0xff);
    }

    release(&cons.lock);

    ilock(ip);

    return n;
}

void consoleinit(void)
{
    initlock(&cons.lock, "console");
    initlock(&input.lock, "input");

    devsw[CONSOLE].write = consolewrite;
    devsw[CONSOLE].read = consoleread;

    cons.locking = 1;
}