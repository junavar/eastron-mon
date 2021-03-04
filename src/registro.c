//#define _POSIX_C_SOURCE 199309
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/file.h>
#include <string.h>
#include <stdio.h>
#include <argp.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include <float.h>


#include "registro.h"
#include "eastron-mon.h"

struct entrada_registro entrada_registro;

/*
 * Energias del intervalo 15min anterior
 */
float energia_imp_anterior=0.0;
float energia_exp_anterior=0.0;
float energia_reactiva_ind_anterior=0.0; //reactiva inductiva (mal dicho importada) anterior
float energia_reactiva_cap_anterior=0.0; //reactiva capacitiva (mal dicho exportada) anterior


char subdirectorio_datos[512]=SUBDIRECTORIO_REGISTRO;
char fichero_datos_consumo[512]=FICHERO_REGISTRO; //inicialmente solo fichero sin path, luego tras funcion leer_energia_total_ultimo_registro() contiene el path completo
char fichero_indice_datos_consumo[512]=FICHERO_INDICE_REGISTRO; //inicialmente solo fichero sin path, luego tras funcion leer_energia_total_ultimo_registro() contiene el path completo
/*
 * Se pone un valor más alto para permitir recuperar las energias
 * incluso cuando están presente las lineas de encabezados diarios de magnitudes
 */
char linea[1024]; //linea de registro de datos de consumo.
struct tm *ploc_time;
struct tm  loc_time; // empleado para copiar la estructura de datos apuntada por ploc_time
int gmtoff_arranque=0; //offset horario en segundos cuando arranca el inversor
int indice_intervalo_15min=0;

/*
 * Inserta por delante una cadena "ainsertar" en otra
 */
int insstr(char * ainsertar, char * cadena)
{
	int desplazar;
	int longitud;
	int i;
	longitud=strlen(cadena);
	desplazar=strlen(ainsertar);
	//printf("%s (%d)-->   %s (%d)\n", ainsertar, desplazar, cadena, longitud);

	cadena[longitud+desplazar]=cadena[longitud]; //se copia el /0 de final de string
	// se mueven los caracteres para dejar sitio a la inserción
	for (i=longitud-1; i>=0; i--){
		cadena[i+desplazar]=cadena[i];
		//printf("\n%s", cadena);
	}
	// se copia los caracteres a insertar en el hueco dejado
	for (i=desplazar-1; i>=0; i--){
			cadena[i]=ainsertar[i];
			//printf("\n%s", cadena);
		}

	//printf("\n");
	return 0;
}

/*
 * abre en modo append el fichero de registro contenido en la variable "fichero_datos_consumo"
 */
int abre_fichero_registro(){

	int fdatos; // file descriptor fichero de datos del inversor

	fdatos = open(fichero_datos_consumo, O_CREAT|O_APPEND|O_RDWR,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fdatos==-1){
		syslog(LOG_ERR, " No se puede abrir o crear el fichero %s ", fichero_datos_consumo);
	}
	else {
		syslog(LOG_INFO, "Abierto o creado el fichero %s ",	fichero_datos_consumo);
	}
	return fdatos;
}

/*
 * abre en modo append el fichero de indices a las lineas del registro contenido en la variable "fichero_indice_datos_consumo"
 */
int abre_fichero_indice_registro(){

	int fdatos; // file descriptor fichero de datos del inversor

	fdatos = open(fichero_indice_datos_consumo, O_CREAT|O_APPEND|O_RDWR,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fdatos==-1){
		syslog(LOG_ERR, " No se puede abrir o crear el fichero %s ", fichero_indice_datos_consumo);
	}
	else {
		syslog(LOG_INFO, "Abierto o creado el fichero %s ",	fichero_indice_datos_consumo);
	}
	return fdatos;
}

/*
 * Pinta las magnitudes en la cabecera del fichero
 */
