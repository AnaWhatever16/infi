#include <stdio.h> // Input/output para printf.
#include <signal.h>                 // Señales.

#include "signalMessagesExample.h"

void testExample1(){
    kill(getpid(), SIGRTMIN+1);
    kill(getpid(), SIGRTMIN);
    sleep(1);
    kill(getpid(), SIGRTMIN+1);
    kill(getpid(), SIGRTMAX);
}
void testExample2(){

}
void testExample3(){

}