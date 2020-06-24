#include <stdio.h>      // Input/output para sscanf, printf y sprintf.
#include <stdlib.h>     // Macros.
#include <string.h>     // Para tratar los string.
#include <mqueue.h>     // Para las colas de mensajes.
#include <sys/stat.h>   // Macros.
#include "problema.h"   // Cabecera dada por problema.

#define COMMANDER   "/commander-queue"      // Nombre de la cola de comandos (la declaramos aqui para que 
                                            // tenga el mismo nombre en el otro programa)      
#define MAX_MESSAGES 10                     // Maximo num de mensajes para la cola
#define MAX_MSG_SIZE 256                    // Tamanno maximo de mensaje
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10   // Tamanno de mensaje en buffer 
                                            // (le sumamos 10 al maximo para darle margen)

int main (int _argc, char **_argv)
{
    mqd_t commander, receiver; // Id de las colas de mensajes.
    printf ("Terminal para envio de comandos al programa principal.\n");

    //Definimos la caracteristicas de la cola.
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    // Abrimos la cola que usaremos aqui que es la que manda los comandos desde el terminal.
    // Se creara la cola si no existe y sino se leera. Ademas damos acceso total de lectura, escritura 
    // y ejecucion, y por ultimo le asociamos los atributos anteriores.
    // El uso del if es para deteccion de fallos de comunicacion (para debug).
    if ((commander = mq_open (COMMANDER, O_RDONLY | O_CREAT, S_IRWXU, &attr)) == -1) {
        perror ("Fallo de terminal de comandos.");
        exit (1);
    }

    char inputBuffer [MSG_BUFFER_SIZE];     // Buffer de entrada para recibir el nombre de la otra cola.
    char outputBuffer [MSG_BUFFER_SIZE];    // Buffer de salida para enviar datos.

    int commandSend = 10000; // Inicializar para que no sea 0.
    
    printf ("Esperando a programa principal para comenzar.\n");

    while (commandSend!=0) { // El comando 0 finaliza este programa tambien.
        printf("----------------------------------------------------------\n");
        
        // Recibimos el nombre de la cola que va a recibir los comando.
        // Se hace asi porque el disenno del terminal de comandos es muy parecido 
        // al de un servidor que puede recibir distintos clientes y es necesario que
        // cada cliente se ejecute separadamente en cada intercambio de mensajes.
        // El uso del if es para deteccion de fallos de comunicacion (para debug).
        if (mq_receive (commander, inputBuffer, MSG_BUFFER_SIZE, NULL) == -1) {
            perror ("Fallo en programa principal. No se recibe contacto.\n");
            exit (1);
        }
        
        // Abrimos la cola de mensajes que hemos recibido (el nombre se ha recibido antes en 
        // inputbuffer) y lo programamos para poder escribir.
        // El uso del if es para deteccion de fallos de comunicacion (para debug).
        if ((receiver = mq_open (inputBuffer, O_WRONLY)) == 1) {
            perror ("No se consigue abrir la cola del programa principal.\n");
            continue;
        }

        // EL comportamiento de cada comando viene explicado en el codigo de
        // solucion.c en la funcion messageReceiver.
        printf("Escriba el numero del comando que se desee realizar:\n");
        printf("0 = \033[1;34mFIN PROGRAMA\033[0m || 1 = \033[1;34mIGNORAR SEnnAL\033[0m || 2 = \033[1;34mCONTAR SEnnAL\033[0m || 3 = \033[1;34mAPAGAR INDICADOR\033[0m\n");
        printf("Comando: ");

        scanf("%d", &commandSend); // Recogemos el valor del comando del terminal.
        sprintf (outputBuffer, "%d", commandSend); // Enviamos el valor del comando al buffer de salida.

        // Enviamos el valor del comando desde el buffer de salida. El tamanno del buffer se define como el 
        // tamanno de lo que contiene mas un margen para el caracter de terminacion de string.
        // El uso del if es para deteccion de fallos de comunicacion (para debug).
        if (mq_send (receiver, outputBuffer, strlen(outputBuffer)+1, 0) == -1) {
            perror ("Error al enviar comando.\n");
            continue;
        }

        // Si el comando enviado ha sido el 1 o el 2 se tiene que enviar ademas que sennal
        // es la que se desea modificar el comportamiento.
        if (commandSend == 1 || commandSend == 2){
            int signal;
            printf("Sennal (de 0 a %d): SIGRTMIN + ", N_SIG);
            scanf("%d", &signal); // Lectura de la sennal deseada.
            printf("\n");
            sprintf(outputBuffer, "%d", signal); // Metemos el valor en el buffer de salida para el envio.
            
            // Envio de la sennal elegida a la cola de mensajes del otro programa.
            // El uso del if es para deteccion de fallos de comunicacion (para debug).
            if (mq_send (receiver, outputBuffer, strlen(outputBuffer)+1, 0) == -1) {
                perror ("Error al enviar comando.\n");
                continue;
            }
        }
        printf ("\033[1;32mComando enviado.\033[0m\n");
    }
}