void pinta_linea_magnitudes(int fdatos){
	sprintf(linea, "     dia         hora           EAi   EApe  ERpi  ERpc           EAti    EAte    ERti    ERtc           Muestras PAmaxp (m:s) PAminp (m:s) PRmaxp (m:s) PRminp (m:s) Vmaxp (m:s)  Vminp (m:s)  Fmaxp (m:s)   Fminp (m:s) P15max  (m:s)\n");
 	write(fdatos, linea, strlen(linea));
    sprintf(linea, "                                (kWh) (kWh)(kvarh)(kvarh)        (kWh)   (kWh)   (kvarh) (kvarh)                 (kW)         (kW)         (kvar)       (kvarh)      (V)          (V)          (Hz)          (Hz)         (W)\n");
    write(fdatos, linea, strlen(linea));
}

/*
 * Verifica que existe y en su caso crea un subdirectorio en variable global subdirectorios_datos ("/var/log/vigsol/")
 * Abre fichero en variable global fichero_datos_consumo (datosconsumo.txt)
 */
int leer_energia_total_ultimo_registro(){
	int fdatos; // file descriptor fichero de datos del inversor
	int leidos;
	struct stat file_info; //estructura para información de fichero de datos de consumo

	if (stat(subdirectorio_datos, &file_info) == -1) {
		if (mkdir(subdirectorio_datos, 0777)==-1){
			sprintf(linea_syslog, "Error %d al crear el subdirectorio %s", errno, subdirectorio_datos);
			syslog(LOG_ERR, " %s", linea_syslog);
			printf("%s\n", linea_syslog);
		}
		else{
			sprintf(linea_syslog, "Creado el subdirectorio %s", subdirectorio_datos);
			syslog(LOG_INFO, "%s", linea_syslog);
			printf("%s\n", linea_syslog);
		}
	}

	// pone el fichero_datos_consumo y en fichero_indice_datos_consumo el path completo
	insstr(subdirectorio_datos, fichero_datos_consumo);
	insstr(subdirectorio_datos, fichero_indice_datos_consumo);

	/*
	 * Abre o crea en modo append el fichero de registro de consumo
	 */

	fdatos=abre_fichero_registro();
	fstat (fdatos, &file_info);
	/*
	 * si el fichero esta vacio le pone cabecera con magnitudes
	 */
	if (file_info.st_size==0){
		pinta_linea_magnitudes(fdatos);
	}
	/*
	 * TODO: Interpolar resultados cuando existen huecos de horas de registro antes de reinicir las funiones de registro horario
	 */

	/*
	 * Accede y lee ultima linea de fichero
	 * y cierra fichero
	 */
	lseek(fdatos, -sizeof(linea), SEEK_END);
	leidos=read(fdatos, linea, sizeof(linea));
	if (leidos){}; //TODO control de error
	close(fdatos);

	/*
	 * busca últimos datos de energía
	 */
	char marcador[]="E_total: "; //String que sirve de marcador dentro del registro para localizar las energias totales del contador
	char *posicion;    // posicion localizada del marcador
	char *posicion_a;  // posicion de arranque
	char *posicion_b;  // posicion de backup
	posicion_a=linea;
	posicion_b=NULL;
	while (1){
		posicion=strstr(posicion_a,marcador);
	    //break;
		if(posicion==NULL){
			posicion=posicion_b; // recuperamos el valor almacenado
			break;
		}
		posicion_b=posicion;
		posicion_a=posicion+1;
	}

	if (posicion==NULL){
		sprintf(linea_syslog,"NO se han podido recuperar las energias anotadas del ultimo registro");
		syslog(LOG_ERR," %s", linea_syslog);
		printf("%s\n", linea_syslog);
		return -1;
	}

    posicion=posicion+sizeof(marcador);

	sscanf(posicion, "%f %f %f %f",
			&energia_imp_anterior,
			&energia_exp_anterior,
			&energia_reactiva_ind_anterior,
			&energia_reactiva_cap_anterior);

	sprintf(linea_syslog,"Recuperadas las energias anotadas del ultimo registro: %f %f %f %f",
			energia_imp_anterior,
			energia_exp_anterior,
			energia_reactiva_ind_anterior,
			energia_reactiva_cap_anterior);
	syslog(LOG_INFO, "%s", linea_syslog);
	printf("%s\n",linea_syslog);
	return 0;
}

