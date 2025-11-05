#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define BSIZE 512
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))

int
main(int argc, char *argv[])
{
    int fd;
    char buf[BSIZE];
    int i;

    printf(1, "Starting bmap double-indirect test...\n");

    // Create a test file
    fd = open("bmapfile", O_CREATE | O_RDWR);
    if(fd < 0){
        printf(1, "Failed to create file\n");
        exit();
    }

    // Total blocks to trigger double-indirect allocation
    int total_blocks = NDIRECT + NINDIRECT + 5;  // 11 + 128 + 5 = 144 blocks

    for(i = 0; i < total_blocks; i++){
        // Fill buffer with test pattern
        for(int j = 0; j < BSIZE; j++)
            buf[j] = i % 256;

        // Write buffer to file
        if(write(fd, buf, BSIZE) != BSIZE){
            printf(1, "Write failed at block %d\n", i);
            break;
        }

        printf(1, "Block %d written\n", i);
    }

    close(fd);
    printf(1, "bmap double-indirect test finished.\n");
    printf(1, "File size: %d bytes (%d blocks)\n", total_blocks * BSIZE, total_blocks);

    exit();
}

