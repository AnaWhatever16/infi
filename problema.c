#define _POSIX_C_SOURCE 199506L
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "problema.h"

// Variables Compartidas
int rebaso;
int no_llegan;
int tcic;
int nmaxcic;
float vlimite;

// Temporizador POSIX 1003.1b
timer_t tiempo;

// Flag de fin
int fin = 0;

// Mutex y variables de condición
pthread_mutex_t mut_rebase      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mut_no_llegan   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  vc_rebase       = PTHREAD_COND_INITIALIZER;
pthread_cond_t  vc_no_llegan    = PTHREAD_COND_INITIALIZER;

// Manejador que no hace nada
void man(int signo, siginfo_t *info, void *p);

// Funciones de arranque de los hilos
void *cnt_sig(void *p);
void *alarma(void *p);

// Main
int main(int argc, char **argv){
    // Guardar argumentos de la linea de comandos
    sscanf(argv[1], "%d", tcic); // No se usarán más por(?)
    sscanf(argv[2], "%f", vlimite); // Main a partir de aquí
    sscanf(argv[3], "%d", nmaxcic); // No regula(?) mutex

    struct sigaction ac; 
    int i;
    sigset_t sens;
    siginfo_t inf;
    pthread_t cnt;
    pthread_t alm;

    sigemptyset(&ac.sa_mask);
    ac.sa_flags=SA_SIGINFO;
    sigemptyset(&sens);
    ac.sa_sigaction = man;

    for(i=0; i<N_SIG; i++){
        sigaction(SIGRTMIN+i, &sens, NULL);
        sigaddset(&sens, SIGRTMIN+i);
    }

    sigaddset(&sens, SIGALRM); // El indicador de tcic
    pthread_sigmask(SIG_BLOCK, &sens, NULL);

    pthread_create(&cnt, NULL, cnt_sig, NULL);
    pthread_create(&alm, NULL, alarma, NULL);

    sigemptyset(&sens);
    sigaction(SIGTERM, &sens, NULL);
    sigaddset(&sens, SIGTERM);


    sigwaitinfo(&sens, &inf);
    pthread_mutex_lock(&mut_no_llegan);
    if (no_llegan == N_SIG){
        printf("main => recibido SIGTERM de de cnt_sig al no llegar señales\n");
    }
    pthread_mutex_unlock(&mut_no_llegan);

    printf("main => voy a acabar(?)\n");
    fin=1;

    kill(getpid(), SIGRTMAX);
    kill(getpid(), SIGALRM);

    pthread_join(&cnt, NULL);
    pthread_join(&alm, NULL);

    printf("main => terminado todo\n");

    return 0;
}

// Hilo cnt_sig
void *cnt_sig(void *p){
    int sens_recib[N_SIG];
    float magnitudes[N_SIG];
    int i; 
    int cont_ceros;
    int k;
    sigset_t set;
    siginfo_t in;
    struct timespec t;
    struct itimerspec ciclo;

    for(i=0; i<N_SIG; i++){
        sens_recib[i]=0;
        magnitudes[i]=0.0;
    }

    struct sigevent ev;
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIGALRM;

    timer_create(CLOCK_REALTIME, &ev, &tiempo);
    t.tv_sec=0;
    t.tv_nsec=tcic;

    ciclo.it_value=t;
    ciclo.it_interval=ciclo;

    timer_settime(tiempo, 0, &ciclo, NULL);
    sigemptyset(&set);

    for(i=0; i<N_SIG; i++){
        sigaddset(&set, SIGRTMIN+1);
    }
    
    while(!fin){
        k=sigwaitinfo(&set, &in);
        if(k==SIGALRM){
            for(i=0; i<N_SIG; i++){
                if(sens_recib[i]==0){
                    pthread_mutex_lock(&mut_no_llegan);
                    cont_ceros++;
                    if(cont_ceros==N_SIG){
                        no_llegan++;
                        if(no_llegan==nmaxcic){
                            kill(getpid(),SIGTERM);
                        }
                    }
                    pthread_mutex_unlock(&mut_no_llegan);
                }
                else{
                    magnitudes[i] = (float)sens_recib[i]/(t.tv_nsec/1e6); //ponía tv.nsec(???)
                }
            }
        }
    }
}