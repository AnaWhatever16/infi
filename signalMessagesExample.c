#include <stdio.h> // Input/output para printf.
#include <signal.h>                 // Señales.

#include "signalMessagesExample.h"

void testExample1(){
    kill(getpid(), SIGRTMIN+1);
    kill(getpid(), SIGRTMIN);
    kill(getpid(), SIGRTMIN+2);
    sleep(1);
    kill(getpid(), SIGRTMIN+1);
    kill(getpid(), SIGRTMAX);
}
void testExample2(){
    kill(getpid(), SIGRTMIN);
    usleep(500000);

}
void testExample3(){

}