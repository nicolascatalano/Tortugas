// Lee los datos de GPS y genera dos archivo con estos datos, txt y un kml.
// Con el tiempo GPS genera etiquetea de tiempo par alos datos de IMU. Lee los datos de IMU y genera un archivo con los datos y las etiquetas de tiempo.
// Genera un archivo con lati, long, fecha, hora y promedio de la temperatura de la IMU del minuto (aprox) anterior a la posición GPS.


/*
Hacer un promedio de un minuto de la temperatura de la IMU para cada dato GPS.

*/

// lat;lon;date;time;tempIMU;
// tempmin(C);temped(C);Temprocíomediadiaria(C);Humedadrelativadiaria(%);vientodir;viento(km/h)vel;
// Presionsobreniveldelmar(Hp);Prec(mm);NubesTotOct;NubbajOct;Vis(km)

// ToDo Cambiar el 256 por un parámetro

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <time.h>

// Lee la versión del encabezado para decidir cómo interpretar los datos.
// Lee el tamaño del bloque para apuntar al inicio de cada bloque para buscar el inicio de los datos de GPS o IMU

#define SD_STRUCT_INUQUEID_INDEX_0         0
#define SD_STRUCT_SIZE_INDEX_0            12
#define SD_STRUCT_VERSION_INDEX_0         16
#define SD_BLOCK_SIZE_INDEX_0             20
#define IMU_BYTES_PAQUETE_INDEX_0		  24
#define IMU_PAQUETES_SEGMENTO_INDEX_0     28
#define LAST_SEGMENT_INDEX_0              32


#define HEADER_IMU_0                  "<IMU"
#define HEADER_GPS_0                    "$G"


#define uint8_t     char
#define uint32_t    char32_t



#define IMU_TEMP_PROM_VENTANA   60 // Tiempo en segundos para promediar la temp de la IMU antes de cada GPS
#define IMU_SAMPLE_RATE         6  // Tasa de datos aproximada de sampleo de la temperatura


typedef struct encabezado
{
	uint8_t  unique_id[12]; 				// Identificación del CPU
	uint32_t N_bytes;       				// Tamaño del encabezado
	uint32_t version;       				// Versión del formato de datos
	uint32_t block_size;       			// Cantidad de bytes usados en cada segmento de la SD
    uint32_t IMU_bytes_paquete; 		// Cantidad de bytes por paquete de datos de la IMU (giros acc, temp)
	uint32_t IMU_paquete_segmento;	// Cantidad de paquetes de la IMU en cada segmento de la SD
	uint32_t segmento_fin;  				// Último segmento ocupado
} encabezado_t;


typedef struct {
        int year;
        int month;
        int day;
        int hour;
        int minutes;
        int seconds;
        } tiempo_t;

typedef struct lat_lon {
        double lat;
        double lon;
        } lat_lon_t;


// productosIMU_t
// Por ahora tiene solo el promedio de temperatura del primer minuto (aprox).
// La idea es agregar otros productos, como por ejemplo, el nivel de actividad del intervalo

typedef struct {
        float temp_promedio;
        int   tempIMU_prom_segmentos_N;
        } productosIMU_t;

uint32_t leeSD_8_32(uint8_t* pBuffer, uint16_t offset);
void imprimeEncabezado(encabezado_t*);
void cargaEncabezado(char*, encabezado_t*);
extern int leeGPS(char *pBuffer);
// int leeIMU(char *pBuffer, encabezado_t *encabezado, FILE* foutIMU);
//int leeIMU(char *pBuffer, encabezado_t *encabezado, uint16_t cont_segmentos, struct tm inicio_GPS_time, float Ts, FILE* foutIMU);
int leeIMU(char *pBuffer, encabezado_t *encabezado, uint16_t cont_segmentos, struct tm inicio_GPS_time, float Ts, FILE* fDatoscrudos, FILE* foutIMU, productosIMU_t* productos);
struct tm decodeGPS_fecha_hora(uint8_t *pBuffer);
uint32_t diferenciaTiempo(tiempo_t t1, tiempo_t t2);
struct lat_lon decodeLatLon(uint8_t *pBuffer);


