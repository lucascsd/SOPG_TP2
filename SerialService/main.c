/*==========================================================================*
 * Autor: Lucas Zalazar <lucas.zalazar6@gmail.com>							*
 * Subject: Sistemas operativos de proposito general - CESE					*
 * 																			*	
 * 																			*
 * Date: 2021/10/30															*
 *==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "SerialManager.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define COM_BAUDRATE 115200
#define COM_PORT_NUMBER 1

#define MSG_OUT_PROTOTYPE ">OUTS:X,Y,W,Z\r\n"
#define SIZE_MSG_OUT sizeof(MSG_OUT_PROTOTYPE)

#define MSG_IN_PROTOTYPE ">TOGGLE STATE:X\r\n"
#define POS_CMD_IN 14
#define SIZE_MSG_IN sizeof(MSG_IN_PROTOTYPE)

#define TCP_PORT_SERVER 10000
#define TCP_IP "127.0.0.1"

#define MSG_TCP_IN_PROTOTYPE ":STATESXYWZ\n"
#define SIZE_MSG_TCP_IN sizeof(MSG_TCP_IN_PROTOTYPE)

#define MSG_TCP_OUT_PROTOTYPE ":LINEXTG\n"
#define POS_CMD_TCP_OUT 5
#define SIZE_MSG_TCP_OUT sizeof(MSG_TCP_OUT_PROTOTYPE)

#define MSG_SIGNAL_SIGINT "Se envió la señal SIGINT!!\n\r"
#define SIZE_MSG_SIGINT sizeof(MSG_SIGNAL_SIGINT)

#define MSG_SIGNAL_SIGTERM "Se envió la señal SIGTERM!!\n\r"
#define SIZE_MSG_SIGTERM sizeof(MSG_SIGNAL_SIGTERM)

pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;

/* Buffers de entrada y salida */
char pDataReceived[SIZE_MSG_IN];
char pDataSend[SIZE_MSG_OUT];
char buffer[SIZE_MSG_TCP_IN];
char bufferOutTCP[SIZE_MSG_TCP_OUT];

int tcpfd;
bool flagHandlerSignal;

void bloquearSign(void)
{
	sigset_t set;

	if (sigemptyset(&set) == -1)
	{
		perror("sigemptyset");
		exit(EXIT_FAILURE);
	}

	if (sigaddset(&set, SIGINT) == -1)
	{
		perror("sigaddset");
		exit(EXIT_FAILURE);
	}

	if (sigaddset(&set, SIGTERM) == -1)
	{
		perror("sigaddset");
		exit(EXIT_FAILURE);
	}

	/* Verificar retorno de error */
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) == -1)
	{
		perror("pthread_sigmask");
		exit(EXIT_FAILURE);
	}
}

void desbloquearSign(void)
{
	sigset_t set;

	if (sigemptyset(&set) == -1)
	{
		perror("sigemptyset");
		exit(EXIT_FAILURE);
	}

	if (sigaddset(&set, SIGINT) == -1)
	{
		perror("sigaddset");
		exit(EXIT_FAILURE);
	}

	if (sigaddset(&set, SIGTERM) == -1)
	{
		perror("sigaddset");
		exit(EXIT_FAILURE);
	}

	/* Verificar retorno de error */
	if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) == -1)
	{
		perror("pthread_sigmask");
		exit(EXIT_FAILURE);
	}
}

/* Manejo de Signals SIGINT o SIGTERM */
void sigint_handler(int sig)
{
	if ((sig == SIGINT) || (sig == SIGTERM))
	{
		flagHandlerSignal = true;
		/* Imprimo mensaje segun señal entrante */
		switch (sig)
		{
		case SIGINT:
			write(1, MSG_SIGNAL_SIGINT, SIZE_MSG_SIGINT);
			break;
		case SIGTERM:
			write(1, MSG_SIGNAL_SIGTERM, SIZE_MSG_SIGTERM);
			break;
		default:
			break;
		}
	}
	else
	{
		flagHandlerSignal = false;
	}
}

int init_serial(void)
{
	/* Se abre puerto serie para recepcion y envío de comandos desde CIAA */
	if (serial_open(COM_PORT_NUMBER, COM_BAUDRATE) == 0)
	{
		printf("Se abre el puerto correctamente\n");
		return 0;
	}
	else
	{
		printf("Error: No se abre el puerto correctamente\n");
		return -1;
	}
}

