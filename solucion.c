#define _POSIX_C_SOURCE 199506L     // POSIX 1003.1c para hilos.

#include <unistd.h>                 // POSIX.
#include <stdlib.h>                 // Macros.
#include <stdio.h>                  // Input/output para sscanf y printf.
#include <time.h>                   // Temporizadores.
#include <string.h>                 // Para tratar los string.
#include <pthread.h>                // Hilos.
#include <signal.h>                 // Señales.
#include <mqueue.h>                 // Para las colas de mensajes
#include <sys/stat.h>               // Macros.

#define COMMANDER   "/commander-queue"      // Nombre de la cola de comandos (la declaramos aquí para que 
                                            // tenga el mismo nombre en el otro programa)
#define MAX_MESSAGES 10                     // Máximo nº de mensajes para la cola
#define MAX_MSG_SIZE 256                    // Tamaño máximo de mensaje
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10   // Tamaño de mensaje en buffer 
                                            // (le sumamos 10 al máximo para darle margen)

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

// Otras variables compartidas que si requerirán mutex porque 
// variarán su valor durante la ejecución del programa
int overflowedSignals = 0;  // Nº de señales que tienen valor mayor que vlimite.
int blockSignal[N_SIG];     // Matriz para la gestión de señales que se deben ignorar

// Temporizador POSIX 1003.1b (declarado absoluto para recoger el tiempo absoluto).
timer_t timerDef;

// Flag de fin de programa (para que los hilos lleguen a su fin y se pueda hacer join).
int end = 0;
// Flag para indicar si se ha pedido fin de programa por linea de comando o no
int out = 0;

// Mutex y variables de condición asociadas a la variable overflowedSignals.
pthread_mutex_t mut_overflowedSignals       = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cv_overflowedSignals        = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut_blockSignal             = PTHREAD_MUTEX_INITIALIZER;

// Manejador en el que nunca entra pero necesario para la definición.
void handeling(int signo, siginfo_t *info, void *p){}

// Definición de las funciones de los hilos.
void *signalCalc(void *p);
void *alarmManage(void *p);
void *messageReceiver(void *p);
void *signalExamples(void *p);

////////////////////////////////////////////////////// MAIN //////////////////////////////////////////////////////
// Se encarga de recoger las variables del problema, gestionar los hilos y sus señales y
// de recibir el fin del programa (con la recepción de la señal SIGTERM).
int main(int argc, char **_argv){

    // Guardar argumentos de la linea de comandos.
    printf("Variables utilizadas:\n");
    sscanf(_argv[1], "%d", &tciclo);    printf("\033[1;34mtciclo\033[0m = %d || ", tciclo);       
    sscanf(_argv[2], "%f", &vlimite);   printf("\033[1;34mvlimite\033[0m = %f || ", vlimite); 
    sscanf(_argv[3], "%d", &nmaxcic);   printf("\033[1;34mnmaxcic\033[0m = %d\n", nmaxcic);

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
        blockSignal[i] = 0;                         // Inicializamos con 0 la matriz de bloqueo de señales.
    }
    // Añadimos SIGRTMAX y SIGALRM al conjunto.
    sigaddset(&expectedSignals, SIGRTMAX); 
    sigaddset(&expectedSignals, SIGALRM);

    // Máscara utilizada para bloquear las señales hasta ahora añadidas a expectedSignals
    // para que solo las puedan usar en sigwaitinfo en los hilos correspondientes
    // (si no se hiciese esto el programa no funciona correctamente).
    pthread_sigmask(SIG_BLOCK, &expectedSignals, NULL);

    // Variables que reciben el identificador de los hilos.
    pthread_t sC;   // signalCalc.
    pthread_t aM;   // alarmManage.
    pthread_t mR;   // messageReceiver
    pthread_t sE;   // signalExamples

    // Creamos los hilos.
    pthread_create(&sC, NULL, signalCalc, NULL);
    pthread_create(&aM, NULL, alarmManage, NULL);
    pthread_create(&mR, NULL, messageReceiver, NULL);
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
    printf("\033[1;31mFinalización de programa solicitada.\n\033[0m");

    // Finalizamos los hilos haciendo que salgan de sus respectivos 
    // bucles while activando el flag end.
    end=1;
    printf("\033[1;31mFinalizando hilos.\n\033[0m");
    
    // Para finalizar el hilo asociado a signalCalc forzamos la señal 
    // de fin de ciclo del temporizador (SIGALRM) (aunque si se está en medio 
    // de una decodificación primero terminará eso).
    kill(getpid(), SIGALRM); 
    
    // Para finalizar el hilo asociado a alarmManage enviamos
    // SIGRTMAX, para terminar la espera en sigwaitinfo,
    // y por tanto se apagará el indicador (si estaba apagada se encenderá
    // y luego se apagará).
    kill(getpid(), SIGRTMAX); 

    // El programa se queda sin finalizar el hilo de messageReceiver si llega
    // SIGTERM desde una orden fuera del terminal de comandos. Por ello es necesario que desde 
    // dicho terminal se envíe el mensaje de fin de programa (0) y para
    // ello se pide con este print (se verá en verde). Esta solución sincroniza además
    // que se cierre el terminal de comandos junto al programa principal.
    if(out == 0){
        printf("\033[1;32mPulse 0 en el terminal de comandos para finalizar programa :)\n\033[0m");
    }
    else{
        printf("\033[1;31mFinalización desde terminal de comandos.\n\033[0m");
    }

    // Espera de la finalización de los hilos.
    pthread_join(sC, NULL); 
    pthread_join(aM, NULL); 
    pthread_join(mR, NULL);
    pthread_join(sE, NULL);

    printf("\033[1;31mHilos finalizados.\nFin de programa.\n\033[0m");

    return 0;
}