char encabezadoKML[] =  "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                        "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
                        "<Document id=\"root_doc\">\n"
                        "<Folder><name>Tracks</name>\n"
                        "</Folder>\n"
                        "<Folder><name>Layer0</name>\n"
                        "   <Placemark>\n"
                        "   <name>Path</name>\n"
                        "   <Style><LineStyle><color>ff0000ff</color></LineStyle><PolyStyle><fill>0</fill></PolyStyle></Style>\n"
                        "       <LineString><coordinates>";

char finKML[] = "</coordinates></LineString>"
                "\n   </Placemark>\n"
                "</Folder>\n"
                "</Document></kml>\n";



char encabezadoCSV[] = "lat;lon;date;timeGMT;tempIMU";


int main(int argc, char* argv[])
{
    FILE *fDatoscrudos, *foutGPS, *foutIMU, *foutKML, *foutCSV;
    char archivoGPSout[] = "GPSout.txt";
    char archivoKMLout[] = "GPSout.kml";
    char archivoIMUout[] = "IMUoutTiempo.csv";
    char archivoCSVout[] = "LLDTT.csv";

    char *pBuffer, Buffer[512];
    char dummy[60], dummyCSV[30];

    long int Offset_GPS_inicio, Offset_GPS_next;

    char promIMUvalido = 0; // Indica que ya se dispone del proimerdio de las últimas muestras para poder escribir el archivo LLDTT
    int tempIMU_prom_segmentos;

    uint16_t largo_trama, contador;
    encabezado_t encabezado;
    productosIMU_t productosIMU;

    float Ts;
    uint32_t dif_tiempo;

    struct tm inicio_GPS_time, fin_GPS_time;
    struct lat_lon posicion;
    time_t inicio_GPS_time_s, fin_GPS_time_s;


    printf("\nLee los datos del GPS y de la IMU del archivo crudo y los guarda en archivos separados. \nGenera etiquetas de tiempo para los datos de la IMU con los datos del GPS\n");

    if(argc<2)
    {
        printf("Indique el nombre del archivo a procesar");
        return -1;
    }


    if( (fDatoscrudos = fopen(argv[1], "rb")) == NULL)
    {
        printf("Error al abrir archivo de entrada");
        return -1;
    }

    sprintf(dummy, "%s_%s", argv[1], archivoGPSout);
    if( (foutGPS = fopen(dummy, "w")) == NULL)
    {
        fclose(fDatoscrudos);
        printf("Error al abrir archivo de salida para el GPS");
        return -1;
    }

        sprintf(dummy, "%s_%s", argv[1], archivoKMLout);
    if( (foutKML = fopen(dummy, "w")) == NULL)
    {
        fclose(fDatoscrudos);
        fclose(foutGPS);
        printf("Error al abrir archivo de salida para el KML");
        return -1;
    }

    fprintf(foutKML, "%s", encabezadoKML);


    sprintf(dummy, "%s_%s", argv[1], archivoIMUout);
    if( (foutIMU = fopen(dummy, "w")) == NULL)
    {
        fclose(foutGPS);
        fclose(foutKML);
        fclose(fDatoscrudos);
        printf("Error al abrir archivo de salida para el la IMU");
        return -1;
    }



    strcpy(dummyCSV, argv[1]);
    dummy[0] = dummyCSV[0];
    dummy[1] = dummyCSV[1];
    dummy[2] = dummyCSV[2];
    dummy[3] = dummyCSV[3];
    dummy[4] = '_';
    dummy[5] = dummyCSV[13];
    dummy[6] = dummyCSV[14];
    dummy[7] = dummyCSV[15];
    dummy[8] = dummyCSV[16];
    dummy[9] = 0;



    sprintf(dummy, "%s_%s", dummy, archivoCSVout);

    // Verifico si el archivo ya existe. Si no existe lo creo y le escribo la primera línea con las etiquetas
    if( (foutCSV = fopen(dummy, "r")) == NULL)
    {// No existe
        if( (foutCSV = fopen(dummy, "a")) == NULL)
        {// Error al crearlo
            fclose(foutIMU);
            fclose(foutGPS);
            fclose(foutKML);
            fclose(fDatoscrudos);
            printf("Error al crear archivo de salida CSV");
            return -1;
        }
        else
        {// Escribe el encabezado
            fprintf(foutCSV, "%s\n", encabezadoCSV);
        }
    }
    else
    {// El archivo existe pero lo abrí para lectura, lo cierro y lo vuelvo a abrir para append.
        fclose(foutCSV);
        if( (foutCSV = fopen(dummy, "a")) == NULL)
        {// Error al crearlo
            fclose(foutIMU);
            fclose(foutGPS);
            fclose(foutKML);
            fclose(fDatoscrudos);
            printf("Error al abrir archivo de salida CSV");
            return -1;
        }
    }

    pBuffer = Buffer;
    fread(pBuffer, 1, 512, fDatoscrudos);

    cargaEncabezado(pBuffer, &encabezado);

    imprimeEncabezado(&encabezado);


// Calcula la cantidad de segmentos a utilizar para tomar el promedio de la temperatura de la IMU
    productosIMU.tempIMU_prom_segmentos_N = (int)((IMU_TEMP_PROM_VENTANA*IMU_SAMPLE_RATE)/encabezado.IMU_paquete_segmento);

    pBuffer = Buffer;
// Busca un buffer con GPS, extrae los datos y los guarda en los archivos .txt y  .kml.
// Vuelve al inicio del archivo y .. sigue leyendo la memoria hasta que encuentra otro buffer con GPS contando la cantidad de buffers que salteó.
// Calcula la diferencia de tiempo entre los buffers GPS y la divide por la cantidad de datos de la IMU que había
// entre los buffer con GPS.
// Genera los etiquetas de tiempo para cada muestra de la IMU y escribe todos los datos en el archivo .csv.

    fseek(fDatoscrudos, 0, SEEK_SET); // Vuelvo al inicio del archivo
    while(fread(pBuffer, 1, 512, fDatoscrudos))
    {
        largo_trama = leeGPS(pBuffer);
        if(largo_trama)
        {
            fwrite(pBuffer, 1, largo_trama, foutGPS); // Escribe en el .txt
            fwrite("\n", 1, 1, foutGPS);
            posicion = decodeLatLon(pBuffer);
            fprintf(foutKML, "%.6f,%.6f ", posicion.lon, posicion.lat); // Escribe en el .kml

        }
    }

    pBuffer = Buffer;
    Offset_GPS_next = 0;

    while(1)
    {
        fseek(fDatoscrudos, Offset_GPS_next, SEEK_SET); //

        do // Busca el próximo segmento con GPS
        {
            if(!fread(pBuffer, 1, 512, fDatoscrudos))
            {// Entra cuando se termina el archivo
                fprintf(foutKML, "%s", finKML);
                fclose(foutKML);
                fclose(foutGPS);
                fclose(foutIMU);
                fclose(fDatoscrudos);
                fclose(foutCSV);
                printf("\nFin");
                return 0;
            }

        }while(!leeGPS(pBuffer));

        Offset_GPS_inicio = ftell(fDatoscrudos);

        inicio_GPS_time = decodeGPS_fecha_hora(pBuffer);
        posicion = decodeLatLon(pBuffer); // Para tenerlo disponible para el último dato de LLDTT.csv


        contador = 0;
        do // Busca el próximo GPS y cuenta los segmentos que hay en el medio
        {
            if(!fread(pBuffer, 1, 512, fDatoscrudos))
            { // Entra cuando se termina el archivo
                fprintf(foutKML, "%s", finKML);
                fclose(foutKML);

                fclose(foutGPS);
                fclose(foutIMU);
                fclose(fDatoscrudos);

                // Último dato de GPS para LLDTT
               // posicion = decodeLatLon(pBuffer);
                fprintf(foutCSV, "%f;%f;", posicion.lat, posicion.lon);
                fprintf(foutCSV, "%02d-%02d-%02d;", inicio_GPS_time.tm_year-100+2000, inicio_GPS_time.tm_mon+1, inicio_GPS_time.tm_mday);
                fprintf(foutCSV, "%02d%02d", inicio_GPS_time.tm_hour, inicio_GPS_time.tm_min);
                fprintf(foutCSV, ";%3.2f\n", productosIMU.temp_promedio);


                fclose(foutCSV);
                printf("\fin");
             return 0;
            }
            contador++;
   //         printf("\n%c%c%c", *(pBuffer+1), *(pBuffer+2), *(pBuffer+3));
   //         printf(" contador: %d", contador);
        }while(!leeGPS(pBuffer)); // Busca el próximo buffer que tiene datos GPS



        Offset_GPS_next = ftell(fDatoscrudos)-512;

        fin_GPS_time = decodeGPS_fecha_hora(pBuffer);

        fin_GPS_time_s = mktime(&fin_GPS_time);
        inicio_GPS_time_s = mktime(&inicio_GPS_time);

        if(fin_GPS_time_s < inicio_GPS_time_s)
        {

            fclose(foutKML);

            fclose(foutGPS);
            fclose(foutIMU);
            fclose(fDatoscrudos);

            // Último dato de GPS para LLDTT
           // posicion = decodeLatLon(pBuffer);
            fprintf(foutCSV, "%f;%f;", posicion.lat, posicion.lon);
            fprintf(foutCSV, "%02d-%02d-%02d;", inicio_GPS_time.tm_year-100+2000, inicio_GPS_time.tm_mon+1, inicio_GPS_time.tm_mday);
            fprintf(foutCSV, "%02d%02d", inicio_GPS_time.tm_hour, inicio_GPS_time.tm_min);
            fprintf(foutCSV, ";%3.2f\n", productosIMU.temp_promedio);


            fclose(foutCSV);
            printf("\nTermino porque encontro datos viejos en la memoria");
            return  0; // Encontró datos viejos en la memoria
        }

        dif_tiempo = fin_GPS_time_s - inicio_GPS_time_s; // Diferencia de tiempo en segundos
        Ts = 1.0*dif_tiempo/(1.0*((contador-1)*encabezado.IMU_paquete_segmento));

        // lat;lon;date;time;tempIMU;
        if(promIMUvalido == 1)
        {
            posicion = decodeLatLon(pBuffer);
            fprintf(foutCSV, "%f;%f;", posicion.lat, posicion.lon);
            fprintf(foutCSV, "%02d-%02d-%02d;", inicio_GPS_time.tm_year-100+2000, inicio_GPS_time.tm_mon+1, inicio_GPS_time.tm_mday);
            fprintf(foutCSV, "%02d%02d", inicio_GPS_time.tm_hour, inicio_GPS_time.tm_min);
            fprintf(foutCSV, ";%3.2f\n", productosIMU.temp_promedio);
        }


    // Vuelvo para atrás para leer los paquetes de IMU
        fseek(fDatoscrudos, Offset_GPS_inicio, SEEK_SET);

        leeIMU(pBuffer, &encabezado, contador, inicio_GPS_time, Ts, fDatoscrudos, foutIMU, &productosIMU);
        promIMUvalido = 1;
        //fprintf(foutCSV, ";%3.2f\n", productosIMU.temp_promedio);
    }



    fclose(foutGPS);
    fclose(foutIMU);
    fclose(fDatoscrudos);
    fclose(foutKML);
    fclose(foutCSV);



    printf("\fin");
    return -1;
}