void *serial_wr_thread(void *message)
{
	printf("%s\n", (const char *)message);

	while (1)
	{
		//		***************************
		//		* Lecutra de puerto serie *
		//		***************************
		if ((serial_receive(pDataReceived, SIZE_MSG_IN + 1)) != -1)
		{
			/* Chequeo que mensaje de estado de leds sea del formato correcto */
			if (strncmp(">TOGGLE STATE:", pDataReceived, sizeof(">TOGGLE STATE:") - 1 /*SIZE_MSG_IN - 4*/) == 0)
			{
				printf("Mensaje recibido por serie: ");
				/* Imprimo mensaje enviado */
				printf("%s", pDataReceived);

				/* Sector critico para escritura y lectura de datos en variables */
				pthread_mutex_lock(&mutexData);

				/* Verifico que fd de TCP exista */
				if (tcpfd != -1)
				{
					/* ":LINEXTG\n" */
					strncpy(bufferOutTCP, MSG_TCP_OUT_PROTOTYPE, SIZE_MSG_TCP_OUT);

					/* Chequeo que mensaje recibido sea valido */
					if ((pDataReceived[POS_CMD_IN] >= '0') && (pDataReceived[POS_CMD_IN] <= '3'))
					{
						/* Reemplazo caracter con el valor de la linea modificado en CIAA */
						bufferOutTCP[POS_CMD_TCP_OUT] = pDataReceived[POS_CMD_IN];

						/* Envio mensaje por socket tcp */
						if (write(tcpfd, bufferOutTCP, SIZE_MSG_TCP_OUT) == -1)
						{
							perror("SERVER TCP: Error mensaje enviado\r\n");
							exit(EXIT_FAILURE);
						}
					}
					/* Imprimo mensaje enviado */
					printf("Mensaje enviado por socket: ");
					printf("%s", bufferOutTCP);
				}
				/* Finalizo zona critica de escritura y lectura de variables */
				pthread_mutex_unlock(&mutexData);

				/* Limpio buffer */
				memset(bufferOutTCP, 0, SIZE_MSG_TCP_OUT);
			}
			/* Recibo mensaje de confirmacion de comando enviado hacia la CIAA */
			/* Recibo mensaje de respuesta correcta por comando enviado desde PC */
			if (strncmp(">OK", pDataReceived, sizeof(">OK") - 1) == 0)
			{
				printf("Mensaje recibido por serie: ");
				printf("%s", pDataReceived);
			}
		}

		/* Limpio buffer de recepcion */
		memset(pDataReceived, 0, SIZE_MSG_IN);

		/* Retardo bloqueante para que el consumo de CPU no se vaya al 100% */
		usleep(10000);
	}
}

