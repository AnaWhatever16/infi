#define _POSIX_C_SOURCE 199506L     // POSIX 1003.1c para hilos.
#include <unistd.h>                 // POSIX.
#include <stdlib.h>                 // Macros.
#include <stdio.h>                  // Input/output para sscanf y printf.
#include <time.h>                   // Temporizadores.
#include <string.h>                 // Para tratar los string.
#include <pthread.h>                // Hilos.
#include <signal.h>                 // Señales.

#include "problema.h"               // Cabecera dada por problema.
#include "signalMessagesExample.h"  // Cabecera de ejemplos de envíos de señales

// Variables por línea de comando 
// (necesario que sean globales porque se usan en los hilos
// pero no cambiarán de valor durante la ejecución por lo que
// no harán falta mutex para su acceso).
int tciclo;     // Nº de milisegundos por ciclo.
float vlimite;  // Máximo valor que pueden tener las señales físicas (nº señales/segundo = unidad de medida física).
int nmaxcic;    // Nº de ciclos máximo que podemos estar sin recibir señales de tipo SIGRTMIN+i.
int example;    // Ejemplo que se desea utilizar

// Otra variable compartida que si requerirá mutex porque 
// variará su valor durante la ejecución del programa
int overflowedSignals = 0;  // Nº de señales que tienen valor mayor que vlimite.

// Temporizador POSIX 1003.1b (declarado absoluto para recoger el tiempo absoluto).
timer_t timerDef;

// Flag de fin de programa (para que los hilos lleguen a su fin y se pueda hacer join).
int end = 0;

// Mutex y variables de condición asociadas a la variable overflowedSignals.
pthread_mutex_t mut_overflowedSignals       = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cv_overflowedSignals        = PTHREAD_COND_INITIALIZER;

// Manejador en el que nunca entra pero necesario para la definición.
void handeling(int signo, siginfo_t *info, void *p){}

// Definición de las funciones de los hilos.
void *signalCalc(void *p);
void *alarmManage(void *p);
void *signalExamples(void *p);

////////////////////////////////////////////////////// MAIN //////////////////////////////////////////////////////
int main(int argc, char **_argv){

    // Guardar argumentos de la linea de comandos.
    printf("Variables utilizadas:\n");
    sscanf(_argv[1], "%d", &tciclo);    printf("tciclo = %d\n", tciclo);       
    sscanf(_argv[2], "%f", &vlimite);   printf("vlimite = %f\n", vlimite); 
    sscanf(_argv[3], "%d", &nmaxcic);   printf("nmaxcic = %d\n", nmaxcic);

    // Escoger ejemplo por línea de comandos.
    printf("Indique cual de los %d ejemplos desea utilizar: ", N_EXAMPLES);
    scanf("%d", &example);
    printf("----------------------------------------------------------\n");

    // Recepción de las señales (el manejador no se usa ya que usaremos sigwaitinfo).
    struct sigaction act; 
    act.sa_flags=SA_SIGINFO;    // Usando 1003.1b hay que usar este flag para asegurar funcionamiento.
    act.sa_sigaction = handeling;  // Manejador.
    sigemptyset(&act.sa_mask);  // No se usa máscara.

    // Definición del conjunto de señales que se espera recibir.
    sigset_t expectedSignals;
    // Inicialización del conjuto.
    sigemptyset(&expectedSignals);

    for(int i=0; i<N_SIG; i++){
        sigaction(SIGRTMIN+i, &act, NULL);          // Asociamos la acción (que es solo recibirlas) a las señales.
        sigaddset(&expectedSignals, SIGRTMIN+i);    // Añadimos al conjunto de señales las de tipo SIGRTMIN+i.
    }
    // Añadimos SIGRTMAX y SIGALRM al conjunto.
    sigaddset(&expectedSignals, SIGRTMAX); 
    sigaddset(&expectedSignals, SIGALRM);

    // Máscara utilizada para bloquear las señales hasta ahora añadidas a expectedSignals
    // para que solo las puedan usar en sigwaitinfo en los hilos correspondientes
    // (si no se hiciese esto el programa no funciona correctamente).
    pthread_sigmask(SIG_BLOCK, &expectedSignals, NULL);

    // Variables que reciben el identificador de los hilos.
    pthread_t sC;   //signalCalc.
    pthread_t aM;   // alarmManage.
    pthread_t sE;   //signalExamples
    // Creamos los hilos del cálculo de las señales, de alarma y el de ejecución de los ejemplos.
    pthread_create(&sC, NULL, signalCalc, NULL);
    pthread_create(&aM, NULL, alarmManage, NULL);
    pthread_create(&sE, NULL, signalExamples,NULL);

    // Reutilizamos el conjunto de señales para procesar otras 
    // (ya que las anteriores han sido ya bloqueadas).
    sigemptyset(&expectedSignals);
    // Asociamos la acción de recepción a la señal SIGTERM.
    sigaction(SIGTERM, &act, NULL);
    // Metemos SIGTERM en el conjunto.
    sigaddset(&expectedSignals, SIGTERM);
    // Esta señal no la bloqueamos porque no la recibirán los hilos, sino el 
    // programa principal.

    // El programa principal espera hasta que recibe una señal de tipo SIGTERM 
    // (que se puede recibir como señal independiente o del hilo signalCalc 
    // si no se reciben señales en un tiempo determinado).
    siginfo_t info; // Necesario para definir sigwaitinfo.
    sigwaitinfo(&expectedSignals, &info);
    printf("Finalización de programa solicitada.\n");

    // Finalizamos los hilos haciendo que salgan de sus respectivos 
    // bucles while activando el flag end.
    end=1;
    printf("Finalizando hilos.\n");
    
    // Para finalizar el hilo asociado a signalCalc forzamos la señal 
    // de fin de ciclo del temporizador (SIGALRM) (aunque si se está en medio 
    // de una decodificación primero terminará eso).
    kill(getpid(), SIGALRM); 
    
    // Para finalizar el hilo asociado a alarmManage enviamos
    // SIGRTMAX, para terminar la espera en sigwaitinfo,
    // y por tanto se apagará el indicador (si estaba apagada se encenderá
    // y luego se apagará).
    kill(getpid(), SIGRTMAX); 

    // Espera de la finalización de los hilos.
    pthread_join(sC, NULL); 
    pthread_join(aM, NULL); 
    pthread_join(sE, NULL);

    printf("Hilos finalizados.\n");
    printf("Fin de programa.\n");

    return 0;
}

