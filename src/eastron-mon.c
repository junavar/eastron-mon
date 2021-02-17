#include <syslog.h>
#include <unistd.h>
#include <stdint.h>
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
#include <pthread.h>
#include <sys/timerfd.h>
#include <sys/ipc.h>
#include <sys/shm.h>



#include "modbus-rtu-static.h"
#include "registro.h"
#include "eastron-mon.h"

#define BUF_SIZE 100/* Large enough to hold maximum PID as string */

// punteros a areas de memoria compartida
struct datos_instantaneos *pdatos_instantaneos;
struct linea_subscripcion *plinea_subscripcion;
struct datos_publicados *pdatos_publicados;

extern struct entrada_registro entrada_registro;

static int recibido_senal_fin=0;

/* This program uses argp
 to be compliant with the GNU standard command line format.
 and uses options and arguments.

 un buen tutorial del uso de argp se puede consultar en
 http://ftp.yzu.edu.tw/nongnu/argpbook/step-by-step-into-argp.pdf
 http://ftp.yzu.edu.tw/nongnu/argpbook/step-by-step-into-argp.pdf
 In addition to making sure no arguments are given, and
 implementing a –help option, this example will have a
 –version option, and will put the given documentation string
 and bug address in the –help output, as per GNU standards.

 The variable ARGP contains the argument parser specification;
 adding fields to this structure is the way most parameters are
 passed to argp_parse (the first three fields are usually used,
 but not in this small program).  There are also two global
 variables that argp knows about defined here,
 ARGP_PROGRAM_VERSION and ARGP_PROGRAM_BUG_ADDRESS (they are
 global variables because they will almost always be constant
 for a given program, even if it uses different argument
 parsers for various tasks).

 Use the first four fields in ARGP, so here’s a description of them:
 OPTIONS  – A pointer to a vector of struct argp_option (see below)
 PARSER   – A function to parse a single option, called by argp
 ARGS_DOC – A string describing how the non-option arguments should look
 DOC      – A descriptive string about this program; if it contains a
 vertical tab character (\v), the part after it will be
 printed *following* the options

 The function PARSER takes the following arguments:
 KEY  – An integer specifying which option this is (taken
 from the KEY field in each struct argp_option), or
 a special key specifying something else; the only
 special keys we use here are ARGP_KEY_ARG, meaning
 a non-option argument, and ARGP_KEY_END, meaning
 that all arguments have been parsed
 ARG  – For an option KEY, the string value of its
 argument, or NULL if it has none
 STATE– A pointer to a struct argp_state, containing
 various useful information about the parsing state; used here
 are the INPUT field, which reflects the INPUT argument to
 argp_parse, and the ARG_NUM field, which is the number of the
 current non-option argument being parsed
 It should return either 0, meaning success, ARGP_ERR_UNKNOWN, meaning the
 given KEY wasn’t recognized, or an errno value indicating some other
 error.

 Note that in this example, main uses a structure to communicate with the
 parse_opt function, a pointer to which it passes in the INPUT argument to
 argp_parse.  Of course, it’s also possible to use global variables
 instead, but this is somewhat more flexible.

 The OPTIONS field contains a pointer to a vector of struct argp_option’s;
 that structure has the following fields (if you assign your option
 structures using array initialization like this example, unspecified
 fields will be defaulted to 0, and need not be specified):
 NAME   – The name of this option’s long option (may be zero)
 KEY	– The KEY to pass to the PARSER function when parsing this option,
 *and* the name of this option’s short option, if it is a printable ascii character
 ARG    – The name of this option’s argument, if any
 FLAGS	– Flags describing this option; some of them are:
 OPTION_ARG_OPTIONAL – The argument to this option is optional
 OPTION_ALIAS        – This option is an alias for the  previous option
 OPTION_HIDDEN       – Don’t show this option in –help output
 DOC    – A documentation string for this option, shown in –help output

 An options vector should be terminated by an option with all fields zero. */

const char *argp_program_version = VERSION_PROGRAMA; // --version visualiza esta string
const char *argp_program_bug_address = "<juan_navarro_garcia@hotmail.com>"; // saca una referencia a que se reporten bugs en --help