uint32_t leeSD_8_32(uint8_t* pBuffer, uint16_t offset)
{
 // Lee 4 bytes desde el buffer de la SD y los acomoda para 32 bits
    union{
        uint32_t uint32;
        uint8_t  uint8[4];
        } union_32_8;

    uint8_t k;

    for(k=0;k<4;k++)
    {
        union_32_8.uint8[3-k] = pBuffer[k+offset];
    }

    return union_32_8.uint32;
}

void imprimeEncabezado(encabezado_t *encabezado)
{
    uint8_t k;

    printf("UNIQUE ID: ");
    for(k=0;k<sizeof(encabezado->unique_id);k++)
    {
        printf("%2.2X ", encabezado->unique_id[k]&0xFF);
    }
    printf("\n");
    printf("Tamaño del encabezado (N_bytes): %d\n", encabezado->N_bytes);
    printf("Version del encabezado: %d\n", encabezado->version);
    printf("Cantidad de bytes en cada segmento de la SD (block_size): %d\n", encabezado->block_size);
    printf("Cantidad de bytes por paquete de la IMU (IMU_bytes_paquete): %d\n", encabezado->IMU_bytes_paquete);
    printf("Cantidad de paqueteds de la IMU en cada segmento (IMU_paquete_segmento): %d\n", encabezado->IMU_paquete_segmento);
    printf("Ultimo segmento utilizado (segmento_fin): %d\n", encabezado->segmento_fin);

}

