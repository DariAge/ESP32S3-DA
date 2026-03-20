/*
 * thermal_control.h
 *
 *  Created on: 24 feb. 2026
 *      Author: DaroA
 */

#ifndef MAIN_THERMAL_CONTROL_H_
#define MAIN_THERMAL_CONTROL_H_


#include "driver/mcpwm_cap.h"
#include "pid_controller.h"
#include "thermal_sensors_da.h" // Tus drivers de sensores

// Configuración de Hardware
#define GPIO_ZCD_IN       GPIO_NUM_6
#define GPIO_TRIAC_OUT    GPIO_NUM_7
#define BURST_WINDOW      100    // 100 semiciclos = 1 segundo a 50Hz

#define MAX_TEMP_GRADIENT 		70
#define PLATE_ABS_MAX_TEMP 		230
#define OUTPUT_LIM_MIN_MASTER  	15		//LA SALIDA DEL PID MAESTRO ES EN TEMPERATURA
#define OUTPUT_LIM_MIN_SLAVE	0		//LA SALIDA DEL PID ESCLAVO ES EN PORCENTAJE DE POWER	

/**
 * @brief Estructura de control térmico
 */
typedef struct {
    pid_ctrl_t pid;				//estructura para realizar control pid
    pid_ctrl_t pid_master;
    pid_ctrl_t pid_slave;
    pid_atune_t atune;          // Estado del Auto-Tune
    thermal_config_t *sensors;
    thermal_data_packet_t data;	//temperaturas medidas de rtd y tc
    int pulse_target;			//cantidad de pulsos por encender SSR
    bool overheat_fault;		//bandera para overheat de superficie
    bool autotune_active;		//bandera para consultar por modo autotune
	
} thermal_system_t;

/**
 * @brief Inicializa GPIO SSR, variable de control del sistema ts.ctx y valores PID
 */
esp_err_t thermal_control_init(thermal_config_t *sensors_handle);

/**
 * @brief Tarea de FreeRTOS para el lazo de control
 */
void vTaskTemperatureControl(void *pvParameters);

/**
 * @brief Cambiar el setpoint de temperatura
 */
void thermal_set_target(float temp);

/**
* @brief Activa/Desactiva el modo Auto-Tune
*/
void thermal_set_autotune(bool enable);



/**
 * @brief ISR para Cruce por Cero (Burst Firing)
 */
bool IRAM_ATTR on_zcd_event(mcpwm_cap_channel_handle_t cap_chan, const mcpwm_capture_event_data_t *edata, void *user_data);


#endif /* MAIN_THERMAL_CONTROL_H_ */