/*
 * Calcula la potencia media en una ventana maxima de 15 minutos (900s)
 * Inicialmente va poblando un almacen circular de energias en cada segundo hasta completar 900 + 1 muestras
 * Si se produce una perdida de temporización (por sobrecarga de máquina) rellena las muestras correspondientes con valores interpolados
 */

float potencia_media_importada_15m(){

	#define VENTANA 900 // numero de muestras de potencia sobre las que se realiza la media (15min @ 1 muestra/s = 900 muestras)
	#define NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA (VENTANA+1) // 900 segundos (15 minutos) + una muestra adicional
	static float energia[NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA];
	static int ventana; // ventana de integracion en segundos que va aumentando hasta llegar al maximo
 	float incremento_energia;
	float potencia_media_15m;
	static int indice_actual;
	static time_t tiempo_anterior=0; //ultimo tiempo sobre el que se almacenan datos de tiempo y energia
	static float energia_anterior;
	int indice_referencia;
	int incremento_tiempo=0; //entre toma de muestras consecutivas

	indice_actual=pdatos_instantaneos->marca_tiempo%NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA;
	/*
	 * primera vez se identifica por tiempo_anterior=0
	 */
	if (tiempo_anterior==0){
		if (pdatos_instantaneos->err_energia_total_importada!=0) { // hasta que no se lea la primera energia total se devuelve 0w y no se hace nada
				return 0;
		}
		tiempo_anterior=pdatos_instantaneos->marca_tiempo;
		energia[indice_actual]=pdatos_instantaneos->energia_total_importada;
		incremento_tiempo=0;
	}
	/*
	 * todas las demas veces ...
	 */
	else{
		incremento_tiempo=(int)(pdatos_instantaneos->marca_tiempo-tiempo_anterior); // Incremento de tiempo en segundos entre tomas de muestras reales
		/*
		 * Se registran tiempos y energias rellenando huecos si es necesario
		 */
		for (int i=1; i<=incremento_tiempo;i++){
			/* Versión con interpolación lineal	 */
			energia[(tiempo_anterior +i)%NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA]=(pdatos_instantaneos->energia_total_importada*i + energia_anterior*(incremento_tiempo-i))/incremento_tiempo;
			//printf(" %8.3f  ", energia[(tiempo_anterior +i)%NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA]); // mensaje de depuración
		}
	}

	ventana+=incremento_tiempo;
	if (ventana > VENTANA) ventana=VENTANA;
	pdatos_instantaneos->ventana_integracion=ventana;

	indice_referencia=(indice_actual+NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA-ventana)%NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA;
	// al sumar NUM_MUESTRAS_ENERGIA_PARA_POTENCIA_MEDIA se asegura que el valor será siempre positivo

	incremento_energia=pdatos_instantaneos->energia_total_importada-energia[indice_referencia];

	if (ventana==0){
			potencia_media_15m=0; // se evita la division por cero
	}
	else {
		potencia_media_15m=incremento_energia*(1000*3600)/ventana;
	}
	//printf ("\niAct:%d  iRef:%d  Ener:%8.3f \n", indice_actual, indice_referencia, energia[indice_referencia]);// mensaje para depuración
	tiempo_anterior=pdatos_instantaneos->marca_tiempo;
	energia_anterior=pdatos_instantaneos->energia_total_importada;
	return  potencia_media_15m;
};

/*
 * esta variable global
 * indica que se ha llegado a registrar al menos un periodo (1hora)
 * desde el ultimo arranque
 */
int registrado_un_periodo;