void cargaEncabezado(char *pBuffer, encabezado_t *encabezado)
{
    uint8_t k;

    // Lee el ID de la memoria
    for(k=0;k<sizeof(encabezado->unique_id);k++)
    {
        encabezado->unique_id[k] = pBuffer[k];
    }

    // Lee la versión del encabezado
    encabezado->version = leeSD_8_32(pBuffer, SD_STRUCT_VERSION_INDEX_0);

    // Lee el tamaño del encabezado
    encabezado->N_bytes = leeSD_8_32(pBuffer, SD_STRUCT_SIZE_INDEX_0);

    // Lee la cantidad de sectores para leer
    encabezado->segmento_fin = leeSD_8_32(pBuffer, LAST_SEGMENT_INDEX_0);

    // Lee la cantidad de bytes que se grabar por segmento en la SD
    encabezado->block_size = leeSD_8_32(pBuffer, SD_BLOCK_SIZE_INDEX_0);

    // Lee IMU_BYTES_PAQUETE_INDEX_0
    encabezado->IMU_bytes_paquete = leeSD_8_32(pBuffer, IMU_BYTES_PAQUETE_INDEX_0);

    encabezado->IMU_paquete_segmento = leeSD_8_32(pBuffer, IMU_PAQUETES_SEGMENTO_INDEX_0);
}


