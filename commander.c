#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>

#define COMMANDER   "/commander-queue"
#define QUEUE_PERMISSIONS 0660
#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 256
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10

int main (int argc, char **argv)
{
    mqd_t commander, receiver;   // queue descriptors
    printf ("Terminal para envío de comandos al programa principal.\n");

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    if ((commander = mq_open (COMMANDER, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
        perror ("Fallo de terminal de comandos.");
        exit (1);
    }

    char inputBuffer [MSG_BUFFER_SIZE];
    char outputBuffer [MSG_BUFFER_SIZE];

    int commandSend = 10000;
    
    printf ("Esperando a programa principal para comenzar.\n");

    while (commandSend!=0) {
        // get the oldest message with highest priority
        printf("----------------------------------------------------------\n");
        
        if (mq_receive (commander, inputBuffer, MSG_BUFFER_SIZE, NULL) == -1) {
            perror ("Fallo en programa principal. No se recibe contacto.\n");
            exit (1);
        }

        // send reply message to client

        if ((receiver = mq_open (inputBuffer, O_WRONLY)) == 1) {
            perror ("No se consigue abrir la cola del programa principal.\n");
            continue;
        }

        printf("Escriba el número del comando que se desee realizar:\n");
        printf("0 = FIN PROGRAMA || 1 = caca || 2 = caca\n");
        printf("Comando: ");
        scanf("%d", &commandSend);
        sprintf (outputBuffer, "%d", commandSend);

        if (mq_send (receiver, outputBuffer, strlen(outputBuffer)+1, 0) == -1) {
            perror ("Error al enviar comando.\n");
            continue;
        }

        printf ("Comando enviado.\n");
    }

}