// TODO: Verificar si se usa o debe usarse esta función
#if 1
void reiniciar_periodo(){

	entrada_registro.muestras_en_periodo=0;
	// la primera vez no se toman los datos del contador sino del registro anterior
	if(!registrado_un_periodo){
		leer_energia_total_ultimo_registro();
		//todo: verificar que si no se leen  las energia del registro (por ejemplo la primera vez) la cosa funciona
		//todo: verificar tiempos de registro y rellenar periodos vacios hasta que el arranque
	}
	else{
		energia_imp_anterior=pdatos_instantaneos->energia_total_importada;
		energia_exp_anterior=pdatos_instantaneos->energia_total_exportada;
		energia_reactiva_ind_anterior=pdatos_instantaneos->energia_reactiva_total_inductiva;
		energia_reactiva_cap_anterior=pdatos_instantaneos->energia_reactiva_total_capacitiva;
	}

	entrada_registro.potencia_max=-FLT_MAX;
	entrada_registro.potencia_max_tiempo=0;
	entrada_registro.potencia_min= FLT_MAX;
	entrada_registro.potencia_min_tiempo=0;

	entrada_registro.potencia_reactiva_max=-FLT_MAX;
	entrada_registro.potencia_reactiva_max_tiempo=0;
	entrada_registro.potencia_reactiva_min= FLT_MAX;
	entrada_registro.potencia_reactiva_min_tiempo=0;

	entrada_registro.tension_max=0;
	entrada_registro.tension_max_tiempo=0;
	entrada_registro.tension_min=FLT_MAX;
	entrada_registro.tension_min_tiempo=0;

	entrada_registro.frecuencia_max=0;
	entrada_registro.frecuencia_max_tiempo=0;
	entrada_registro.frecuencia_min=FLT_MAX;
	entrada_registro.frecuencia_min_tiempo=0;

	entrada_registro.potencia_media_importada_15min_max=0;
	entrada_registro.potencia_media_importada_15min_max_tiempo=0;

}
#endif

