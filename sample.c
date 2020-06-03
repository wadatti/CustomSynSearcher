#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

int i = 1;

void func1(void){
    while(i==1){
    }
    printf("synchronized!!\n");
}

void func2(void){
    sleep(2);
    i = 0;
    printf("i=0;\n");
}

int main(void){
    pthread_t thread1, thread2;
    int ret1, ret2;
    ret1 = pthread_create(&thread1, NULL, (void *)func1, NULL);
    ret2 = pthread_create(&thread2, NULL, (void *)func2, NULL);


    ret1 = pthread_join(thread1,NULL);
    ret2 = pthread_join(thread2,NULL);
    
    return 0;
}