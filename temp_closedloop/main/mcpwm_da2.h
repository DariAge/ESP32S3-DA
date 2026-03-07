/*
 * mcpwm_da2.h
 *  Se adicionan definiciones Capture mode para ZCD, a las anteriores 
 *  de PWM Output Edge Aligned  
 *  Created on: 14 ene. 2026
 *      Author: DaroA
 */

#ifndef MAIN_MCPWM_DA2_H_
#define MAIN_MCPWM_DA2_H_

#include <stdint.h>
#include "driver/gpio.h"

// --- Configuración BTS7960 (Motor DC) ---
#define BDC_MCPWM_GPIO_RPWM   25
#define BDC_MCPWM_GPIO_LPWM   26
#define MAX_MOTOR_RPM         1250

// --- Configuración SSR (Temperatura) ---
#define SSR_PWM_GPIO          14  // GPIO para el pulso del SSR
#define ZERO_CROSS_GPIO       34  // Entrada del detector de cruce por cero
#define AC_FREQ_HZ            50  // Frecuencia de red 

// Configuración de la ventana de control
// 100 semiciclos a 50Hz equivalen a 1 segundo de periodo total (T_on + T_off)
#define VENTANA_CONTROL_CICLOS 100 

/**
 * @brief Configuraciones del módulo y seteo GPIO
 */
void init_mcpwm(void);

/**
 * @brief Ajusta la velocidad del motor en función del dutycycle
 * @param rpm valor de 0 a MAX_MOTOR_RPM
 */
void mcpwm_set_speed(float rpm);

/**
 * @brief Ajusta la potencia del SSR
 * @param percentage 0.0 a 100.0
 */
void mcpwm_set_ssr_power(float percentage);

#endif /* MAIN_MCPWM_DA2_H_ */