/* Program documentation. */
static char doc[] =
		"eastron-mon -- a program/daemon for monitoring AC electric energy parameters using Eastron modbus-rtu meter\nAuthor: Juan Navarro\nDic 2018"; // -help visualiza esta string

/* A description of the arguments we accept. */
static char args_doc[] = "MODBUS_DEVICE_FILE";

/* The options we understand. */
static struct argp_option options[] = {
		{"slave", 's', "SLAVE_ID", 0, "Modbus Slave id to monitor (default: 01)" },
		{"baud_rate", 'b', "SPEED", 0, "Serial baud rate: 9600|4800|2400 (default)|1200 bps" },
		{"wait_time", 'w', "WAIT_TIME", 0, "Wait time after write request  in microseconds (default: baud_rate dependent)" },
		{"process", 'p', 0, 0, "Execute as a foreground normal process, not as a background daemon" },
		{"verbose", 'v', 0, 0, "Produce verbose output" },
		{ "quiet", 'q', 0, 0, "Don't produce any output" },
		{ 0 }
};

/* Used by main to communicate with parse_opt. */
struct arguments {
	char *args[1]; /* MODBUS_DEVICE_FILE (arg1) */
	int slave;
	int speed;
	int process;
	int wait_time;
	int quiet, verbose;
};