//int leeIMU(char *pBuffer, encabezado_t *encabezado, FILE* foutIMU)
int leeIMU(char *pBuffer, encabezado_t *encabezado, uint16_t cont_segmentos, struct tm inicio_GPS_time, float Ts, FILE* fDatoscrudos, FILE* foutIMU, productosIMU_t* productosIMU)
{
// Buscar tramas de IMU dentro del buffer
// Para la versión 0 del encabezado las tramas IMU deben estar alineadas con el iniocio del segmento
// Por lo tanto el segmento debe empezar con <IMU
    int16_t dato[7], cont_prom_temp_IMU;

    float temp_C, temp_acc;
    uint8_t i, j;
    float segundos_mil;
    uint16_t segmentos;


    union{
        int16_t int16;
        char uint8[2];
        } union_16_8;

 //   cuenta_lineas = 0;
    segundos_mil = inicio_GPS_time.tm_sec;
    temp_acc = 0; // Acumulador para sacar el promedio de N samples de la temp
    cont_prom_temp_IMU = 0;

    for(segmentos = 0; segmentos<cont_segmentos-1; segmentos++)
    {
        fread(pBuffer, 1, 512, fDatoscrudos);

// Algunas veces vienen segmentos inválidos dentro de los cont_segmentos. Hay que descartarlos pero considero que hay que incrementar en un Ts.

//        if( ('<' == pBuffer[0]) && ('I' == pBuffer[1]) && ('M' == pBuffer[2]) && ('U' == pBuffer[3]) )
//        {
        for(i=0; i<encabezado->IMU_paquete_segmento; i++) // paquetes de datos por segmento
        {
            if(segundos_mil>=60)
            {
                segundos_mil -= 60;
                inicio_GPS_time.tm_min++;
                if(inicio_GPS_time.tm_min>=60)
                {
                    inicio_GPS_time.tm_min -=60;
                    inicio_GPS_time.tm_hour++;
                    if(inicio_GPS_time.tm_hour>=24)
                    {
                        inicio_GPS_time.tm_hour -= 24;
                        inicio_GPS_time.tm_mday++;
                        //TODO completar con el mes
                    }
                }
            }

            if( ('<' == pBuffer[0]) && ('I' == pBuffer[1]) && ('M' == pBuffer[2]) && ('U' == pBuffer[3]) )
            { // Verifico que sea un segmento con datos de IMU
              //  fprintf(foutIMU, "%03d ", ++cuenta_lineas);
                fprintf(foutIMU, "%d", inicio_GPS_time.tm_year+1900);
                fprintf(foutIMU, "_%02d", inicio_GPS_time.tm_mon+1);
                fprintf(foutIMU, "_%02d", inicio_GPS_time.tm_mday);
                fprintf(foutIMU, "_%02d", inicio_GPS_time.tm_hour);
                fprintf(foutIMU, "_%02d", inicio_GPS_time.tm_min);
                //printf("_%02d", (int)segundos_mil);
                fprintf(foutIMU, "_%06.3f, ", segundos_mil);

                for(j=0; j<7; j++) // Cantidad de datos en cada paquete (girosx3, tempx1, accx3)
                {
                    union_16_8.uint8[1] = pBuffer[4+i*encabezado->IMU_bytes_paquete+j*2];
                    union_16_8.uint8[0] = pBuffer[4+i*encabezado->IMU_bytes_paquete+j*2+1];
                    dato[j] = union_16_8.int16;
                    fprintf(foutIMU, "%6d, ", dato[j]);
                    // printf("%d; ", dato[j]);
                }

                temp_C = dato[3]/340.0+36.53; // De la hoja de datos de la IMU

                // El promedio de temp lo calcula con el último minuto antes del GPS
                if((segmentos+2)>(cont_segmentos-productosIMU->tempIMU_prom_segmentos_N))
               //  if(cont_prom_temp_IMU < productosIMU->temp_promedio_N)
                {
                    cont_prom_temp_IMU++;
                    temp_acc = temp_acc + temp_C; // acumula para calcular el promedio de N muestras
                }

                // Actualiza el promedio cada vez por si no llega a temp_promedio_N samples
                productosIMU->temp_promedio = temp_acc/(cont_prom_temp_IMU);
                //printf("\n%d", cont_prom_temp_IMU);

                fprintf(foutIMU, "%.2f, ", temp_C);
                fprintf(foutIMU, "%1.3f\n", Ts);
                // printf("%.2f \n", temp_C);
            }
            else
            {
                // printf("\n Segmento inválido \n");
            }
            segundos_mil += Ts;
        }
     //   else
 //       {
  //          return 1;
  //      }

    }
    // No está llgano hasta acá, sale en el return 1
    productosIMU->temp_promedio = temp_acc/(cont_prom_temp_IMU-1);
    return 0;
}


