struct uproc {
    int pid;
    int ppid;
    char name[16];
    char state[16];
    int syscalls;
    int tickets;
    int boostsleft;
};