void accion_cada_segundo(){
	static int primera_muestra=0; //flag para controlar primera muestra
	int i;
	// la primera vez: toma energias iniciales y calcula el intervalo 15 minutos y pone el tiempo del intervalo en datos_publicados
	if (primera_muestra==0){

		gmtoff_arranque=loc_time.tm_gmtoff;

		energia_imp_anterior=pdatos_instantaneos->energia_total_importada;
		energia_exp_anterior=pdatos_instantaneos->energia_total_exportada;

		indice_intervalo_15min = (loc_time.tm_hour + loc_time.tm_gmtoff / SEGUNDOS_HORA - gmtoff_arranque / SEGUNDOS_HORA) * (MINUTOS_HORA / MINUTOS_INTERVALO)
						+ (loc_time.tm_min  + loc_time.tm_gmtoff % SEGUNDOS_HORA - gmtoff_arranque % SEGUNDOS_HORA) / MINUTOS_INTERVALO;
		//se marcan como registros incompletos los anteriores (salvo que un futuro se puedan recuperar de BD, o se interpolen)
		for (i=0; i<indice_intervalo_15min; i++){
			pdatos_publicados->entradaregistrodiario[i].tipo_registro=2;


			pdatos_publicados->entradaregistrodiario[i].hora=i/4;
			pdatos_publicados->entradaregistrodiario[i].min=(i%4) * 15;
			pdatos_publicados->entradaregistrodiario[i].offset=gmtoff_arranque / SEGUNDOS_MINUTO;


		}
		// datos de tiempo del intervalo
		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].tipo_registro=2; // se marca como incompleto ya que el programa puede arrancar en cualquier momento (no en limite de 15min)
		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].hora=loc_time.tm_hour;
		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].min=loc_time.tm_min;
		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].offset=loc_time.tm_gmtoff / SEGUNDOS_MINUTO;

		primera_muestra=1;
	}

	entrada_registro.muestras_en_periodo+=1;

	/*
	 * Cálculo de energia por diferenciación de energia contabilizada contador al principio y final del periodo
	 */
	//todo: controlar el caso de que se den la vuelta los contadores totales
	entrada_registro.energia_imp_diferenciacion=pdatos_instantaneos->energia_total_importada - energia_imp_anterior;
	entrada_registro.energia_exp_diferenciacion=pdatos_instantaneos->energia_total_exportada - energia_exp_anterior;
	entrada_registro.energia_reactiva_ind_diferenciacion=pdatos_instantaneos->energia_reactiva_total_inductiva - energia_reactiva_ind_anterior;
	entrada_registro.energia_reactiva_cap_diferenciacion=pdatos_instantaneos->energia_reactiva_total_capacitiva - energia_reactiva_cap_anterior;

	// Se anota en intervalos de datos _publicados expresado en watios*hora
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_imp = (pdatos_instantaneos->energia_total_importada - energia_imp_anterior)*1000; // en watios*hora
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_exp = (pdatos_instantaneos->energia_total_exportada - energia_exp_anterior)*1000; // en watios*hora
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_consumida =
			pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_imp +
			pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_generada; // no se multiplica por 1000 ya que los dos sumandos  ya están en watios*hora

	/*
	 * Calculo máxima y minimos de potencia activa
	 */
	if (pdatos_instantaneos->potencia > entrada_registro.potencia_max){
		entrada_registro.potencia_max = pdatos_instantaneos->potencia;
		entrada_registro.potencia_max_tiempo = pdatos_instantaneos->marca_tiempo;
	}
	if (pdatos_instantaneos->potencia < entrada_registro.potencia_min){
		entrada_registro.potencia_min = pdatos_instantaneos->potencia;
		entrada_registro.potencia_min_tiempo = pdatos_instantaneos->marca_tiempo;
	}

	/*
	 * Calculo máxima y minimos de potencia reactiva
	 */
	if (pdatos_instantaneos->potencia_reactiva > entrada_registro.potencia_reactiva_max){
		entrada_registro.potencia_reactiva_max = pdatos_instantaneos->potencia_reactiva;
		entrada_registro.potencia_reactiva_max_tiempo = pdatos_instantaneos->marca_tiempo;
	}
	if (pdatos_instantaneos->potencia_reactiva < entrada_registro.potencia_reactiva_min){
		entrada_registro.potencia_reactiva_min = pdatos_instantaneos->potencia_reactiva;
		entrada_registro.potencia_reactiva_min_tiempo = pdatos_instantaneos->marca_tiempo;
	}

	/*
	 * Cálculo máximo y mínimo de tensión
	 */
	if (pdatos_instantaneos->tension > entrada_registro.tension_max){
			entrada_registro.tension_max = pdatos_instantaneos->tension;
			entrada_registro.tension_max_tiempo = pdatos_instantaneos->marca_tiempo;
	}
	if (pdatos_instantaneos->tension < entrada_registro.tension_min){
		entrada_registro.tension_min = pdatos_instantaneos->tension;
		entrada_registro.tension_min_tiempo = pdatos_instantaneos->marca_tiempo;
	}

	/*
	 * calculo maximo y minimos de frecuencia
	 */
	if (pdatos_instantaneos->frecuencia > entrada_registro.frecuencia_max){
		entrada_registro.frecuencia_max = pdatos_instantaneos->frecuencia;
		entrada_registro.frecuencia_max_tiempo = pdatos_instantaneos->marca_tiempo;
	}
	if (pdatos_instantaneos->frecuencia < entrada_registro.frecuencia_min){
		entrada_registro.frecuencia_min = pdatos_instantaneos->frecuencia;
		entrada_registro.frecuencia_min_tiempo = pdatos_instantaneos->marca_tiempo;
	}

	/*
	 * calculo máximo potencia 15min
	 */
	if (pdatos_instantaneos->potencia_media_importada_15min > entrada_registro.potencia_media_importada_15min_max){
		entrada_registro.potencia_media_importada_15min_max= pdatos_instantaneos->potencia_media_importada_15min;
		entrada_registro.potencia_media_importada_15min_max_tiempo=pdatos_instantaneos->marca_tiempo;
	}
}

