#include <stdio.h> // Input/output para printf.
#include <signal.h>                 // Señales.

#include "signalMessagesExample.h"

void ejemplo1(){
    printf("Ejemplo 1.\n");
    kill(getpid(), SIGRTMIN+1);
    kill(getpid(), SIGRTMIN);
}
void ejemplo2(){

}
void ejemplo3(){

}