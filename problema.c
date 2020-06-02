#define _POSIX_C_SOURCE 199506L // POSIX 1003.1c para hilos
#include <unistd.h> // POSIX
#include <stdlib.h> // Macros
#include <stdio.h> // input/output para sscanf
#include <time.h> // Temporizadores
#include <string.h> // Para tratar los string
#include <pthread.h> // Hilos
#include <signal.h> // Señales
#include "problema.h" // Cabecera dada por problema

// Variables por línea de comando
int tciclo; // nº de milisegundos 
float vlimite; // representa la unidad de medida de las señales físicas (nº señales/segundo)
int nmaxcic; //nº de ciclos

// Variables compartidas
int rebaso; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int no_llegan; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Temporizador POSIX 1003.1b (declarado absoluto para recoger el tiempo absoluto)
timer_t tiempo;

// Flag de fin de programa (para que los hilos lleguen a su fin y se pueda hacer join)
int end = 0;

// Mutex y variables de condición ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
pthread_mutex_t mut_rebase      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mut_no_llegan   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  vc_rebase       = PTHREAD_COND_INITIALIZER;
pthread_cond_t  vc_no_llegan    = PTHREAD_COND_INITIALIZER;

// Manejador en el que nunca entra pero necesario para la definición
void handle(int signo, siginfo_t *info, void *p);

// Inicialización de las funciones de los hilos
void *signalCalc(void *p);
void *alarm(void *p);