void accion_cada_15min(){

	energia_imp_anterior=pdatos_instantaneos->energia_total_importada;
	energia_exp_anterior=pdatos_instantaneos->energia_total_exportada;
	energia_reactiva_ind_anterior=pdatos_instantaneos->energia_reactiva_total_inductiva;
	energia_reactiva_cap_anterior=pdatos_instantaneos->energia_reactiva_total_capacitiva;

	//se calcula el intervalo de 15 min teniendo en cuenta los cambios DST que se produzcan en el año
	indice_intervalo_15min = (loc_time.tm_hour + loc_time.tm_gmtoff / SEGUNDOS_HORA - gmtoff_arranque / SEGUNDOS_HORA) * (MINUTOS_HORA / MINUTOS_INTERVALO)
					+ (loc_time.tm_min  + loc_time.tm_gmtoff % SEGUNDOS_HORA - gmtoff_arranque % SEGUNDOS_HORA) / MINUTOS_INTERVALO;

	// datos de tiempo de nuevo intervalo
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].hora=loc_time.tm_hour;
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].min=loc_time.tm_min;
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].offset=loc_time.tm_gmtoff / SEGUNDOS_MINUTO;
	pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].tipo_registro=1;
}

void accion_cada_hora(){
	int fdatos; // file descriptor fichero de datos del inversor
	char buf_tiempo[20]; //buffer para string de tiempo

	//TODO oner tiempo ISO con timezone
	strftime (buf_tiempo, sizeof(buf_tiempo), "%d-%m-%Y %H:%M:%S", &loc_time);


    // Tiempos de maximos y minimos
    char str_potencia_max_tiempo[10];
    strftime (str_potencia_max_tiempo, sizeof(str_potencia_max_tiempo), "%M:%S", localtime(&entrada_registro.potencia_max_tiempo));

    char str_potencia_min_tiempo[10];
    strftime (str_potencia_min_tiempo, sizeof(str_potencia_min_tiempo), "%M:%S", localtime(&entrada_registro.potencia_min_tiempo));

    char str_potencia_reactiva_max_tiempo[10];
    strftime (str_potencia_reactiva_max_tiempo, sizeof(str_potencia_reactiva_max_tiempo), "%M:%S", localtime(&entrada_registro.potencia_reactiva_max_tiempo));

    char str_potencia_reactiva_min_tiempo[10];
    strftime (str_potencia_reactiva_min_tiempo, sizeof(str_potencia_reactiva_min_tiempo), "%M:%S", localtime(&entrada_registro.potencia_reactiva_min_tiempo));

    char str_tension_max_tiempo[10];
    strftime (str_tension_max_tiempo, sizeof(str_tension_max_tiempo), "%M:%S", localtime(&entrada_registro.tension_max_tiempo));

    char str_tension_min_tiempo[10];
    strftime (str_tension_min_tiempo, sizeof(str_tension_min_tiempo), "%M:%S", localtime(&entrada_registro.tension_min_tiempo));

    char str_frecuencia_max_tiempo[10];
    strftime (str_frecuencia_max_tiempo, sizeof(str_frecuencia_max_tiempo), "%M:%S", localtime(&entrada_registro.frecuencia_max_tiempo));

    char str_frecuencia_min_tiempo[10];
    strftime (str_frecuencia_min_tiempo, sizeof(str_frecuencia_min_tiempo), "%M:%S", localtime(&entrada_registro.frecuencia_min_tiempo));

    char str_potencia_media_importada_15min_max_tiempo[10]; //maximo de 15minutos
    strftime (str_potencia_media_importada_15min_max_tiempo, sizeof(str_potencia_media_importada_15min_max_tiempo), "%M:%S", localtime(&entrada_registro.potencia_media_importada_15min_max_tiempo));

	sprintf(linea, "RH-> %s E_per: %05.3f %05.3f %05.3f %05.3f E_total: %07.1f %07.1f %07.1f %07.1f max_per: %4d %04.0f (%s) %04.0f (%s) %04.0f (%s) %04.0f (%s) %04.0f (%s) %04.0f (%s) %05.2f (%s) %05.2f (%s) %05.0f (%s)\n",
			buf_tiempo,

			entrada_registro.energia_imp_diferenciacion,
			entrada_registro.energia_exp_diferenciacion,
			entrada_registro.energia_reactiva_ind_diferenciacion,
			entrada_registro.energia_reactiva_cap_diferenciacion,

	        pdatos_instantaneos->energia_total_importada,
		    pdatos_instantaneos->energia_total_exportada,
		    pdatos_instantaneos->energia_reactiva_total_inductiva,
		    pdatos_instantaneos->energia_reactiva_total_capacitiva,

			entrada_registro.muestras_en_periodo,
			entrada_registro.potencia_max,
			str_potencia_max_tiempo,
			entrada_registro.potencia_min,
			str_potencia_min_tiempo,

			entrada_registro.potencia_reactiva_max,
			str_potencia_reactiva_max_tiempo,
			entrada_registro.potencia_reactiva_min,
			str_potencia_reactiva_min_tiempo,

			entrada_registro.tension_max,
			str_tension_max_tiempo,
			entrada_registro.tension_min,
			str_tension_min_tiempo,
			entrada_registro.frecuencia_max,
			str_frecuencia_max_tiempo,
			entrada_registro.frecuencia_min,
			str_frecuencia_min_tiempo,

			entrada_registro.potencia_media_importada_15min_max,
			str_potencia_media_importada_15min_max_tiempo);

	fdatos= abre_fichero_registro();
	//TODO escribir este error en stderr y en syslog
	int escritos=0;
	escritos=write(fdatos, linea, strlen(linea));
	if (escritos<0) {
		printf("\nError escritura en FICHERO_REGISTRO. errno: %d\n", errno);
	}
	close(fdatos);
	printf("\r%s", linea); // asegura principio de linea y escribe la linea de registro por pantalla
	registrado_un_periodo=1; //Indica que se ha registrado al menos un periodo desde que arrancó el programa
}

