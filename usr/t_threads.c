#include "types.h"
#include "user.h"

void* thread1(void* arg){
    printf(1, "Thread 1 created %d\n", *(int*)arg);
    int* argPtr = (int*)arg;
    *argPtr = (*argPtr + 1);
    thread_exit(0);
    return 0;
}

void* thread2(void* arg){
    printf(1, "Thread 2 created %d\n", *(int*)arg);
    thread_exit(0);
    return 0;
}

int main(){
    printf(1, "Hello World\n");
    int x = 10;
    uint tid1, tid2;
    thread_create(&tid1, thread1, (void*)&x);
    thread_join(tid1);
    thread_create(&tid2, thread2, (void*)&x);
    thread_join(tid2);
    
    printf(1,"Value of x = %d\n",x);
    exit();
}