char linea_syslog[512]; // linea de registro de syslog

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
	/* Get the input argument from argp_parse, which we
	 know is a pointer to our arguments structure. */
	struct arguments *arguments = state->input;
	switch (key) {
	case 's':

		if (atoi(arg) > 01 && atoi(arg) < 256) {
			arguments->slave = atoi(arg);
		} else {
			arguments->slave = 01;
		}
		break;
	case 'b':
		/*
		 * solo se aceptan velocidades de 9600,4800,2400 o 1200bps. En caso de no coincidencia se ajusta la velociad por defecto a 9600
		 */
		arguments->speed = atoi(arg);
		if (!(arguments->speed == 9600 || arguments->speed == 4800
				|| arguments->speed == 2400 || arguments->speed == 1200)) {
			arguments->speed = 2400;
		}
		break;
	case 'w':
		arguments->wait_time=atoi(arg);
		break;
	case 'p':
		arguments->process = 1;
		break;

	case 'q':
		arguments->quiet = 1;
		break;
	case 'v':
		arguments->verbose = 1;
		break;

	case ARGP_KEY_ARG:
		if (state->arg_num >= 1)
			argp_usage(state);/* Too many arguments. */
		arguments->args[state->arg_num] = arg;
		break;

	case ARGP_KEY_END:
		if (state->arg_num < 1)
			argp_usage(state); /* Not enough arguments. */

		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

/*
 * descriptor del fichero de lock con el PID de proceso que indica que el proceso ya se está ejecutando
 */
int fd_lock;

/*
 * se ejecuta cuando se recibe la señal SIGHUP.
 * En el caso de daemons esta señal se emplea para indicar el reincio o relectura de la configuración del daemon
 */
void reload() {
	syslog(LOG_INFO,
			"Reinicio o recarga de configuración por recepción de señal SIGHUP");
	/*
	 * Pendiente de implementar
	 */
}

/*
 * se ejecuta cuando se recibe la señal SIGTERM.
 * Esta señal se emplea para indicar la terminación ordenada del proceso
 */
void terminar() {

	sprintf(linea_syslog,"Terminación ordenada por recepción de señal SIGTERM o SIGINT");
	fprintf(stderr, "\n%s\n",linea_syslog);
	syslog(LOG_INFO, "%s", linea_syslog);

	recibido_senal_fin=1;
}


/*
 * Obtiene el siguiente segundo sobre el tiempo real desde el momento actual
 */
void siguiente_segundo (struct timeval *tv_siguiente_segundo){
	struct timeval tv_actual;
	struct tm *ptiempo_bd, tiempo_bd;
	/* obtiene el tiempo actual */
	gettimeofday(&tv_actual, NULL);
	/* Descompone el tiempo y obtiene siguiente segundo*/
	ptiempo_bd=localtime(&tv_actual.tv_sec);
	tiempo_bd=*ptiempo_bd;
	tiempo_bd.tm_sec+=1;
	tv_siguiente_segundo->tv_sec=mktime(&tiempo_bd);
	tv_siguiente_segundo->tv_usec=0;
}


/*
 * pasa a flotante los 2 registros de 16 bits que devuelve libmodbus.
 */
float pasar_4_bytes_a_float(unsigned char * buffer) {
	unsigned char tmp[4];

	tmp[3] = buffer[1];
	tmp[2] = buffer[0];
	tmp[1] = buffer[3];
	tmp[0] = buffer[2];

	//return *(float *) (tmp);   da el warning: dereferencing type-punned pointer will break strict-aliasing rules [-Wstrict-aliasing]
	//return *(float *) (unsigned long *) (tmp);da el warning: dereferencing type-punned pointer will break strict-aliasing rules [-Wstrict-aliasing]

	float *pvalor;
	pvalor=(float *)tmp;
	return *pvalor;
}

void cerrar_modbus(modbus_t *ctx){
	if (!ctx){
		modbus_close(ctx);
		modbus_free(ctx);
	}
}

modbus_t * abrir_modbus(char * dev, int velocidad, int slave){
	modbus_t *ctx;

	/*
	 * construye estructura para el canal serie para modbus-rtu
	 * · device serie que es el master del modbus que va el primer argumento del programa/daemon,
	 * · la velocidad
	 * Observesé que no se especifica en esta funcion el slave.
	 */
	ctx = modbus_new_rtu(dev, velocidad, 'N', 8, 1);
	if (!ctx) {
		sprintf(linea_syslog,"Fallo en la creacion de estructura interna : %s ",
				modbus_strerror(errno));
				fprintf(stderr,"%s\n",linea_syslog);
				syslog(LOG_ERR," %s",linea_syslog);
		return 0;
	}

	// abre fichero del puerto serie especificado en la estructura  y ajusta parametros de comunicacion serie
	if (modbus_connect(ctx) == -1) {

		sprintf(linea_syslog,"No se ha podido abrir correctamente la comunicacion modbus a traves de %s. errno: %s",
						dev, modbus_strerror(errno));
		fprintf(stderr,"%s\n",linea_syslog);
		syslog(LOG_ERR, " %s",linea_syslog);
		modbus_free(ctx);
		return 0;
	}

	//Especifica el slave
	int rc;
	rc=modbus_set_slave(ctx, slave);
	if (rc == -1) {

		sprintf(linea_syslog,"Dispositivo slave invalido %d ", slave);
		fprintf(stderr,"%s\n",linea_syslog);
		syslog(LOG_ERR, " %s",linea_syslog);
		modbus_close(ctx);
	    modbus_free(ctx);
	    return 0;
	}
	sprintf(linea_syslog,"Preparada conexion a modbus device %s  velocidad %d slave %d",dev, velocidad, slave);
	fprintf(stderr,"%s\n",linea_syslog);
	syslog(LOG_INFO, " %s",linea_syslog);
	return ctx;
}

int main(int argc, char **argv) {

	struct arguments arguments;
	int bloqueo;
	int escritos = 0;
	int esdaemon = 1;
	char cadena_pid[BUF_SIZE] = "0000000000";
	char servicename[] = "eastron-mon"; //nombre del programa o demonio que se consignará en los mensajes de sylog()
	char filelockname[BUF_SIZE]; //Nombre fichero que sirve para indicar que el proceso ya está funcionando. Contiene el PID del proceso.
	int err_rcp=0;  // sirve para señalar que alguna trama del turno ha presentado un error


	/* Default values. */
	arguments.args[0] = "/dev/ttyUSB0";
	arguments.slave = 01;
	arguments.speed = 2400;
	arguments.wait_time=0;
	arguments.process = 0;
	arguments.quiet = 0;
	arguments.verbose = 0;

	/* Parse our arguments; every option seen by parse_opt will
	 be reflected in arguments. */
	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	fprintf(stderr, "%s v%s %s", NOMBRE_PROGRAMA, VERSION_PROGRAMA, FECHA_PROGRAMA);
	fprintf(stderr, "\n%s", TEXTO_PROGRAMA);
	fprintf(stderr, "\n%s", AUTOR_PROGRAMA);



	fprintf(stderr, "\nMODBUS_DEVICE_FILE = %s; SLAVE= %d BAUDRATE= %d  WAIT_TIME= %d process_foreground = %s",
			arguments.args[0], arguments.slave, arguments.speed, arguments.wait_time,
			arguments.process ? "yes" : "no");


	openlog(servicename, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);
	sprintf(filelockname, "%s%s.pid", FILE_LOCK_NAME_LOCATION, servicename);
	fd_lock = open(filelockname,    O_RDWR | O_CREAT, O_SYNC | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	/*
	 * Se pone el modo =O_SYNC para asegurar la escritura efectiva a disco tras el write sin tener que llamar a la funcion fsync()
	 * No se pone =0_TRUNC porque siempre borra el fichero, incluso cuando ya hay una instancia del proceso corriendo,
	 * con el fichero con el bloqueo y el PID us escrito en él.
	 */
	if (fd_lock == -1) {

		sprintf(linea_syslog,"No se puede abrir o crear fichero de lock. Intente bajo usuario root");
		fprintf(stderr, "\nError:%s\n",linea_syslog);
		syslog(LOG_ERR," %s", linea_syslog);
		return -1;
	}
	bloqueo = flock(fd_lock, LOCK_EX | LOCK_NB);
	/*
	 * este bloqueo es para el fichero completo
	 * esta asociado al file descriptor (no al proceso)
	 * el proceso hijo hereda el bloqueo
	 */
	if (bloqueo == -1) {
		sprintf(linea_syslog,"El servicio ya esta ejecutandose");
		fprintf(stderr, "\n%s\n",linea_syslog);
		syslog(LOG_ERR, "%s", linea_syslog);
		return -1;
	}

	esdaemon = !arguments.process;  	// 0 -> evita la conversion en daemon
										// 1 -> realiza la conversion a daemon

	if (esdaemon) {
		sprintf(linea_syslog, "Service %s (v%s) arrancado con PID:%d por usuario:%d antes de convertirse en demonio",
				servicename, VERSION_PROGRAMA, getpid(), getuid());
		fprintf(stderr, "\n%s\n",linea_syslog);
		syslog(LOG_INFO,"%s",linea_syslog);
		daemon(0, 0);

		/*
		 * Captura las señales SIGHUP y SIGTERM (señal por defecto del comando kill)
		 */
		signal(SIGHUP, reload);
		signal(SIGTERM, terminar);

		syslog(LOG_INFO, "Convertido en daemon with PID: %d", getpid());
	} else {
		sprintf(linea_syslog,"Service %s (v%s) arrancado como proceso normal with PID: %d by User: %d\n",
				servicename, VERSION_PROGRAMA, getpid(), getuid());
		fprintf(stderr,"\n%s",linea_syslog);
		syslog(LOG_INFO,"%s",linea_syslog);

		/*
		 * Captura las señales SIGTERM (comando kill por defecto) y SIGINT (ctrl-c) para hacer un cierre ordenado
		 */
		signal(SIGTERM, terminar);
		signal(SIGINT, terminar);
	}



	// escribe el PID en el fichero de bloqueo.
	// Se pone aquí para asegurar que el PID de proceso child si es un daemon
	// o del proceso padre si no se ejecuta como proceso normal
	sprintf(cadena_pid, "%ld", (long) getpid());
	int a_escribir;
	a_escribir = strlen(cadena_pid);
	ftruncate(fd_lock, 0);
	escritos = write(fd_lock, cadena_pid, a_escribir);
	if (escritos != a_escribir) {
		syslog(LOG_ERR, " No se ha podido escribir el PID en el fichero de bloqueo");
	} else {

		syslog(LOG_INFO,
				"El PID %s del proceso ha sido escrito en el fichero %s (%d bytes)",
				cadena_pid, filelockname, a_escribir);
	}



	modbus_t *ctx;
	ctx = abrir_modbus(arguments.args[0], arguments.speed, arguments.slave);
	int rc=0;

	/*
	 * crea areas de memoria compartida
	 */

	/*
	 * Para datos instantaneo a publicar
	 */
	int shmid; // identificador de memoria compartida
	shmid = shmget(SHM_KEY, sizeof (struct datos_instantaneos), IPC_CREAT | 0666);
    pdatos_instantaneos = shmat(shmid, NULL, 0);
	/*
	 * Para pids para notificar
	 */
	int shmid3; // identificador de memoria compartida
	shmid3 = shmget(SHM_KEY_3, sizeof (struct linea_subscripcion)*MAX_PIDS_PARA_SIGNAL, IPC_CREAT | 0666);
	plinea_subscripcion = shmat(shmid3, NULL, 0);

	/*
	 * La empleada por fronius-mon
	*/
		int shmid2;
		shmid2 = shmget(SHM_KEY_2, sizeof (struct datos_publicados), IPC_CREAT | 0666);
		pdatos_publicados = shmat(shmid2, NULL, 0);

	/*
	 * threads de cada 15min, cada hora y cada dia
	 */
	pthread_t t1, t2, t3 ;
	int s1, s2, s3;
	s3=pthread_create(&t3, NULL, (void *)funcion_a_ejecutar_cada_15min, NULL);
		if(s3){}
	s2=pthread_create(&t2, NULL, (void *)funcion_a_ejecutar_cada_hora, NULL);
	if(s2){}
	s1=pthread_create(&t1, NULL, (void *)funcion_a_ejecutar_cada_dia, NULL);
	if(s1){}



	/*
	 * Temporizador de cada segundo
	 */
	int fd_timer_segundo; //
	fd_timer_segundo = timerfd_create(CLOCK_REALTIME, 0);

	struct timeval tiempo;
	struct itimerspec ts;
	int s;
	uint64_t numExp=0;
	/*
	 * alinea el inicio del temporizador con el inicio de segundo de tiempo real
	 */
	siguiente_segundo(&tiempo);
	ts.it_value.tv_sec=tiempo.tv_sec;
	ts.it_value.tv_nsec=0;
	ts.it_interval.tv_sec=0;
	ts.it_interval.tv_nsec=0;
	if (timerfd_settime(fd_timer_segundo, TFD_TIMER_ABSTIME, &ts, NULL) == -1){
		/*TODO error*/

		printf("\nError en timerfd_settime()");
	}

	/*
	 * reajuste de temporizador periodico a 1 segundo
	 */
	struct itimerspec timer = {
	        .it_interval = {1, 0},  /* segundos y nanosegundos  */
	        .it_value    = {1, 0},
	};

	if (timerfd_settime(fd_timer_segundo, 0, &timer, NULL) == -1){
		/*TODO error*/
		printf("\nError en timerfd_settime()");
	}

	/*
	 * Variables para control de solicitud de parametros en cada ciclo
	 */
	int sol_potencia=0;
	int sol_potencia_reactiva=0;
	int sol_factor_potencia=0;
	int sol_tension=0;
	int sol_intensidad=0;
	int sol_frecuencia=0;
	int sol_energia_total_importada=0;
	int sol_energia_total_exportada=0;
	int sol_energia_reactiva_total_inductiva=0;
	int sol_energia_reactiva_total_capacitiva=0;

	sol_potencia=1;
	sol_potencia_reactiva=1;
	sol_factor_potencia=1;
	sol_tension=1;
	sol_intensidad=1;
	sol_frecuencia=1;
	sol_energia_total_importada=1;
	sol_energia_total_exportada=1;
	sol_energia_reactiva_total_inductiva=1;
	sol_energia_reactiva_total_capacitiva=1;

	while (!recibido_senal_fin) {
		/*
		* la ejecucion queda suspendida en la función read() hasta que el temporizador se dispare (alcance el nuevo segundo)
		*/
		s = read(fd_timer_segundo, &numExp, sizeof(uint64_t));
		if (s != sizeof(uint64_t)) {
			/* TODO error*/
			printf("\nError en read() de fd_timer_segundo");
		}

		pdatos_instantaneos->retraso=numExp-1; //falta de puntualidad es el numero de turnos expirados - 1


		struct tm *loc_time;
		char buf[150]; //buffer para string de tiempo

		/*
		 *  tiempo de solicitud al medidor
		 */

		pdatos_instantaneos->marca_tiempo= time(NULL);
		loc_time = localtime(&pdatos_instantaneos->marca_tiempo); // Converting current time to local time
		int hora;
		int minuto;
		hora = loc_time->tm_hour;
		minuto = loc_time->tm_min;
		indice_intervalo_15min = hora*4 + minuto/15;

		strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", loc_time);
		snprintf(pdatos_publicados->tiempo,sizeof(pdatos_publicados->tiempo), "%s", buf);


		if (numExp >1) {
			sprintf(linea_syslog,"Perdido el turno de lecturas numExp=%d con el dispositivo modbus %d",
							(int)numExp,  arguments.slave);
					fprintf(stderr,"%s\n",linea_syslog);
					syslog(LOG_INFO,"%s",linea_syslog);
		}

		// pinta el tiempo en el momento de hacer la solicitud y si ha habido saltos de turno
		if (arguments.process){
			printf("%s ",buf);
			printf("faltas:%1d ", pdatos_instantaneos->retraso);
			fflush(stdout);
		}

		if (ctx==0){
				fprintf(stderr, "\nreabriendo modbus\n");
				ctx=abrir_modbus(arguments.args[0], arguments.speed,arguments.slave);
				continue;
		}

		// se resetea el indicador de error global
		err_rcp=0;

		int registro;
		int registro_inicial;
		short num_registros_lectura;
		int num;
		int rc1, rc2;



		/*
		 *  Primer grupo de 16 valores = 32 registros de 16bits modbus
		 *  que contiene los registros de potencia (0x0C), tension (0x00), intensidad (0x06),
		 *  potencia reactiva (0x18) y factor_potencia (0x1E)
		 */

		registro_inicial=0x00;
		num_registros_lectura=32;
		uint16_t buff_receive[num_registros_lectura*sizeof(uint16_t)]; // will store read registers values

		memset(buff_receive, 0, sizeof(buff_receive));
		rc1=0;

		//Read holding registers starting from address 0x00 (tension)
		num = modbus_read_input_registers(ctx, 0x00, num_registros_lectura, buff_receive);
		if (num != num_registros_lectura) { // number of read registers is not the one expected
			rc1=1;
			sprintf(linea_syslog,"Fallo de lectura: %s ", modbus_strerror(errno));
			fprintf(stderr,"%s\n",linea_syslog);
			syslog(LOG_ERR, " %s",linea_syslog);
			//TODO control de registros recurrentes en syslog
			//TODO contar errores

			printf("err:1");
			fflush(stdout);

			cerrar_modbus(ctx);//cierra para reabrir en siguiente iteracion
			fprintf(stderr, "\nreabriendo 1 modbus\n");
			ctx=abrir_modbus(arguments.args[0], arguments.speed,arguments.slave);
			continue;

		}

		if (sol_potencia==1){
			pdatos_instantaneos->err_potencia=rc; /*potencia activa registro 0x0C*/
			if (pdatos_instantaneos->err_potencia==0){
				registro=0x0C;

				pdatos_instantaneos->potencia=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
				pdatos_publicados->potencia_consumo=pdatos_instantaneos->potencia + pdatos_publicados->potencia_generada;

#if 0
				//en todo caso
				pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_consumida += pdatos_publicados->potencia_consumo * (pdatos_instantaneos->retraso + 1);


				//energia en watios*segundo
				if (pdatos_instantaneos->potencia>=0){  // hay importación
					pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_imp += pdatos_instantaneos->potencia * (pdatos_instantaneos->retraso + 1);
				}
				else{ // hay exportación
					pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_exp += -pdatos_instantaneos->potencia * (pdatos_instantaneos->retraso + 1);
				}
#endif
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_potencia==0)?0:1;
		}

		if (sol_tension==1){
			pdatos_instantaneos->err_tension=rc; /*tension registro 0x00*/
			if (pdatos_instantaneos->err_tension==0){
				registro=0x00;
				pdatos_instantaneos->tension=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
				pdatos_publicados->tension_consumo=pdatos_instantaneos->tension;
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_tension==0)?0:1;

		}

		if (sol_intensidad==1){
			pdatos_instantaneos->err_intensidad=rc; /*intensidad registro 0x06*/
			if (pdatos_instantaneos->err_intensidad==0){
				registro=0x06;
				pdatos_instantaneos->intensidad=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
				//TODO sacar intensidad degeneración del inversor en lugar de calcularla a partir de la potencia de generación.
				pdatos_publicados->intensidad_consumo = pdatos_instantaneos->intensidad +
						pdatos_publicados->potencia_generada/pdatos_publicados->tension_consumo;
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_intensidad==0)?0:1;
		}

		if (sol_factor_potencia==1){
			pdatos_instantaneos->err_factor_potencia=rc; /*factor_potencia registro 0x1E*/
			if (pdatos_instantaneos->err_factor_potencia==0){
				registro=0x1E;
				pdatos_instantaneos->factor_potencia=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
				pdatos_publicados->factor_potencia_consumo=pdatos_instantaneos->factor_potencia;
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_factor_potencia==0)?0:1;
		}

		if (sol_potencia_reactiva==1){
			pdatos_instantaneos->err_potencia_reactiva=rc; /*potencia_reactiva 0x18*/
			if (pdatos_instantaneos->err_potencia_reactiva==0){
				registro=0x18;
				pdatos_instantaneos->potencia_reactiva=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_potencia_reactiva==0)?0:1;
		}


		usleep(100000);
		/*
		 * segundo grupo de lectura que contiene la frecuencia (0x46),
		 * la energia importada(0x48), energia exportada (0x4A),
		 * la energia_reactiva_total_inductiva (0x4C), la  energia_reactiva_total_capacitiva (0x4E)
		 */

		registro_inicial=0x46;
		num_registros_lectura=12;

		memset(buff_receive, 0, sizeof(buff_receive));
		rc2=0;

		//Read holding registers starting from address 0x46 (frecuencia)
		num = modbus_read_input_registers(ctx, registro_inicial, num_registros_lectura, buff_receive);
		if (num != num_registros_lectura) { // number of read registers is not the one expected
			rc2=1;
			sprintf(linea_syslog,"Fallo de lectura: %s ", modbus_strerror(errno));
			fprintf(stderr,"%s\n",linea_syslog);
			syslog(LOG_ERR, " %s",linea_syslog);
			//TODO control de registros recurrentes en syslog
			//TODO contar errores

			printf("1 ");
			fflush(stdout);

			cerrar_modbus(ctx); //cierra para reabrir en siguiente iteracion
			fprintf(stderr, "\nreabriendo 2 modbus\n");
			ctx=abrir_modbus(arguments.args[0], arguments.speed,arguments.slave);
			continue;

		}

		if (sol_frecuencia==1){
			pdatos_instantaneos->err_frecuencia=rc; /*frecuencia registro 0x46*/
			if (pdatos_instantaneos->err_frecuencia==0){
				registro=0x46;
				pdatos_instantaneos->frecuencia=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
				pdatos_publicados->frecuencia_consumo=pdatos_instantaneos->frecuencia;
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_frecuencia==0)?0:1;
		}

		if (sol_energia_total_importada==1){
			pdatos_instantaneos->err_energia_total_importada=rc; /*energia_total_importada registro 0x48*/
			if (pdatos_instantaneos->err_energia_total_importada==0){
				registro=0x48;
				pdatos_instantaneos->energia_total_importada=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
				pdatos_publicados->energia_total_consumo=(int)pdatos_instantaneos->energia_total_importada;
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_energia_total_importada==0)?0:1;
		}

		if (sol_energia_total_exportada==1){
			pdatos_instantaneos->err_energia_total_exportada=rc; /*energia_total_exportada registro 0x4A*/
			if (pdatos_instantaneos->err_energia_total_exportada==0){
				registro=0x4A;
				pdatos_instantaneos->energia_total_exportada=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_energia_total_exportada==0)?0:1;
		}

		if (sol_energia_reactiva_total_inductiva==1){
			pdatos_instantaneos->err_energia_reactiva_total_inductiva=rc; /*energia_reactiva_total_inductiva registro 0x4C*/
			if (pdatos_instantaneos->err_energia_reactiva_total_inductiva==0){
				registro=0x48;
				pdatos_instantaneos->energia_reactiva_total_inductiva=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_energia_reactiva_total_inductiva==0)?0:1;
		}

		if (sol_energia_reactiva_total_capacitiva==1){
			pdatos_instantaneos->err_energia_reactiva_total_capacitiva=rc; /*energia_reactiva_total_capacitiva registro 0x4E*/
			if (pdatos_instantaneos->err_energia_reactiva_total_capacitiva==0){
				registro=0x4E;
				pdatos_instantaneos->energia_reactiva_total_capacitiva=pasar_4_bytes_a_float((unsigned char *)&buff_receive[registro-registro_inicial]);
			}
			if (err_rcp==0) err_rcp=(pdatos_instantaneos->err_energia_reactiva_total_capacitiva==0)?0:1;
		}


		pdatos_instantaneos->potencia_media_importada_15min=potencia_media_importada_15m();
		consolidar_en_periodo();

		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_imp = entrada_registro.energia_imp_diferenciacion;
		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_exp = entrada_registro.energia_exp_diferenciacion;
		pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_consumida =
				pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_imp +
				pdatos_publicados->entradaregistrodiario[indice_intervalo_15min].energia_generada;


		// se notifica con señales realtime a los procesos que se han registrado
		// en la tabla de subscripcion. Se recorre la tabla para ver que procesos hay que notificar
		int i, retc;
		for (i=0;i< MAX_PIDS_PARA_SIGNAL; i++){
			if (plinea_subscripcion[i].pid!=0){
				retc=kill(plinea_subscripcion[i].pid, SIGRTMIN + plinea_subscripcion[i].rt_senal);
				if (retc==-1 && errno==ESRCH){
						plinea_subscripcion[i].pid=0;
				}
			}
		}
		/*
		 * saca valores por consola si es un proceso normal en lugar de un daemon
		 */
		if (arguments.process){
			printf("err:%d%d ", rc1,rc2);
			printf("P:%4.0fW ", pdatos_instantaneos->potencia);
			printf("Q:%4.0fvar ", pdatos_instantaneos->potencia_reactiva);
			printf("FP:%4.2f ", pdatos_instantaneos->factor_potencia);
			printf("V:%3.0fV ", pdatos_instantaneos->tension);
			printf("I:%4.1fA ", pdatos_instantaneos->intensidad);
			printf("F:%5.2fHz ", pdatos_instantaneos->frecuencia);
			printf("Ei:%3.2fkWh ", pdatos_instantaneos->energia_total_importada);
			printf("Ee:%3.2fkWh ", pdatos_instantaneos->energia_total_exportada);
			printf("Eri:%3.2fkvarh ", pdatos_instantaneos->energia_reactiva_total_inductiva);
			printf("Erc:%3.2fkvarh ", pdatos_instantaneos->energia_reactiva_total_capacitiva);
			printf("Pavg(%d s):%4.0fW ", pdatos_instantaneos->ventana_integracion, pdatos_instantaneos->potencia_media_importada_15min);
			printf("\r");
			fflush(stdout);

			if (numExp != 1 ||err_rcp!=0){
					printf("\n");
			}
		}

	}
	//sale de bucle por cambio variable recibido_senal_fin en la funcion terminar que se ejecuta por señal SIGGINT o SIGTERM
	close(fd_lock); // cierra fichero de lock con PID
	closelog(); // cierra log
	modbus_close(ctx);
	modbus_free(ctx);
	printf("\n");
 	return 0;
}