//tiempo_t decodeGPS_fecha_hora(uint8_t *pBuffer)
struct tm decodeGPS_fecha_hora(uint8_t *pBuffer)
{
//Validar el tipo de trama (GPRMC)
    char header[] = "$GPRMC";
    char temp_s[2];
    char i, *pComa;

    struct tm timeinfo;



    if(strstr(pBuffer, header))
    {
        pComa = strchr(pBuffer, ',');
        pComa++;

        for(i=0;i<sizeof(temp_s); i++)
        {
            temp_s[i] = *pComa++;
        }
        timeinfo.tm_hour = atoi(temp_s);

        for(i=0;i<sizeof(temp_s); i++)
        {
            temp_s[i] = *pComa++;
        }
        timeinfo.tm_min = atoi(temp_s);

        for(i=0;i<sizeof(temp_s); i++)
        {
            temp_s[i] = *pComa++;
        }
        timeinfo.tm_sec = atoi(temp_s);

        for(i=0;i<8; i++)// Cuento 9 comas para llegar a la fecha
        {
            pComa = strchr(++pComa, ',');
        }
        pComa++;



        for(i=0;i<sizeof(temp_s); i++)
        {
            temp_s[i] = *pComa++;
        }
        timeinfo.tm_mday = atoi(temp_s);

        for(i=0;i<sizeof(temp_s); i++)
        {
            temp_s[i] = *pComa++;
        }
        timeinfo.tm_mon = atoi(temp_s)-1;

        for(i=0;i<sizeof(temp_s); i++)
        {
            temp_s[i] = *pComa++;
        }
        timeinfo.tm_year = atoi(temp_s)+2000-1900;
  /*
        for(i=0;i<sizeof(fecha_s); i++)
        {
            fecha_s[i] = pComa[i];
        }
*/
    }

