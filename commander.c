#include <stdio.h>      // Input/output para sscanf, printf y sprintf.
#include <stdlib.h>     // Macros.
#include <string.h>     // Para tratar los string.
#include <mqueue.h>     // Para las colas de mensajes.
#include <sys/stat.h>   // Macros.
#include "problema.h"   // Cabecera dada por problema.

#define COMMANDER   "/commander-queue"      // Nombre de la cola de comandos (la declaramos aquí para que 
                                            // tenga el mismo nombre en el otro programa)      
#define MAX_MESSAGES 10                     // Máximo nº de mensajes para la cola
#define MAX_MSG_SIZE 256                    // Tamaño máximo de mensaje
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10   // Tamaño de mensaje en buffer 
                                            // (le sumamos 10 al máximo para darle margen)

int main (int _argc, char **_argv)
{
    mqd_t commander, receiver; // Id de las colas de mensajes.
    printf ("Terminal para envío de comandos al programa principal.\n");

    //Definimos la características de la cola.
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    // Abrimos la cola que usaremos aquí que es la que manda los comandos desde el terminal.
    // Se creará la cola si no existe y sino se leerá. Además damos acceso total de lectura, escritura y ejecución,
    // y por último le asociamos los atributos anteriores.
    // El uso del if es para detección de fallos de comunicación (para debug).
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
        // Se hace así porque el diseño del terminal de comandos es muy parecido 
        // al de un servidor que puede recibir distintos clientes y es necesario que
        // cada cliente se ejecute separadamente en cada intercambio de mensajes.
        // El uso del if es para detección de fallos de comunicación (para debug).
        if (mq_receive (commander, inputBuffer, MSG_BUFFER_SIZE, NULL) == -1) {
            perror ("Fallo en programa principal. No se recibe contacto.\n");
            exit (1);
        }
        
        // Abrimos la cola de mensajes que hemos recibido (el nombre se ha recibido antes en 
        // inputbuffer) y lo programamos para poder escribir.
        // El uso del if es para detección de fallos de comunicación (para debug).
        if ((receiver = mq_open (inputBuffer, O_WRONLY)) == 1) {
            perror ("No se consigue abrir la cola del programa principal.\n");
            continue;
        }

        // EL comportamiento de cada comando viene explicado en el código de
        // solucion.c en la función messageReceiver.
        printf("Escriba el número del comando que se desee realizar:\n");
        printf("0 = \033[1;34mFIN PROGRAMA\033[0m || 1 = \033[1;34mIGNORAR SEÑAL\033[0m || 2 = \033[1;34mCONTAR SEÑAL\033[0m || 3 = \033[1;34mAPAGAR INDICADOR\033[0m\n");
        printf("Comando: ");

        scanf("%d", &commandSend); // Recogemos el valor del comando del terminal.
        sprintf (outputBuffer, "%d", commandSend); // Enviamos el valor del comando al buffer de salida.

        // Enviamos el valor del comando desde el buffer de salida. El tamaño del buffer se define como el 
        // tamaño de lo que contiene más un margen para el caracter de terminación de string.
        // El uso del if es para detección de fallos de comunicación (para debug).
        if (mq_send (receiver, outputBuffer, strlen(outputBuffer)+1, 0) == -1) {
            perror ("Error al enviar comando.\n");
            continue;
        }

        // Si el comando enviado ha sido el 1 o el 2 se tiene que enviar además que señal
        // es la que se desea modificar el comportamiento.
        if (commandSend == 1 || commandSend == 2){
            int signal;
            printf("Señal (de 0 a %d): SIGRTMIN + ", N_SIG);
            scanf("%d", &signal); // Lectura de la señal deseada.
            printf("\n");
            sprintf(outputBuffer, "%d", signal); // Metemos el valor en el buffer de salida para el envío.
            
            // Envío de la señal elegida a la cola de mensajes del otro programa.
            // El uso del if es para detección de fallos de comunicación (para debug).
            if (mq_send (receiver, outputBuffer, strlen(outputBuffer)+1, 0) == -1) {
                perror ("Error al enviar comando.\n");
                continue;
            }
        }

        printf ("\033[1;32mComando enviado.\033[0m\n");
    }
}