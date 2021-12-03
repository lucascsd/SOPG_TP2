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

/*
 *****************************************************************************************************************
 * Seteo estado de salidas (hacia la edu-ciaa)																	 *
 * “>OUTS:X,Y,W,Z\r\n”																							 *				
 * Siendo X,Y,W,Z el estado de cada salida. Los estados posibles son ““0” (apagado),”1”(encendido) o “2” (blink) *
******************************************************************************************************************
* Evento pulsador (desde la edu-ciaa)																			 *			
* “>TOGGLE STATE:X\r\n”																							 *
* Siendo X el número de pulsador (0,1, 2 o 3)																	 *			
******************************************************************************************************************
*/

pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;

char pDataReceived[SIZE_MSG_IN];
char pDataSend[SIZE_MSG_OUT];
char buffer[SIZE_MSG_TCP_IN];
char bufferOutTCP[SIZE_MSG_TCP_OUT];

int tcpfd;

/* Manejo de Signals SIGINT o SIGTERM */
void sigint_handler(int sig)
{
	if ((sig == SIGINT) || (sig == SIGTERM))
	{
		if (close(tcpfd) == 0)
		{
			printf("Cierro puerto TCP\n\r");
			serial_close();
			printf("Cierro puerto serie\n\r");

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

			exit(EXIT_SUCCESS);
		}
		else
		{
			perror("close");
			printf("No se pueden cerrar conexiones, reintentar ...\n\r");
		}
	}
}

int init_serial(void)
{
	int n;
	if (serial_open(COM_PORT_NUMBER, COM_BAUDRATE) == 0)
	{
		printf("Se abre el puerto correctamente\n");
		n = 0;
	}
	else
	{
		printf("Error: No se abre el puerto correctamente\n");
		n = -1;
	}

	/* Inicio con todos los leds apagados */
	if ((serial_receive(pDataReceived, SIZE_MSG_IN + 1)) == -1)
	{
		printf("Error: No se puede escribir el puerto\n");
		n = -1;
	}
	else
	{
		n = 0;
	}
	return n;
}

void *serial_wr_thread(void *param)
{
	while (1)
	{
		/**************************
		* Lecutra de puerto serie *
		***************************/

		if ((serial_receive(pDataReceived, SIZE_MSG_IN + 1)) != -1)
		{
			/* Recibo mensaje de estado de leds */
			if (strncmp(">TOGGLE STATE:", pDataReceived, sizeof(">TOGGLE STATE:") - 1 /*SIZE_MSG_IN - 4*/) == 0)
			{
				if (tcpfd != -1)
				{
					pthread_mutex_lock(&mutexData);

					/* ":LINEXTG\n" */
					strncpy(bufferOutTCP, MSG_TCP_OUT_PROTOTYPE, SIZE_MSG_TCP_OUT);

					if ((pDataReceived[POS_CMD_IN] >= '0') && (pDataReceived[POS_CMD_IN] <= '3'))
					{
						bufferOutTCP[POS_CMD_TCP_OUT] = pDataReceived[POS_CMD_IN];

						if (send(tcpfd, bufferOutTCP, SIZE_MSG_TCP_OUT, 0) == -1)
						{
							perror("SERVER TCP: Error mensaje enviado\r\n");
							exit(EXIT_FAILURE);
						}
					}
					pthread_mutex_unlock(&mutexData);
				}
				memset(bufferOutTCP, 0, SIZE_MSG_TCP_OUT);
			}
		}

		if ((serial_receive(pDataReceived, sizeof(">OK") + 1)) != -1)
		{
			/* Recibo mensaje de respuesta correcta por comando enviado desde PC */
			if (strncmp(">OK", pDataReceived, sizeof(">OK") - 1 /*SIZE_MSG_IN - 4*/) == 0)
			{
				printf("%s", pDataReceived);
			}
		}
		memset(pDataReceived, 0, SIZE_MSG_IN);

		usleep(10000);
	}
}

void *tcp_client_thread(void *param)
{
	socklen_t addr_len;
	struct sockaddr_in clientaddr;
	struct sockaddr_in serveraddr;

	bool connected = false;
	int n;

	// Creamos socket
	int s = socket(PF_INET, SOCK_STREAM, 0);

	// Cargamos datos de IP:PORT del server
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(TCP_PORT_SERVER);
	serveraddr.sin_addr.s_addr = inet_addr(TCP_IP);
	if (serveraddr.sin_addr.s_addr == INADDR_NONE)
	{
		perror("inet_addr");
		fprintf(stderr, "ERROR invalid server IP\r\n");
		return (void *)1;
	}
	printf("SERVER TCP: Servidor creado correctamente\n\r");

	int yes = 1;

	// lose the pesky "Address already in use" error message
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
	{
		perror("setsockopt");
		exit(1);
	}

	// Abrimos puerto con bind()
	if (bind(s, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
	{
		close(s);
		perror("listener: bind");
		return (void *)1;
	}
	printf("SERVER TCP: Puerto abierto correctamente\n\r");

	// Seteamos socket en modo Listening
	if (listen(s, 10) == -1) // backlog=10
	{
		perror("listen");
		exit(1);
	}
	printf("SERVER TCP: Socket en modo listening\n\r");

	while (true)
	{
		// Ejecutamos accept() para recibir conexiones entrantes
		addr_len = sizeof(struct sockaddr_in);

		printf("SERVER TCP: Esperando por una nueva conexion\n\r");
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
			// Leemos mensaje de cliente
			if ((n = recv(tcpfd, buffer, sizeof(MSG_TCP_IN_PROTOTYPE), 0)) == -1)
			{
				perror("recv");
				connected = false;
			}
			if (n == 0)
			{
				printf("Lecturas pendientes = %d\n\r", n);
				break;
			}

			/****************************
			* Escritura de puerto serie *
			*****************************/
			pthread_mutex_lock(&mutexData);

			if (strncmp(":STATES", buffer, sizeof(":STATES") - 1) == 0)
			{
				printf("%s", buffer);
				/* ">OUTS:X,Y,W,Z\r\n" */
				strncpy(pDataSend, MSG_OUT_PROTOTYPE, SIZE_MSG_OUT);

				/* Envio de comandos */
				pDataSend[6] = buffer[7];
				pDataSend[8] = buffer[8];
				pDataSend[10] = buffer[9];
				pDataSend[12] = buffer[10];

				serial_send(pDataSend, SIZE_MSG_OUT);
				memset(buffer, 0, SIZE_MSG_OUT);
			}
			pthread_mutex_unlock(&mutexData);
		}
	}
	// Cerramos conexion con cliente
	close(tcpfd);
	return (void *)0;
}

int main(void)
{
	int ret;

	struct sigaction sa;

	sa.sa_handler = sigint_handler;
	sa.sa_flags = 0; //SA_RESTART;
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

	/* Creacion de thread para lectura del puerto serie */
	ret = pthread_create(&thread_serial, NULL, serial_wr_thread, NULL);

	/* Chequeo de creacion del hilo */
	if (ret)
	{
		errno = ret;
		perror("pthread_create");
		return -1;
	}

	/* Creacion de thread para lectura del puerto serie */
	ret = pthread_create(&thread_tcp, NULL, tcp_client_thread, NULL);

	/* Chequeo de creacion del hilo */
	if (ret)
	{
		errno = ret;
		perror("pthread_create");
		return -1;
	}

	while (1)
	{
		usleep(10000);
	}

	exit(EXIT_SUCCESS);
	return 0;
}
