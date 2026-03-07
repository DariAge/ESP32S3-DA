/*
 * pid_controller.h
 *  Definiciones para el control PID.
 *  Created on: 24 feb. 2026
 *      Author: DaroA
 */

#ifndef MAIN_PID_CONTROLLER_H_
#define MAIN_PID_CONTROLLER_H_

#include <stdbool.h>
#include <math.h>
#include <esp_timer.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
/**
 * @brief Estructura para el control PID de temperatura
 */
typedef struct {
    float kp;               // Ganancia Proporcional
    float ki;               // Ganancia Integral
    float kd;               // Ganancia Derivativa
    float setpoint;         // Temperatura objetivo
    float integral;         // Acumulador integral (con anti-windup)
    float last_measured;    // ÚLTIMA MEDIDA para cálculo de derivada (reemplaza last_error)
    float output_limit_min; // Límite mínimo de salida (0)
    float output_limit_max; // Límite máximo (BURST_WINDOW)
    float ts;               // Tiempo de muestreo en segundos (1.0 para 1Hz)
} pid_ctrl_t;

/**

@brief Estructura para el estado del Auto-Tune (Método del Relevo)
*/
typedef struct {
	bool running;
	float noise_band;
	int peak_count;
	unsigned long last_peak_time;
	float sum_periods;
	float sum_amplitude;
	int last_direction; // 1: subiendo, -1: bajando
} pid_atune_t;



/**
 * @brief Inicializa el controlador PID con sus parámetros base
 * * @param pid Estructura del PID
 * @param kp Ganancia proporcional
 * @param ki Ganancia integral
 * @param kd Ganancia derivativa
 * @param ts Tiempo de muestreo en segundos
 * @param min Límite inferior de salida
 * @param max Límite superior de salida
 */
void pid_init(pid_ctrl_t *pid, float kp, float ki, float kd, float ts, float min, float max);

/**
 * @brief Realiza el cálculo del PID
 * * @param pid Estructura del PID
 * @param measured_value Valor actual leído del sensor (RTD/Líquido)
 * @return float Salida calculada limitada entre min y max
 */
float pid_compute(pid_ctrl_t *pid, float measured_value);

/**
* @brief Valida e inicia el proceso de Auto-Tune
* @return true si se pudo iniciar, false si las condiciones (temp) no son aptas
*/
bool pid_atune_start(pid_atune_t *atune, pid_ctrl_t *pid, float current_temp);


/**
* @brief Ejecuta un paso del Auto-Tune
* @return Potencia (0 o output_limit_max) para generar oscilación
*/
float pid_atune_execute(pid_atune_t *atune, pid_ctrl_t *pid, float measured_value);


esp_err_t save_pid_params(pid_ctrl_t *pid);
esp_err_t load_pid_params(pid_ctrl_t *pid);


#endif /* MAIN_PID_CONTROLLER_H_ */