void *tcp_client_thread(void *message)
{
	printf("%s\n", (const char *)message);
	sleep(1);

	socklen_t addr_len;
	struct sockaddr_in clientaddr;
	struct sockaddr_in serveraddr;

	bool connected = false;
	int n;
	int s;

	/* Creacion socket del socket */
	if ((s = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		fprintf(stderr, "SERVER TCP: ERROR invalid socket\r\n");
		exit(EXIT_FAILURE);
	}
	printf("SERVER TCP: Socket creado correctamente\n\r");

	/* Cargamos datos de IP:PORT del server */
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(TCP_PORT_SERVER);
	serveraddr.sin_addr.s_addr = inet_addr(TCP_IP);

	/* Chequeo de direccion cargada */
	if (serveraddr.sin_addr.s_addr == INADDR_NONE)
	{
		perror("inet_addr");
		fprintf(stderr, "SERVER TCP: ERROR invalid server IP\r\n");
		exit(EXIT_FAILURE);
	}
	printf("SERVER TCP: Servidor creado correctamente\n\r");

	int yes = 1;

	/* Fuerzo la reutilizacion de la direccion que ya esta en uso */
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
	{
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	/* Abrimos puerto con bind() */
	if (bind(s, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
	{
		close(s);
		perror("listener: bind");
		return (void *)1;
	}
	printf("SERVER TCP: Puerto abierto correctamente\n\r");

	/* Seteamos socket en modo Listening */
	if (listen(s, 10) == -1) // backlog=10
	{
		perror("listen");
		exit(1);
	}
	printf("SERVER TCP: Socket en modo listening\n\r");

	while (true)
	{
		printf("SERVER TCP: Esperando por una nueva conexion\n\r");
		/* Ejecutamos accept() para recibir conexiones entrantes */
		addr_len = sizeof(struct sockaddr_in);
		if ((tcpfd = accept(s, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
		{
			perror("accept");
		}
		else
		{
			printf("SERVER TCP: Estamos conectados!\n\r");
			connected = true;

			printf("SERVER TCP: conexion desde IP: %s\n", inet_ntoa(clientaddr.sin_addr));
			printf("SERVER TCP: conexion desde PORT: %d\n", TCP_PORT_SERVER);
		}

		while (connected)
		{
			printf("SERVER TCP: Esperando respuesta!\n\r");
			/* Lecutra de mensaje de cliente entrante */
			n = read(tcpfd, buffer, SIZE_MSG_TCP_IN);

			/* Cantidad de bytes recibidos */
			printf("SERVER TCP: Cantidad de bytes n = %d y mensaje %s\n\r", n, buffer);

			if (n > 0)
			{
				//			*****************************
				//			* Escritura de puerto serie *
				//			*****************************
				/* Comienzo zona critica de lectura y escritura de variables */
				pthread_mutex_lock(&mutexData);

				if (strncmp(":STATES", buffer, sizeof(":STATES") - 1) == 0)
				{
					printf("SERVER TCP: Mensaje recibido por socket: ");
					/* Imprimo mensaje recibido */
					printf("%s", buffer);

					/* ">OUTS:X,Y,W,Z\r\n" */
					strncpy(pDataSend, MSG_OUT_PROTOTYPE, SIZE_MSG_OUT);

					/* Reemplazo comandos en buffer a enviar */
					pDataSend[6] = buffer[7];
					pDataSend[8] = buffer[8];
					pDataSend[10] = buffer[9];
					pDataSend[12] = buffer[10];

					/* Envio comandos */
					serial_send(pDataSend, SIZE_MSG_OUT);

					printf("Mensaje enviado por serie: ");
					/* Imprimo mensaje recibido */
					printf("%s", pDataSend);

					/* Limpio buffer luego de enviar */
					memset(buffer, 0, SIZE_MSG_OUT);
				}
				/* Finaliza zona critica de lectura y escritura de variables */
				pthread_mutex_unlock(&mutexData);
			}
			else if (n == 0)
			{
				printf("SERVER TCP: Se desconecto el cliente\n\r");
				connected = false;
				break;
			}
			else if (n < 0)
			{
				perror("recv");
				exit(EXIT_FAILURE);
			}
		}
	}
	close(tcpfd);
	return (void *)0;
}

int main(void)
{
	int ret;

	struct sigaction sa;

	flagHandlerSignal = false;

	sa.sa_handler = sigint_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);

	if ((sigaction(SIGINT, &sa, NULL) == -1) || (sigaction(SIGTERM, &sa, NULL) == -1))
	{
		perror("sigaction");
		exit(1);
	}

	printf("Inicio Serial Service\r\n");
	if (init_serial() == -1)
	{
		printf("Error: Puerto Serie\r\n");
		exit(1);
		return -1;
	}

	pthread_t thread_tcp;
	pthread_t thread_serial;

	const char *messageTCP = "thread_tcp";
	const char *messageSerial = "thread_serial";

	printf("Bloqueo signal\n");
	bloquearSign();

	/* Creacion de thread para lectura del puerto serie */
	ret = pthread_create(&thread_serial, NULL, serial_wr_thread, (void *)messageSerial);

	/* Chequeo de creacion del hilo */
	if (ret)
	{
		errno = ret;
		perror("pthread_create");
		return -1;
	}

	/* Creacion de thread para lectura del puerto serie */
	ret = pthread_create(&thread_tcp, NULL, tcp_client_thread, (void *)messageTCP);

	/* Chequeo de creacion del hilo */
	if (ret)
	{
		errno = ret;
		perror("pthread_create");
		return -1;
	}

	printf("Desbloqueo signal\n");
	desbloquearSign();

	while (1)
	{
		if (flagHandlerSignal)
		{
			/* Cerramos conexion con cliente TCP*/
			if (close(tcpfd) == 0)
			{
				printf("Cierro puerto TCP\n\r");

				/* Cerramos conexion con puerto serie*/
				serial_close();
				printf("Cierro puerto serie\n\r");

				/* Termino ejecucion de programa */
				exit(EXIT_SUCCESS);
			}
			else
			{
				perror("close");
				printf("No se pueden cerrar conexiones\n\r");
			}
			/* Cancellation request to thread TCP */
			if (pthread_cancel(thread_tcp) == -1)
			{
				perror("THREAD TCP: pthread_cancel");
				exit(EXIT_FAILURE);
			}
			else
			{
				printf("THREAD TCP: Se cancela el thread serial\n\r");
			}

			/* Cancellation request to thread serial */
			if (pthread_cancel(thread_serial) == -1)
			{
				perror("THREAD SERIAL: pthread_cancel");
				exit(EXIT_FAILURE);
			}
			else
			{
				printf("THREAD SERIAL: Se cancela el thread serial\n\r");
			}

			if (pthread_join(thread_tcp, NULL) == -1)
			{
				perror("THREAD TCP: pthread_join");
				exit(EXIT_FAILURE);
			}
			else
			{
				printf("THREAD TCP: El thread serial finaliza\n\r");
			}

			if (pthread_join(thread_serial, NULL) == -1)
			{
				perror("THREAD SERIAL: pthread_join");
				exit(EXIT_FAILURE);
			}
			else
			{
				printf("THREAD SERIAL: El thread serial finaliza\n\r");
			}

			exit(EXIT_FAILURE);
		}
		usleep(10000);
	}

	/* Cerramos conexion con cliente */
	close(tcpfd);
	printf("Cierro puerto TCP\n\r");

	serial_close();
	printf("Cierro puerto serie\n\r");

	exit(EXIT_SUCCESS);
	return 0;
}
