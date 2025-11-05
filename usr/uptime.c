#include "user.h"
#include "param.h"

int main(void) {
    int ticks = uptime();
    int uptime_in_sec = ticks/HZ; // 1 tick = 100ms
    int milli_sec_remaining = ticks%HZ;
    printf(1,"Uptime: %d.%d secs\n", uptime_in_sec, milli_sec_remaining);
    exit();
}