////////////////////////////////////// HILO CALCULO VALORES DE LAS SEÑALES //////////////////////////////////////
// Este es el hilo principal del programa. Es el encargado de calcular los valores de las 
// señales físicas externas que vienen representadas por la frecuencia de recepción de señales
// de tipo SIRTMIN+i. Para ello se tiene que definir un temporizador que nos marcará ciclos 
// de tiempo que usaremos para el cálculo de la frecuencia con la que llegan las señales.  
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

    while(end!=1){ // Mientras no sea el final del programa.
        // Guardamos el valor de la señal que recibe sigwaitinfo.
        receivedSignal=sigwaitinfo(&signalSet, &in);

        if(receivedSignal==SIGALRM){ // Si la señal que se recibe es un fin de ciclo de reloj.
            printf("Ciclo de reloj terminado.\n");

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
                            printf("\033[1;31mFinalización de programa porque han pasado %i ciclos sin recibirse señales.\n\033[0m", nmaxcic);
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
                    printf("-> SIGRTMIN + %i (%d)= %f\n", i, signalCount[i], signalValue[i]);

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
            printf("----------------------------------------------------------\n");
        }
        else{ // Si no es SIGALRM será una de las señales de tipo SIGRTMIN + i.
            // blockSignals es una variable compartida con el hilo asociado a messageReceiver por
            // lo que es necesario usar mutex para bloquear el uso simultáneo de esta variable.
            pthread_mutex_lock(&mut_blockSignal);
            // Desde el hilo de messageReceiver se deciden que señales se cuentan y que señales no.
            // Si blockSignal[nº de la señal] = 1 quiere decir que la señal no se debe contar en este momento.
            if (blockSignal[receivedSignal-SIGRTMIN]!=1){
                    // Cada posición del vector está asociada a una de las señales por lo que si 
                    // restamos SIGRTMIN podemos ordenar las señales como 0, 1, ...
                    // Este vector nos sirve para saber cuantas veces han llegado las señales de cada tipo
                    // antes de que acabe un ciclo de reloj (sumando 1 cada vez que llega una de las señales).
                    signalCount[receivedSignal-SIGRTMIN]++;
            }
            // Liberamos el mutex porque ya se ha terminado de usar blockSignal.
            pthread_mutex_unlock(&mut_blockSignal);
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
// Hilo de gestión de la alarma. Dependiendo de la condición establecida
// se encenderá o apagará un indicador (programado en problema.c/.h).
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
        }

        // Como se ha cumplido la condición encendemos el indicador.
        indicador(1);
        // Liberamos el mutex porque ya hemos terminado de usar overflowedSignals.
        pthread_mutex_unlock(&mut_overflowedSignals);

        // Esperamos que llegue la señal SIGRTMAX.
        sigwaitinfo(&turnOff, &info);

        // Una vez llega la señal podemos apagar el indicador.
        indicador(0);
    }
}

