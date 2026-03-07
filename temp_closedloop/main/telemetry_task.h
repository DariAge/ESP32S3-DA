/*
 * telemetry_task.h
 *  Version modificada para enviar datos a Adafruit IO
 *  Created on: 08 nov. 2025
 *  Modified on: 07 feb. 2026
 *      Author: DaroA
 */

#ifndef MAIN_TELEMETRY_TASK_H_
#define MAIN_TELEMETRY_TASK_H_

#include "thermal_sensors_da.h"

// --- CONFIGURACIÓN DE CREDENCIALES ---
#define AIO_SERVER      "mqtt://io.adafruit.com"
#define AIO_PORT        1883

typedef struct{
	thermal_data_packet_t log_data;
	int log_power;
	float log_setpoint; 
}telemetry_data_t;

#endif /* MAIN_TELEMETRY_TASK_H_ */
