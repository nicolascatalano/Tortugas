#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <time.h>

int leeGPS(char *pBuffer);

#define BUFFER_SIZE 512
int leeGPS(char *pBuffer)
{
// Buscar tramas de GPS dentro del buffer
// Para la versión 0 del encabezado las tramas GPS deben estar alineadas con el inicio del segmento
// Por lo tanto el segmento debe empezar con $G
// Verific si el campo de la fecha está completo
// Devuelve el largo de la trama (incuye encabezado y CS) si encontró una trama y su CS es válido
    uint16_t contador = 0;
    uint8_t cs_calculado = 0, cs_recibido;

    char *pComa, i;
    pComa = pBuffer;


    if( ('$' == pBuffer[0]) &&  ('G' == pBuffer[1]) )
    {


    /////////
        for(i=0;i<9; i++)// Cuento 9 comas para llegar a la fecha
        {
            pComa = strchr(++pComa, ',');
        }
        pComa++;

        if(*pComa == ',')
        {
            return 0; // El campo de la fecha estaba vacío y hay que descatar la trama
        }
    ////////////

        pBuffer++; //Descarto el '$'
        contador++;
        cs_calculado ^= *pBuffer++;

        do{
            cs_calculado ^= *pBuffer++;
            contador++;
        }while( (*pBuffer != '*') && (contador < (BUFFER_SIZE-2) ) );

        if( (BUFFER_SIZE-2) != contador)
        {
            contador++;
            pBuffer++;

			if(*pBuffer>'9')
			{
				cs_recibido = (*pBuffer++ - 55)<<4; //A-F
			}
			else
			{
				cs_recibido = (*pBuffer++ - 0x30)<<4; //0-9
			}
			contador++;

			if(*pBuffer>'9')
			{
				cs_recibido += (*pBuffer - 55); //A-F
			}
			else
			{
				cs_recibido += (*pBuffer - 0x30); //0-9
			}
			contador++;

			if(cs_recibido == cs_calculado)
			{
                return contador+1;
			}

        }

    }

    return 0; // No se encontró una trama GPS en el buffer o no era válida


}
