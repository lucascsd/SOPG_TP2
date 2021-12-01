#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "SerialManager.h"

#define COM_BAUDRATE 115200
#define COM_PORT_NUMBER 1
#define MSG_OUT_PROTOTYPE ">OUTS:X,Y,W,Z\r\n"
#define SIZE_MSG_OUT sizeof(MSG_OUT_PROTOTYPE)
#define MSG_IN_PROTOTYPE ">TOGGLE STATE:X\r\n"
#define SIZE_MSG_IN sizeof(MSG_IN_PROTOTYPE)

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

void init_serial(void)
{
	if (serial_open(COM_PORT_NUMBER, COM_BAUDRATE) == 0)
	{
		printf("Se abre el puerto correctamente\n");
	}
	else
	{
		printf("No se abre el puerto correctamente\n");
		perror("OpenComport");
		exit(1);
	}
}

void *serial_read_thread(void *param)
{
	while (1)
	{
		pthread_mutex_lock(&mutexData);

		if ((serial_receive(pDataReceived, SIZE_MSG_IN + 1)) != -1)
		{
			if (strncmp(">TOGGLE STATE:", pDataReceived, sizeof(">TOGGLE STATE:") - 1 /*SIZE_MSG_IN - 4*/) == 0)
			{
				printf("Mensaje recibido %s\n\r", pDataReceived);
			}
		}
		memset(pDataReceived, 0, SIZE_MSG_IN);
		pthread_mutex_unlock(&mutexData);
		usleep(1000);
	}
}
/* Unificar threads de lectura y escritura */
void *serial_write_thread(void *param)
{
	u_int8_t ledCIAA = 0x0F;
	while (1)
	{
		pthread_mutex_lock(&mutexData);

		/* ">OUTS:X,Y,W,Z\r\n" */
		strcpy(pDataSend, MSG_OUT_PROTOTYPE);

		if ((ledCIAA & 0x08) == 0x08)
		{
			ledCIAA &= 0x07;
			pDataSend[6] = '0';
		}
		else if ((ledCIAA & 0x08) == 0x00)
		{
			ledCIAA |= 0x08;
			pDataSend[6] = '1';
		}

		pDataSend[8] = '1';
		pDataSend[10] = '0';
		pDataSend[12] = '1';

		serial_send(pDataSend, sizeof(MSG_OUT_PROTOTYPE));
		//printf("Valor de leds %d\r\n", ledCIAA);

		pthread_mutex_unlock(&mutexData);
		//usleep(100);
	}
}

int main(void)
{
	printf("Inicio Serial Service\r\n");
	init_serial();

	pthread_t serialRead, serialWrite;
	int ret;

	/* Creacion de thread para lectura del puerto serie */
	ret = pthread_create(&serialRead, NULL, serial_read_thread, NULL);
	
	/* Chequeo de creacion del hilo */
	if (!ret)
	{
		errno = ret;
		perror("pthread_create");
		return -1;
	}
	
	/* Creacion de thread para escritura del puerto serie */
	ret = pthread_create(&serialWrite, NULL, serial_write_thread, NULL);
	
	/* Chequeo de creacion del hilo */
	if (!ret)
	{
		errno = ret;
		perror("pthread_create");
		return -1;
	}

	while (1)
		;

	exit(EXIT_SUCCESS);
	return 0;
}
