#define _POSIX_C_SOURCE 199506L     // POSIX 1003.1c para hilos.
#include <unistd.h>                 // POSIX.
#include <stdlib.h>                 // Macros.
#include <stdio.h>                  // Input/output para sscanf y printf.
#include <time.h>                   // Temporizadores.
#include <string.h>                 // Para tratar los string.
#include <pthread.h>                // Hilos.
#include <signal.h>                 // Señales.
#include "problema.h"               // Cabecera dada por problema.

// Variables por línea de comando 
// (necesario que sean globales porque se usan en los hilos).
int tciclo;     // Nº de milisegundos por ciclo.
float vlimite;  // Máximo valor que pueden tener las señales físicas (nº señales/segundo = unidad de medida física).
int nmaxcic;    // Nº de ciclos máximo que podemos estar sin recibir señales de tipo SIGRTMIN+i.

// Otras variables globales.
int overflowedSignals = 0;  // Nº de señales que tienen valor mayor que vlimite.
int emptyCycle = 0;         // Nº de ciclos seguidos en los que no se ha recibido ninguna señal.

// Temporizador POSIX 1003.1b (declarado absoluto para recoger el tiempo absoluto).
timer_t tiempo;

// Flag de fin de programa (para que los hilos lleguen a su fin y se pueda hacer join).
int end = 0;

// Mutex y variables de condición ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
pthread_mutex_t mut_rebase      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mut_no_llegan   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  vc_rebase       = PTHREAD_COND_INITIALIZER;
pthread_cond_t  vc_no_llegan    = PTHREAD_COND_INITIALIZER;

// Manejador en el que nunca entra pero necesario para la definición.
void handle(int signo, siginfo_t *info, void *p);

// Definición de las funciones de los hilos.
void *signalCalc(void *p);
void *alarm(void *p);

////////////////////////////////////////////////////// MAIN //////////////////////////////////////////////////////
int main(int argc, char **_argv){
    // Guardar argumentos de la linea de comandos.
    sscanf(_argv[1], "%d", tciclo);    
    sscanf(_argv[2], "%f", vlimite);    
    sscanf(_argv[3], "%d", nmaxcic);    

    // Recepción de las señales (el manejador no se usa ya que usaremos sigwaitinfo).
    struct sigaction acc; 
    acc.sa_flags=SA_SIGINFO;    // Usando 1003.1b hay que usar este flag para asegurar funcionamiento.
    acc.sa_sigaction = handle;  // Manejador.
    sigemptyset(&acc.sa_mask);  // No se usa máscara.

    // Definición del conjunto de señales que se espera recibir.
    sigset_t expectedSignals;
    // Inicialización del conjuto.
    sigemptyset(&expectedSignals);

    for(int i=0; i<N_SIG; i++){
        sigaction(SIGRTMIN+i, &acc, NULL);          // Asociamos la acción (que es solo recibirlas) a las señales.
        sigaddset(&expectedSignals, SIGRTMIN+i);    // Añadimos al conjunto de señales las de tipo SIGRTMIN+i.
    }
    // Añadimos SIGALRM al conjunto.
    sigaddset(&expectedSignals, SIGALRM); 

    // Máscara utilizada para bloquear las señales hasta ahora añadidas a expectedSignals
    // para que solo las puedan usar en sigwaitinfo en los hilos correspondientes.
    pthread_sigmask(SIG_BLOCK, &expectedSignals, NULL);

    // Variables que reciben el identificador de los hilos.
    pthread_t sC;   //signalCalc.
    pthread_t alm;  // alarm.
    // Creamos los hilos del cálculo de las señales y de alarma.
    pthread_create(&sC, NULL, signalCalc, NULL);
    pthread_create(&alm, NULL, alarm, NULL);

    // Reutilizamos el conjunto de señales para procesar otras 
    // (ya que las anteriores han sido ya bloqueadas).
    sigemptyset(&expectedSignals);
    // Asociamos la acción de recepción a la señal SIGTERM.
    sigaction(SIGTERM, &acc, NULL);
    // Metemos SIGTERM en el conjunto.
    sigaddset(&expectedSignals, SIGTERM);

    // El programa principal espera hasta que recibe una señal de tipo SIGTERM 
    // (que se puede recibir como señal independiente o del hilo signalCalc 
    // si no se reciben señales en un tiempo determinado).
    siginfo_t inf; // Necesario para definir sigwaitinfo.
    sigwaitinfo(&expectedSignals, &inf);
    printf("Finalización de programa solicitada.\n");

    pthread_mutex_lock(&mut_no_llegan);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    // Condición para ver si SIGTERM ha llegado por superar 
    // el nº de ciclos seguidos sin llegar ninguna señal.
    if (emptyCycle == nmaxcic){ 
        printf("Finalización de programa porque han pasado %i ciclos sin recibirse señales.\n", nmaxcic);
    }
    pthread_mutex_unlock(&mut_no_llegan);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Finalizamos los hilos haciendo que salgan de sus respectivos 
    // bucles while activando el flag end.
    end=1;
    printf("Finalizando hilos.\n");

    // Para finalizar el hilo asociado a alarm enviamos
    // SIGRTMAX por si está esperando para continuar (en sigwaitinfo).
    kill(getpid(), SIGRTMAX); 
    // Para finalizar el hilo asociado a signalCalc forzamos la señal 
    // de fin de ciclo del temporizador (SIGALRM) y como no se están
    // recibiendo señales, el bucle terminará rápido.
    kill(getpid(), SIGALRM); 

    // Espera de la terminación de los hilos.
    pthread_join(&sC, NULL); 
    pthread_join(&alm, NULL); 

    printf("Hilos finalizados.\n");
    printf("Fin de programa.\n");

    return 0;
}

