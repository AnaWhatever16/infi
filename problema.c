#include <stdio.h> // Input/output para printf.
#include "problema.h"

//Función de la cabecera.
void indicador(int valor){
    if (valor == 0){
        printf("Indicador apagado.\n");
    }
    else{
        printf("Indicador encendido.\n");
    }
}