/*
 * al inicio de cada dia:
 * Añade una línea de cabecera diaria en el fichero de datos de consumo
 * Incluye la posicion del cursor del fichero de datos de consumo en el fichero de indice
 *
 */
void accion_cada_dia(){
	int fdatos;// file descriptor fichero de datos del inversor
	int findice; // file descriptor fichero de indice a datos del inversor
	int posicion; //posicion del cursor del fichero
	char buf_tiempo[20]; //buffer para string de tiempo
	char linea[24]; // buffer linea de fichero

	// determina el gmt_offset inicial
	gmtoff_arranque=loc_time.tm_gmtoff;

	// resetea el registro cuartohorario del dia y su indice;
	int e;
	for (e=0; e<NUM_INTERVALOS_DIA_15MIN; e++){

		pdatos_publicados->entradaregistrodiario[e].tipo_registro   	=0;
		pdatos_publicados->entradaregistrodiario[e].hora				=0;
		pdatos_publicados->entradaregistrodiario[e].min					=0;
		pdatos_publicados->entradaregistrodiario[e].offset				=0;
		pdatos_publicados->entradaregistrodiario[e].energia_generada 	=0;
		pdatos_publicados->entradaregistrodiario[e].energia_consumida	=0;
		pdatos_publicados->entradaregistrodiario[e].energia_exp      	=0;
		pdatos_publicados->entradaregistrodiario[e].energia_imp 		=0;

	}

	indice_intervalo_15min=0;

	fdatos=abre_fichero_registro();
	findice= abre_fichero_indice_registro();
	pinta_linea_magnitudes(fdatos);

	strftime (buf_tiempo, sizeof(buf_tiempo), "%d-%m-%Y", &loc_time);
	posicion = lseek(fdatos, 0, SEEK_CUR);
	sprintf(linea,"%s %012d\n",buf_tiempo, posicion);
	write(findice, linea, sizeof(linea));
	close(fdatos);
	close(findice);
}