////////////////////////////////////////////////////// MAIN //////////////////////////////////////////////////////
int main(int argc, char **_argv){
    // Guardar argumentos de la linea de comandos
    sscanf(_argv[1], "%d", tciclo); // nº de milisegundos 
    sscanf(_argv[2], "%f", vlimite); // representa la unidad de medida de las señales físicas (nº señales/segundo)
    sscanf(_argv[3], "%d", nmaxcic); //nº de ciclos
    
    siginfo_t inf;

    // Recepción de las señales: el manejador no se usa ya que usaremos sigwaitinfo
    struct sigaction acc; 
    acc.sa_flags=SA_SIGINFO; // usando 1003.1b hay que usar este flag para asegurar funcionamiento
    acc.sa_sigaction = handle; //manejador
    sigemptyset(&acc.sa_mask); // No se usa máscara

    // Definición del conjunto de señales que se espera recibir
    sigset_t expectedSignals;
    // Inicialización del conjuto
    sigemptyset(&expectedSignals);

    for(int i=0; i<N_SIG; i++){
        sigaction(SIGRTMIN+i, &acc, NULL); // Asociamos la acción (que es solo recibirlas) a las señales
        sigaddset(&expectedSignals, SIGRTMIN+i); // Creamos el conjunto de señales y añadimos las de tipo SIGRTMIN+i
    }
    // Añadimos SIGALRM al conjunto
    sigaddset(&expectedSignals, SIGALRM); 

    // Máscara utilizada para bloquear las señales hasta ahora añadidas a expectedSignals
    // para que solo las puedan usar en sigwaitinfo en los hilos correspondientes
    pthread_sigmask(SIG_BLOCK, &expectedSignals, NULL);

    // Variables que reciben el identificador de los hilos
    pthread_t cnt;
    pthread_t alm;
    // Creamos los hilos del cálculo de las señales y de alarma
    pthread_create(&cnt, NULL, signalCalc, NULL);
    pthread_create(&alm, NULL, alarm, NULL);

    // Reutilizamos el conjunto de señales para procesar otras 
    // (ya que las anteriores han sido ya bloqueadas)
    sigemptyset(&expectedSignals);

    // Recepción de la señal SIGTERM 
    sigaction(SIGTERM, &acc, NULL);
    // Metemos SIGTERM en el conjunto
    sigaddset(&expectedSignals, SIGTERM);

    // El programa principal espera hasta que recibe una señal de tipo SIGTERM 
    // (que se puede recibir como señal independiente o del hilo signalCalc 
    // si no se reciben señales en un tiempo determinado)
    // Lo que queda de código no es más que la finalización del programa
    sigwaitinfo(&expectedSignals, &inf);


    pthread_mutex_lock(&mut_no_llegan);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (no_llegan == N_SIG){////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        printf("main => recibido SIGTERM de cnt_sig al no llegar señales\n");////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    }
    pthread_mutex_unlock(&mut_no_llegan);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    printf("main => voy a acabar(?)\n");////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    // Finalizamos los hilos haciendo que salgan de sus respectivos bucles while
    end=1;

    kill(getpid(), SIGRTMAX); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    kill(getpid(), SIGALRM); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    pthread_join(&cnt, NULL); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    pthread_join(&alm, NULL); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    printf("main => terminado todo\n"); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    return 0;
}

////////////////////////////////////// HILO CALCULO VALORES DE LAS SEÑALES //////////////////////////////////////
void *signalCalc(void *p){
    int sens_recib[N_SIG]; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    float magnitudes[N_SIG]; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    int cont_ceros; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    int k; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    siginfo_t in; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Reloj de referencia
    struct timespec clock;
    clock.tv_sec=0; // 0 segundos
    clock.tv_nsec=tciclo*1e6; // tciclo milisegundos en nanosegundos
    
    // Temporizador
    struct itimerspec cycle;
    cycle.it_value=clock; // Primer disparo a los tciclo milisegundos
    cycle.it_interval=clock; // Ciclo de tciclo milisegundos

    // El vencimiento del temporizador (cuando ha pasado un ciclo) avisará con una señal SIGALRM
    struct sigevent event;
    event.sigev_signo = SIGALRM;
    event.sigev_notify = SIGEV_SIGNAL;

    // Creación del temporizador
    timer_create(CLOCK_REALTIME, &event, &tiempo);
    // Programación del temporizador
    timer_settime(tiempo, 0, &cycle, NULL); // Sin tiempo absoluto

    // Inicialización del conjunto de señales signalSet
    sigset_t signalSet;
    sigemptyset(&signalSet); // Para inicializar
    // Añadir las señales que se van a recibir de tipo SIGRTMIN+i
    for(int i=0; i<N_SIG; i++){ 
        sigaddset(&signalSet, SIGRTMIN+i);
    }
    // Añadimos SIGALRM al conjunto de señales que podemos recibir 
    // (que representa el fin de un ciclo del temporizador)
    sigaddset(&signalSet, SIGALRM);

    // Inicializamos valores a 0
    for(int i=0; i<N_SIG; i++){ ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        sens_recib[i]=0; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        magnitudes[i]=0.0; ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    }
    
    while(!end){ //TODO EL WHILE //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        k=sigwaitinfo(&signalSet, &in);
        if(k==SIGALRM){
            for(int i=0; i<N_SIG; i++){
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
                    magnitudes[i] = (float)sens_recib[i]/(clock.tv_nsec/1e6); //ponía tv.nsec(???)
                    printf("hilo => recibidas %f sen/ticc para SIGRTMIN+%d\n", magnitudes[i],k);
                    pthread_mutex_lock(&mut_rebase);
                    if(magnitudes[i]>vlimite){
                        rebaso++;
                        if(rebaso*2 >=N_SIG){
                            pthread_cond_signal(&mut_rebase);
                            pthread_mutex_unlock(&mut_rebase);
                        }
                    }
                }
            }
        }
        else{ // Si no es SIGALRM
            sens_recib[k-SIGRTMIN]++;
        }
    }
    printf("hilo => cont_sig acabando\n"); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    return NULL;
}

////////////////////////////////////////////////// HILO ALARMA //////////////////////////////////////////////////
void *alarm(void *p){ // TODO EL HILO //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    sigset_t max;
    siginfo_t info;
    sigemptyset(&max);
    sigaddset(&max, SIGRTMAX);
    while(end!= 1){
        pthread_mutex_lock(&mut_rebase);
        while(rebaso*2<N_SIG){
            pthread_cond_wait(&vc_rebase, &mut_rebase);
        }
        indicador(1);
        pthread_mutex_unlock(&mut_rebase);
        sigwaitinfo(&max, &info);
        indicador(0);
    }
    return NULL;
}