////////////////////////////////////// HILO CALCULO VALORES DE LAS SEÑALES //////////////////////////////////////
void *signalCalc(void *p){
    int receivedSignal;         // Valor de la señal que se recibirá a través de sigwaitinfo.
    int signalCount[N_SIG];     // Vector que contará cuantas señales de cada tipo han llegado en cada ciclo de reloj.
    float signalValue[N_SIG];   // Vector en el que se almacenará el cálculo del valor físico de cada una de las señales.
    int noSignal;               // Contador de tipo de señales de las que no se ha recibido nada en un ciclo de reloj.

    // Inicializamos valores a 0
    for(int i=0; i<N_SIG; i++){ 
        signalCount[i]=0; 
        signalValue[i]=0.0; 
    }

    // Variable necesaria para usar sigwaitinfo.
    siginfo_t in; 

    // Si los milisegundos que se nos dan equivalen a más de 1000 ms 
    // es necesario dividir el tiempo para el temporizador
    // (porque tiene variable en segundos y variable en nanosegundos).
    int seconds = 0;
    int millisec = tciclo;
    if (millisec>=1000){
        seconds = (int)(millisec/1000);
        millisec = millisec%1000;
    } // seconds*1000 + millisec = tciclo.
    

    // Especificaciones de tiempo del temporizador.
    struct timespec clock;
    clock.tv_sec = seconds;             // Segundos.
    clock.tv_nsec = millisec*1000000;   // Nanosegundos.
    
    // Comportamiento temporizador.
    struct itimerspec cycle;
    cycle.it_value=clock;       // Primer disparo a los tciclo milisegundos.
    cycle.it_interval=clock;    // Ciclo de tciclo milisegundos.

    // El vencimiento del temporizador (cuando ha pasado un ciclo) 
    // avisará con una señal SIGALRM.
    struct sigevent event;
    event.sigev_signo = SIGALRM;
    event.sigev_notify = SIGEV_SIGNAL;

    // Creación del temporizador con el evento programado.
    timer_create(CLOCK_REALTIME, &event, &tiempo);
    // Programación del temporizador con el comportamiento programado.
    timer_settime(tiempo, 0, &cycle, NULL);

    // Inicialización del conjunto de señales signalSet.
    sigset_t signalSet;
    sigemptyset(&signalSet); // Para inicializar.
    // Añadir las señales que se van a recibir de tipo SIGRTMIN+i.
    for(int i=0; i<N_SIG; i++){ 
        sigaddset(&signalSet, SIGRTMIN+i);
    }
    // Añadimos SIGALRM al conjunto de señales que podemos recibir 
    // (que representa el fin de un ciclo del temporizador).
    sigaddset(&signalSet, SIGALRM);

    while(!end){ // Mientras no sea el final del programa.
        // Guardamos el valor de la señal que recibe sigwaitinfo.
        receivedSignal=sigwaitinfo(&signalSet, &in);

        if(receivedSignal==SIGALRM){ // Si la señal que se recibe es un fin de ciclo de reloj.
            printf("Ciclo de reloj terminado\n");

            noSignal = 0;           // Inicializamos contador de señales no recibidas.
            overflowedSignals = 0;  // Inicializamos contador de señales que sobrepasan el valor vlimite.
            // MUTEX???????//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            
            // Se calcula el valor de las señales dependiendo de la frecuencia con la que hayan llegado.
            printf("Los valores de las distintas señales son:\n");
            for(int i=0; i<N_SIG; i++){ // Vamos a ver cada una de las señales SIGRTMIN+i por separado.
                if(signalCount[i]==0){ // Si no se han recibido señales de este tipo.
                    pthread_mutex_lock(&mut_no_llegan); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                    // Contamos que uno de los tipos de señales no ha llegado.
                    noSignal++;
                    if(noSignal==N_SIG){ // Esto quiere decir que no ha llegado ninguna señal en este ciclo de reloj.
                        emptyCycle++;
                        if(emptyCycle==nmaxcic){ // Si ha habido un número de ciclos vacíos igual al máximo permitido seguidos.
                            kill(getpid(),SIGTERM); // Se envía la señal SIGTERM para terminar el programa.
                        }
                    }
                    pthread_mutex_unlock(&mut_no_llegan); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                }
                else{ // Se han recibido señales del tipo que corresponde a i.

                    noSignal = 0; // Como se ha recibido una señal al menos, lo ponemos a 0 para evitar noSignal==N_SIG.
                    emptyCycle = 0; // Como se ha recibido una señal, ya no sigue habiendo ciclos vacios seguidos.
                    // HACE FALTA MUTEX?????////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                    // DECODIFICACIÓN: 
                    // El valor de la señal física es igual a la frecuencia con la que
                    // se ha recibido la señal del tipo SIGRTMIN+i en un ciclo de reloj
                    // (es decir, cuantas señales se han recibido por ciclo de reloj).
                    // El casteo es necesario ya que la mayoría de las variables son int.
                    // Se divide millisec/1000 porque lo queremos en segundos.
                    signalValue[i] = (float)signalCount[i]/((float)seconds+(float)millisec/1000.0);
                    printf("-> SIGRTMIN + %i = %f\n", i, signalValue[i]);

                    pthread_mutex_lock(&mut_rebase); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                    if(signalValue[i]>=vlimite){ // Si la señal iguala o sobrepasa el valor límite.
                        overflowedSignals++; // Contamos esa señal como que ha sobrepasado.

                        // Si el nº de señales que sobrepasan vlimite es mayor que la mitad
                        // del número de señales totales entraremos en este if.
                        // Para evitar problemas con las divisiones, en vez de escribir la condición 
                        // literalmente como la frase (overflowedSignals >= N_SIG/2)
                        // pasamos el 2 dividiendo al otro lado (multiplicando) y tenemos lo mismo.
                        if(overflowedSignals*2 >=N_SIG){
                            pthread_cond_signal(&mut_rebase); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                        }
                    }
                    pthread_mutex_unlock(&mut_rebase); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                }
            }
        }
        else{ // Si no es SIGALRM será una de las señales de tipo SIGRTMIN + i.
            // Cada posición del vector está asociada a una de las señales por lo que si 
            // restamos SIGRTMIN podemos ordenar las señales como 0, 1, ...
            // Este vector nos sirve para saber cuantas veces han llegado las señales de cada tipo
            // antes de que acabe un ciclo de reloj (sumando 1 cada vez que llega una de las señales).
            signalCount[receivedSignal-SIGRTMIN]++;
        }
    }
    
    return;
}

