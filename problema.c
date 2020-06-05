#include <stdio.h> // Input/output para printf.
#include "problema.h"

//Función de la cabecera.
void indicador(int valor){
    if (valor == 0){
        printf("\033[0;33m");
        printf("Indicador apagado.\n");
        printf("\033[0m");
    }
    else{
        printf("\033[0;33m");
        printf("Indicador encendido.\n");
        printf("\033[0m");
    }
}