////////////////////////////////////////////// HILO RECIBE MENSAJES //////////////////////////////////////////////
// Hilo de tipo cliente. Se encargará de recibir mensajes de un servidor (terminal de comando)
// y dependiendo del contenido del mensaje se realizarán distintas acciones.
void *messageReceiver(void *p){
    mqd_t receiver, commander;  // Id de las colas de mensajes.
    char receiverName[64];      // Nombre de la cola que recibe los comandos.

    sprintf(receiverName, "/receiver-queue-%d", getpid());  // Guardamos el nombre de la cola que recibe datos.
                                                            // El nombre depende del valor del id del proceso porque 
                                                            // se conecta a un servidor que puede recibir más clientes.

    //Definimos la características de la cola.
    struct mq_attr attr;
    attr.mq_flags   = 0;                
    attr.mq_maxmsg  = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;   
    attr.mq_curmsgs = 0;

    // Abrimos la cola que usaremos aquí que es la que recibe los comandos desde el terminal.
    // Se creará la cola si no existe y sino se leerá. Además damos acceso total de lectura, escritura y ejecución,
    // y por último le asociamos los atributos anteriores.
    // El uso del if es para detección de fallos de comunicación (para debug).
    if ((receiver = mq_open(receiverName, O_RDONLY | O_CREAT, S_IRWXU, &attr)) == -1) {
        printf("\033[1;31mFin de programa por error al crear recepción de mensajes.\n\033[0m");
        kill(getpid(), SIGTERM);
    }

    // Abrimos la cola de la que recibiremos los comandos (por eso solo tenemos permisos de escritura).
    // El uso del if es para detección de fallos de comunicación (para debug).
    if ((commander= mq_open(COMMANDER, O_WRONLY)) == -1){
        printf("\033[1;31mFin de programa por no poder contactar programa de comandos.\n\033[0m");
        kill(getpid(), SIGTERM);
    }

    char inputBuffer[MSG_BUFFER_SIZE];  // Buffer de datos de entrada.
    int commandReceived;                // Variable que contendrá el comando a ejecutar.

    // Mientras no sea el fin del programa.
    while(end!=1){
        // Enviamos el nombre de la cola que va a recibir los comando a la que los va
        // a enviar. Se hace así porque el diseño del terminal de comandos es muy parecido 
        // al de un servidor que puede recibir distintos clientes y es necesario que
        // cada cliente se ejecute separadamente en cada intercambio de mensajes.
        // El tamaño del "buffer" en este caso no es más que la longitud del nombre
        // de la cola porque no es necesario más.
        // El uso del if es para detección de fallos de comunicación (para debug).
        if (mq_send (commander, receiverName, strlen(receiverName)+1, 0) == -1) {
            printf("\033[1;31mFin de programa por no contactar con comandos.\n033[0m");
            kill(getpid(), SIGTERM);
        }

        // Aquí recibimos el comando del terminal dentro de inputBuffer.
        // El uso del if es para detección de fallos de comunicación (para debug).
        if (mq_receive (receiver, inputBuffer, MSG_BUFFER_SIZE, NULL) == -1) {
            printf("\033[1;31mFin de programa por mala recepción.\n\033[0m");
            kill(getpid(), SIGTERM);
        }

        // Leemos el valor del comando, ya que el buffer se interpreta como caracteres.
        sscanf(inputBuffer, "%d", &commandReceived);

        // Dependiendo del valor se ejecutan comandos differentes.
        switch (commandReceived){
            case 0:
            {
                // FIN DE PROGRAMA:
                // Se envía la señal de fin de programa SIGTERM que se espera en el main.
                printf("\033[1;31mFin de programa solicitado desde terminal.\n\033[0m");
                out = 1; // Flag para indicar que se termina desde aquí el programa.
                kill(getpid(), SIGTERM); 
                sleep(1);   // Para asegurar que la señal se envía, 
                            //cambia el flag de end y no se quede el 
                            // programa colgado.
                break;
            }
            case 1:
            {
                // IGNORAR SEÑAL:
                // Desde el terminal de comandos se recibe que señal se desea no contar para el cálculo.
                // Se recibe dicho valor en inputBuffer.
                // El uso del if es para detección de fallos de comunicación (para debug).
                int signal;
                if (mq_receive (receiver, inputBuffer, MSG_BUFFER_SIZE, NULL) == -1) {
                    printf("\033[1;31mFin de programa por mala recepción.\n\033[0m");
                    kill(getpid(), SIGTERM);
                }
                sscanf(inputBuffer, "%d", &signal); // Leemos la señal que se quiere ignorar.
                
                // blockSignals es una variable compartida con el hilo asociado a signalCalc por
                // lo que es necesario usar mutex para bloquear el uso simultáneo de esta variable.
                pthread_mutex_lock(&mut_blockSignal);
                if (blockSignal[signal]!=1){ // Comprobamos que no esté ya bloqueada.
                    blockSignal[signal]=1; // Indicamos que se debe bloquear.
                    printf("\033[1;36mSe ignorarán las señales de tipo SIGRTMIN + %d.\n\033[0m", signal);
                }
                else {
                    printf("\033[1;36mSeñal ya ignorada.\n\033[0m");
                }
                // Liberamos el mutex porque se ha dejado de usar blockSignal.
                pthread_mutex_unlock(&mut_blockSignal);
                
                break;
            }
            case 2:
            {
                // CONTAR SEÑAL:
                // Desde el terminal de comandos se recibe que señal se desea volver a contar para el cálculo.
                // Se recibe dicho valor en inputBuffer.
                // El uso del if es para detección de fallos de comunicación (para debug).
                int signal;
                if (mq_receive (receiver, inputBuffer, MSG_BUFFER_SIZE, NULL) == -1) {
                    printf("\033[1;31mFin de programa por mala recepción.\n\033[0m");
                    kill(getpid(), SIGTERM);
                }
                sscanf(inputBuffer, "%d", &signal); //Leemos la señal que se quiere contar

                // blockSignals es una variable compartida con el hilo asociado a signalCalc por
                // lo que es necesario usar mutex para bloquear el uso simultáneo de esta variable.
                pthread_mutex_lock(&mut_blockSignal);
                if(blockSignal[signal]!=0){ // Comporbamos que no se esté contando.
                    blockSignal[signal]=0; // Indicamos que se debe contar
                    printf("\033[1;36mSe vuelven a contar las señales de tipo SIGRTMIN + %d.\n\033[0m", signal);

                }
                else{
                    printf("\033[1;36mSeñal ya contada.\n\033[0m");
                }
                // Liberamos el mutex porque se ha dejado de usar blockSignal.
                pthread_mutex_unlock(&mut_blockSignal);
                
                break;

            }
            case 3:
            {
                // APAGAR INDICADOR:
                // Se envía la señal que apaga el indicador.
                kill(getpid(), SIGRTMAX);
                break;
            }
        }
    }

    // Cerramos la cola que recibe mensajes.
    if (mq_close (receiver) == -1) {
        perror ("Error al cerrar.\n"); // Para debug.
        exit (1);
    }

    // Desvinculamos la cola.
    if (mq_unlink (receiverName) == -1) {
        perror ("Error al cerrar.\n"); // Para debug.
        exit (1);
    }

}

