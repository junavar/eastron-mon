/*
 * registro.h
 *
 *  Created on: 17 Jan 2016
 *      Author: juan
 */

#ifndef SRC_REGISTRO_H_
#define SRC_REGISTRO_H_

#define SUBDIRECTORIO_REGISTRO  "/var/log/vigsol/"
#define FICHERO_REGISTRO "datosconsumo.txt"
#define FICHERO_INDICE_REGISTRO "indicedatosconsumo.ind"
#define MAX_PIDS_PARA_SIGNAL 4

//claves de los segmentos de memoria compartida
#define SHM_KEY		0x00001235  //identificador de memoria compartida para struct datos_instantaneos. Solo actualizada por eastron_mon

#define SHM_KEY_2 					0x00001234  //identificador de memoria compartida para struct datos_publicados.
#define SHM_KEY_DATOS_PUBLICADOS 	0x00001234  //identificador de memoria compartida para struct datos_publicados.

#define SHM_KEY_3 	0x00054321  //identificador de memoria compartida para tabla de subscripcion de notificaciones



struct entrada_registro{
	time_t 	energia_tiempo;
	float 	energia_imp_diferenciacion;
	float 	energia_exp_diferenciacion;
	float 	energia_reactiva_ind_diferenciacion; // reactiva inductiva (reactiva importada estaría mal expresado)
	float 	energia_reactiva_cap_diferenciacion; // reactiva capacitiva (reactiva exportada estaría mal expresado)

	int 	muestras_en_periodo; //muestras tipicamente 3600 (1 muestra por segundo durante una hora)

	float 	potencia_media;
	float 	potencia_max;
	time_t 	potencia_max_tiempo;
	float 	potencia_min;
	time_t 	potencia_min_tiempo;

	float 	potencia_reactiva_max;
	time_t 	potencia_reactiva_max_tiempo;
	float 	potencia_reactiva_min;
	time_t 	potencia_reactiva_min_tiempo;
	float 	tension_max;
	time_t 	tension_max_tiempo;
	float 	tension_min;
	time_t 	tension_min_tiempo;
	float 	frecuencia_max;
	time_t 	frecuencia_max_tiempo;
	float 	frecuencia_min;
	time_t 	frecuencia_min_tiempo;
	float 	factor_potencia_max;
	time_t 	factor_potencia_max_tiempo;
	float 	factor_potencia_min;
	time_t 	factor_potencia_min_tiempo;

	float   potencia_media_importada_15min_max;
	time_t 	potencia_media_importada_15min_max_tiempo;


	float	segundos_sin_tension;
	float	numero_cortes;

};


struct datos_instantaneos{
	time_t marca_tiempo;
	float potencia;
	float potencia_reactiva;
	float tension;
	float intensidad;
	float frecuencia;
	float factor_potencia;
	float energia_total_importada;
	float energia_total_exportada;
	float energia_reactiva_total_inductiva; // en lugar de importada
	float energia_reactiva_total_capacitiva;  // en lugar de exportada
	float potencia_media_importada_15min; // potencia media en una ventana maxima de 15 minutos
	int   ventana_integracion; // ventana en segundos empleados para la ventana de integración para potencia media

	int	  salto;
	int   retraso;

	unsigned char err_potencia;
	unsigned char err_potencia_reactiva;
	unsigned char err_tension;
	unsigned char err_intensidad;
	unsigned char err_frecuencia;
	unsigned char err_factor_potencia;
	unsigned char err_energia_total_importada;
	unsigned char err_energia_total_exportada;
	unsigned char err_energia_reactiva_total_inductiva;
	unsigned char err_energia_reactiva_total_capacitiva;

};

extern struct datos_instantaneos *pdatos_instantaneos;

struct entradaregistrodiario{

	int hora;
	int min;
	int offset; // desplazamiento zona horaria
	int tipo_registro; // 0 - vacio; -1 error ; 1 - normal- valido;  2 incompleto; 3 - interpolado

	float energia_imp;
	float energia_exp;
	float energia_consumida;
	float energia_generada;
	float energia_generable; // energia que se podia haber generado si no se hubiese aplicado limitación de exportación
	float potencia_max;
	float potencia_min;
};

//25 horas para el cambio de hora de invierno
#define NUM_INTERVALOS_DIA_15MIN 25*4

struct datos_publicados {

	char tiempo[26]; // 10 de fecha + 1 blanco(o "T") + 8 de tiempo + 6 de offset + /0
	float potencia_consumo;
	float tension_consumo;
	float intensidad_consumo;
	float frecuencia_consumo;
	float factor_potencia_consumo;
	float energia_total_consumo;
	//float energia_parcial_consumo;
	float energia_generada_dia;
	float potencia_generada;
	float limitacion_potencia_generada;
	float potencia_generable;
	//TODO poner variables que indique el numero de registros y posiblemente desde cual es valido
	// int num_entradas;
	// int entrada_inicial_valida
	struct entradaregistrodiario entradaregistrodiario[NUM_INTERVALOS_DIA_15MIN]; // se realiza una entrada por cada 1/4 de hora
};

extern struct datos_publicados *pdatos_publicados;
extern int indice_intervalo_15min;


extern pid_t *pid_para_signal;

struct linea_subscripcion{
	int pid;
	int rt_senal;
};
//extern struct linea_subscripcion linea_subscripcion, *tabla_subscripcion;

int registro_horario();
void funcion_a_ejecutar_cada_15min();
void funcion_a_ejecutar_cada_hora();
void funcion_a_ejecutar_cada_dia();
void consolidar_en_periodo();
void reiniciar_periodo();
int leer_energia_total_ultimo_registro();
float potencia_media_importada_15m();



#endif /* SRC_REGISTRO_H_ */
