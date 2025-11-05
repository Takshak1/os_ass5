#include "types.h"
#include "stat.h"
#include "user.h"

struct lock l;
struct condvar cv;
int turn = 1;

void*
toggle1(void* arg)
{
    int i;
    for(i = 0; i < 10; i++){
        acquireLock(&l);
        while(turn != 1){
            condWait(&cv, &l);
        }
        printf(1, "I am thread 1\n");
        turn = 2;
        signal(&cv);
        releaseLock(&l);
    }
    thread_exit();
    return 0;
}

void*
toggle2(void* arg)
{
    int i;
    for(i = 0; i < 10; i++){
        acquireLock(&l);
        while(turn != 2){
            condWait(&cv, &l);
        }
        printf(1, "I am thread 2\n");
        turn = 1;
        signal(&cv);
        releaseLock(&l);
    }
    thread_exit();
    return 0;
}

int
main()
{
    uint tid1, tid2;
    
    initiateLock(&l);
    initiateCondVar(&cv);
    
    thread_create(&tid1, toggle1, 0);
    thread_create(&tid2, toggle2, 0);
    
    thread_join(tid1);
    thread_join(tid2);
    
    exit();
    return 0;  
}