    return timeinfo;
}


struct lat_lon decodeLatLon(uint8_t *pBuffer)
{
//Validar el tipo de trama (GPRMC)
    char header[] = "$GPRMC";
    char grad_lon[5] = "", grad_lat[5] = "";
    char min_lon[10] = "", min_lat[10] = "";
    struct lat_lon posicion;
    char i, *pComa1;
    char *ppBuffer;

    ppBuffer = pBuffer;

    if(strstr(ppBuffer, header))
    {
        pComa1 = ppBuffer;
        // Busca el campo de la latitud
        for(i=0;i<3;i++)
        {
            pComa1 = strchr(++pComa1, ',');
        }
        pComa1++;

        // Lee la parte de grados
        for(i=0;i<2;i++)
        {
            grad_lat[i] = *pComa1++;
        }
        grad_lat[i] = "\n";
       // *pComa1++;

        // Lee la parte de minutos y decimas
        i=0;
        do{
            min_lat[i++] = *pComa1++;
        }while( *pComa1 != ',');
        min_lat[i] = "\n";
        posicion.lat = atof(grad_lat) + atof(min_lat)/60.0;


        pComa1++; //salteo la proxima coma para ver el signo
        if(*pComa1 == 'S')
        {
            posicion.lat = -posicion.lat;
        }
        pComa1++; //salteo el signo
        pComa1++; //salteo la proxima coma para llegar a la longitud

        // Lee los grados de longitud
        for(i=0;i<3;i++)
        {
            grad_lon[i] = *pComa1++;
        }
        grad_lon[i] = "\n";

        // Lee la parte de minutos y decimas
        i=0;
        do{
            min_lon[i++] = *pComa1++;
        }while( *pComa1 != ',');
        min_lon[i] = "\n";

        posicion.lon = atof(grad_lon) + atof(min_lon)/60.0;

        pComa1++; //salteo la proxima coma para ver el signo
        if(*pComa1 == 'W')
        {
            posicion.lon = -posicion.lon;
        }

    }
    return posicion;
}


/*
uint32_t diferenciaTiempo(tiempo_t t1, tiempo_t t2)
{
// Devuelve la diferencia de tiempo en segundos
// t2 tiene que ser mayor a t1
// No opera sobre el mes ni el año
    tiempo_t dif_tiempo;
    uint32_t dif_segundos;
    struct tm timeinfo;
    dif_tiempo.day     = t2.day     - t1.day;
    dif_tiempo.month   = t2.month   - t1.month;
    dif_tiempo.hour    = t2.hour    - t1.hour;
    dif_tiempo.minutes = t2.minutes - t1.minutes;
    dif_tiempo.seconds = t2.seconds - t1.seconds;


    dif_segundos  = dif_tiempo.seconds;
    dif_segundos += dif_tiempo.minutes*60;
    dif_segundos += dif_tiempo.hour*60*60;
    dif_segundos += dif_tiempo.day*60*60*24;
    //dif_segundos += dif_tiempo.month*60*60*24;
    //dif_segundos += dif_tiempo.year*60*60*24*365;

    return dif_segundos;
}
*/