//////////////////////////////////////////////// HILO EJEMPLOS ////////////////////////////////////////////////
// Hilo que llama a varias funciones que contienen envío de señales. 
// El ejemplo a utilizar se escoge por terminal al principio del programa.
// Estos ejemplos se encuentran desarrollados en el archivo signalMessagesExample.c.
// Si se desean probar otros patrones de señales se pueden crear en dicho archivo.
// Los valores utilizados para sobrepasar rápidamente todos los límites son: 
// tciclo = 500, vlimite = 1 y nmaxcic = 3
// Se pueden usar otros valores, no hay problema.
void *signalExamples(void *p){
    switch (example){
        case 1:
        {
            // En este ejemplo se reciben dos señales de tipo SIGRTMIN
            // primero y un segundo despues otra señal de tipo SIGRTMIN
            // y SIGALARM para probar que se apaga el indicador. Si se deja 
            // continuar se puede comprobar como se para el programa 
            // (porque se cumplen los ciclos máximos sin señales) que
            // esperará un 0 del terminal de comandos para finalizar completamente.
            testExample1();
            break;
        }
        case 2:
        {
            // Este ejemplo está pensado para probar los comandos.
            // Se envían constantemente todas las señales de tipo 
            // SIGRTMIN (para N_SIG = 4) cada 500ms.
            while (end!=1){
                testExample2();
            }
            break;
        }
        case 3:
        {
            // Este ejemplo está vacío. Se parará y esperará como el ejemplo 1.
            testExample3();
            break;
        }
        case 4:
        {
            // Envía un patrón de señales cada segundo. 
            // Diseñado para probar los comandos y que se vea mejor la activación
            // y desactivación de la alarma (porque el segundo ejemplo es demasiado rápido).
            while (end!=1)
            {
                testExample4();
            }
            break;
        }
        case 5:
        {
            // Se envían unas señales de tipo SIGRTMIN primero y luego se 
            // envía la señal SIGTERM para demostrar que el envío de dicha señal finaliza el programa.
            // Aún así ocurre como en los otros casos: se espera que se envíe un 
            // 0 del terminal de comandos para finalizar completamente.
            testExample5();
        }
    }
}