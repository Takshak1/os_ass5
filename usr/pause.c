#include "user.h"
#include "param.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf(1,"Usage: pause <seconds>\n");
        exit();
    }

    int seconds = atoi(argv[1]);
    if (seconds <= 0) {
        printf(1,"Pause time should be integer and more than 0\n");
        exit();
    }

    sleep(seconds * HZ);  // 1 tick = 100ms
    exit();
}
