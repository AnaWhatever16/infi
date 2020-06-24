#include <stdio.h> // Input/output para printf.
#include "problema.h"

//Funcion de la cabecera.
void indicador(int valor){
    if (valor == 0){
        printf("\033[0;33mIndicador apagado.\n\033[0m");
    }
    else if (valor == 1){
        printf("\033[0;33mIndicador encendido.\n\033[0m");
    }
}