////////////////////////////////////////////////// HILO ALARMA //////////////////////////////////////////////////
void *alarm(void *p){ 
    // Creamos el conjunto de señales que contendrá 
    // la señal SIGRTMAX que es la señal que apaga el indicador.
    sigset_t turnOff;
    sigemptyset(&turnOff); // Para inicializar.
    sigaddset(&turnOff, SIGRTMAX);

    // Variable necesaria para sigwaitinfo.
    siginfo_t info;

    //Mientras no sea el fin del programa.
    while(end!= 1){
        pthread_mutex_lock(&mut_rebase);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Mientras el nº de señales que sobrepasan vlimite no sea mayor que la mitad
        // del número de señales totales seguiremos en este bucle.
        // Para evitar problemas con las divisiones, en vez de escribir la condición 
        // literalmente como la frase (overflowedSignals < N_SIG/2)
        // pasamos el 2 dividiendo al otro lado (multiplicando) y tenemos lo mismo.
        while(overflowedSignals*2 < N_SIG){
            pthread_cond_wait(&vc_rebase, &mut_rebase); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        }

        // Como se ha cumplido la condición encendemos el indicador.
        indicador(1);

        pthread_mutex_unlock(&mut_rebase);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Esperamos que llegue la señal SIGRTMAX.
        sigwaitinfo(&turnOff, &info);

        // Una vez llega la señal podemos apagar el indicador.
        indicador(0);
    }

    return;
}