////////////////////////////////////// HILO CALCULO VALORES DE LAS SEÑALES //////////////////////////////////////
void *signalCalc(void *p){
    int receivedSignal;         // Valor de la señal que se recibirá a través de sigwaitinfo.
    int signalCount[N_SIG];     // Vector que contará cuantas señales de cada tipo han llegado en cada ciclo de reloj.
    float signalValue[N_SIG];   // Vector en el que se almacenará el cálculo del valor físico de cada una de las señales.
    int noSignal = 0;           // Contador de tipo de señales de las que no se ha recibido nada en un ciclo de reloj.
    int emptyCycle = 0;         // Nº de ciclos seguidos en los que no se ha recibido ninguna señal.

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
    timer_create(CLOCK_REALTIME, &event, &timerDef);
    // Programación del temporizador con el comportamiento programado.
    timer_settime(timerDef, 0, &cycle, NULL);

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

            noSignal = 0; // Inicializamos contador de señales no recibidas.

            // overflowedSignals es una variable compartida con el hilo asociado a alarmManage por
            // lo que es necesario usar mutex para bloquear el uso simultáneo de esta variable. 
            pthread_mutex_lock(&mut_overflowedSignals);
            overflowedSignals = 0;  // Inicializamos contador de señales que sobrepasan el valor vlimite.
            // Liberamos mutex ya que hemos terminado de usar la variable.
            pthread_mutex_unlock(&mut_overflowedSignals);
        
            // Se calcula el valor de las señales dependiendo de la frecuencia con la que hayan llegado.
            printf("Los valores de las distintas señales son:\n");
            for(int i=0; i<N_SIG; i++){ // Vamos a ver cada una de las señales SIGRTMIN+i por separado.

                if(signalCount[i]==0){ // Si no se han recibido señales de este tipo.
                    // Contamos que uno de los tipos de señales no ha llegado.
                    noSignal++;

                    if(noSignal==N_SIG){ // Esto quiere decir que no ha llegado ninguna señal en este ciclo de reloj.
                        printf("No se ha recibido ninguna señal.\n");
                        emptyCycle++;

                        if(emptyCycle==nmaxcic){ // Si ha habido un número de ciclos vacíos igual al máximo permitido seguidos.
                            printf("Finalización de programa porque han pasado %i ciclos sin recibirse señales.\n", nmaxcic);
                            kill(getpid(),SIGTERM); // Se envía la señal SIGTERM para terminar el programa.
                        }
                    }
                }
                else{ // Se han recibido señales del tipo que corresponde a i.

                    noSignal = 0; // Como se ha recibido una señal al menos, lo ponemos a 0 para evitar noSignal==N_SIG.
                    emptyCycle = 0; // Como se ha recibido una señal, ya no sigue habiendo ciclos vacios seguidos.
                    
                    // DECODIFICACIÓN: 
                    // El valor de la señal física es igual a la frecuencia con la que
                    // se ha recibido la señal del tipo SIGRTMIN+i en un ciclo de reloj
                    // (es decir, cuantas señales se han recibido por ciclo de reloj).
                    // El casteo es necesario ya que la mayoría de las variables son int.
                    // Se divide millisec/1000 porque lo queremos en segundos.
                    signalValue[i] = (float)signalCount[i]/((float)seconds+(float)millisec/1000.0);
                    printf("-> SIGRTMIN + %i = %f\n", i, signalValue[i]);

                    // overflowedSignals es una variable compartida con el hilo asociado a alarmManage por
                    // lo que es necesario usar mutex para bloquear el uso simultáneo de esta variable. 
                    pthread_mutex_lock(&mut_overflowedSignals); 
                    if(signalValue[i]>=vlimite){ // Si la señal iguala o sobrepasa el valor límite.
                        overflowedSignals++; // Contamos esa señal como que ha sobrepasado.

                        // Si el nº de señales que sobrepasan vlimite es mayor que la mitad
                        // del número de señales totales entraremos en este if.
                        // Para evitar problemas con las divisiones, en vez de escribir la condición 
                        // literalmente como la frase (overflowedSignals >= N_SIG/2)
                        // pasamos el 2 dividiendo al otro lado (multiplicando) y tenemos lo mismo.
                        if(overflowedSignals*2 >=N_SIG){
                            // Mandamos que la condición se ha cumplido y así el hilo asociado a alarmManage
                            // puede continuar (en este caso para encender el indicador).
                            pthread_cond_signal(&cv_overflowedSignals);
                        }
                    }
                    // Liberamos el mutex porque ya hemos terminado de usar overflowedSignals.
                    pthread_mutex_unlock(&mut_overflowedSignals); 
                }
            }

            // Ya se han hecho los cálculos en este ciclo de reloj.
            // Para empezar la cuenta de nuevo ponemos todo a 0
            for(int i=0; i<N_SIG; i++){ 
                signalCount[i]=0; 
                signalValue[i]=0.0; 
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

    // overflowedSignals es una variable compartida con el hilo asociado a alarmManage por
    // lo que es necesario usar mutex para bloquear el uso simultáneo de esta variable.
    pthread_mutex_lock(&mut_overflowedSignals);
    // Para terminar el programa hay que asegurarse que alarma no se quede esperando
    // en la variable de condición por lo que hacemos que overflowedSignals tenga
    // un valor exageradamente grande para que salga del while.
    overflowedSignals=1000;
    // Además enviamos que la variable de condición se ha cumplido para que desbloqueee
    // de la espera.
    pthread_cond_signal(&cv_overflowedSignals);
    // Liberamos el mutex porque ya hemos terminado de usar overflowedSignal.
    pthread_mutex_unlock(&mut_overflowedSignals);
}

////////////////////////////////////////////////// HILO ALARMA //////////////////////////////////////////////////
void *alarmManage(void *p){ 
    //Inicializamos el indicador apagado.
    indicador(0);
    // Creamos el conjunto de señales que contendrá 
    // la señal SIGRTMAX que es la señal que apaga el indicador.
    sigset_t turnOff;
    sigemptyset(&turnOff); // Para inicializar.
    sigaddset(&turnOff, SIGRTMAX);

    // Variable necesaria para sigwaitinfo.
    siginfo_t info;

    //Mientras no sea el fin del programa.
    while(end!= 1){
        // overflowedSignals es una variable compartida con el hilo asociado a signalCalc por
        // lo que es necesario usar mutex para bloquear el uso simultáneo de está variable. 
        pthread_mutex_lock(&mut_overflowedSignals);

        // Mientras el nº de señales que sobrepasan vlimite no sea mayor que la mitad
        // del número de señales totales seguiremos en este bucle.
        // Para evitar problemas con las divisiones, en vez de escribir la condición 
        // literalmente como la frase (overflowedSignals < N_SIG/2)
        // pasamos el 2 dividiendo al otro lado (multiplicando) y tenemos lo mismo.
        while(overflowedSignals*2 < N_SIG){
            // Esta condición también se verifica en el otro hilo por lo que esperamos a recibir 
            // la variable de condición para continuar. Se saldrá del while y se continuará a 
            // encender el indicador.
            pthread_cond_wait(&cv_overflowedSignals, &mut_overflowedSignals);
            // Como se ha cumplido la condición encendemos el indicador.
            // Lo encendemos dentro del while por si se recibe rápidamente que 
            // la condición se vuelve a cumplir. Si estuviese fuera es posible 
            // que si esto sucede demasiado rápido el indicador no se encendierá.
            indicador(1);
        }

        // Liberamos el mutex porque ya hemos terminado de usar overflowedSignals.
        pthread_mutex_unlock(&mut_overflowedSignals);

        // Esperamos que llegue la señal SIGRTMAX.
        sigwaitinfo(&turnOff, &info);

        // Una vez llega la señal podemos apagar el indicador.
        indicador(0);
    }
}

void *signalExamples(void *p){
    switch (example){
        case 1:
        {
            testExample1();
            break;
        }
        case 2:
        {
            testExample2();
            break;
        }
        case 3:
        {
            testExample3();
            break;
        }

    }
}