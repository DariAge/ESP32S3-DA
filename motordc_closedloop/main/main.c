/**  Sistema de control a lazo cerrado de motor DC.
 *   @brief: Envía datos (velocidad medida por encoder del motor)
 *	 a dispositivo maestro y recibe datos para funcionamiento 
 *   (start/stop - setpoint - slope). 
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

// Librerías custom que mencionaste (deben estar en tu proyecto)
#include "mcpwm_da2.h"
#include "encoder_da.h"

#define DEFAULT_SETPOINT 	 500
#define MAX_SETPOINT		 2800
#define MIN_SETPOINT	     10
#define DEFAULT_SLOPE        50.0f
#define MIN_SLOPE            5.0f
#define MAX_SLOPE            200.0f
#define SLOPE_STEP           5.0f

// --- PARÁMETROS DE CONTROL ---
#define CONTROL_LOOP_MS      100
#define ALPHA_RPM_FILTER     0.2f
#define DEADBAND_VALUE		 15.0f
	
#define EVENT_BIT_RUNNING    (1 << 0)
#define EVENT_BIT_STOP       (1 << 1)
#define EVENT_BIT_ERROR_OC   (1 << 2)

// Configuración UART (Conectar a pines 16/17 del WROOM)
#define RX_PIN 18  // Definir según tu cableado en el S3
#define TX_PIN 17
#define UART_PORT UART_NUM_2
#define BUF_SIZE 1024
#define BUF_SIZE_TX 64

#define DRV_MOTOR_ENABLE 39

static const char *TAG = "SISTEMA_CONTROL_S3";

// --- ESTRUCTURAS ---
typedef struct {
    uint16_t setpoint_rpm;
    float current_ramp_rpm;
    float current_real_rpm;
    float current_ma;
    uint8_t system_state;
    float rampslope;
} MonitorData_t;

typedef struct {
    uint16_t setpoint_rpm;
    float ramp_slope;
    SemaphoreHandle_t xMotorParamsMutex;
    EventGroupHandle_t event_group;
} MotorParams_t;

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_START,
    CMD_STOP,
    CMD_SET_RPM,
    CMD_SET_SLOPE
} hmi_cmd_id_t;

typedef struct {
	float value;
	hmi_cmd_id_t cmd;
}HmiParams_t;



// --- GLOBALES ---
static float ema_rpm = 0.0f;
static EventGroupHandle_t s_system_event_group;
static QueueHandle_t uart_evt_queue = NULL;
static QueueHandle_t current_queue = NULL;
static QueueHandle_t monitor_queue = NULL;

// Variables de Estado Globales (Compartidas con tus PIDs)
float target_rpm = 0;
float target_temp = 0;
bool system_running = false;

// --- FUNCIONES ---

float apply_rpm_filter(float current_sample) {
    if (current_sample < 1.0f) {
        ema_rpm = 0.0f;
        return 0.0f;
    }
    ema_rpm = (ALPHA_RPM_FILTER * current_sample) + ((1.0f - ALPHA_RPM_FILTER) * ema_rpm);
    return ema_rpm;
}

void init_uart() {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/**
 * Tarea encargada de escuchar la UART y parsear el protocolo
 */
void uart_rx_task(void *arg) {
    uint8_t data [BUF_SIZE];
    HmiParams_t msg_to_send;
    
    while (1) {
        // Leer datos de la UART
        //uart_read_bytes tiene un buffer circular y no trabaja por polling
        //de hecho, bloquea la tarea en ticks_to_wait
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, 20 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            data[len] = '\0';
            char* str_data = (char*)data;

            // Buscar inicio y fin de trama: $...#
            char* start = strchr(str_data, '$');
            char* end = strchr(str_data, '#');

            if (start && end && (end > start)) {
                *end = '\0'; // Eliminar el terminador #
                char* payload = start + 1; // Saltar el inicio $
                
                // Separar comando y valor por el ":"
                char* colon = strchr(payload, ':');
                
                char* value_str = "0";
                char* command = payload;
                 
                if (colon) {
                    // Escenario A: Comando con valor (ej: SET_RPM:1200)
                    *colon = '\0';
                    value_str = colon + 1;

                }
                // Escenario B: Comando simple (ej: START). NO BUSCA EL :
				
				// --- TRADUCCIÓN DE STRING A ENUM (El "Traductor") ---
				
                if (strcmp(command, "START") == 0)      msg_to_send.cmd = CMD_START;
                else if (strcmp(command, "STOP") == 0)  msg_to_send.cmd = CMD_STOP;
                else if (strcmp(command, "SET_RPM") == 0) msg_to_send.cmd = CMD_SET_RPM;
                else if (strcmp(command, "SET_SLOPE") == 0) msg_to_send.cmd = CMD_SET_SLOPE;
                else msg_to_send.cmd = CMD_UNKNOWN;
               
               //DESCOMENTAR PARA PROBAR SI LA TRAMA ES RECIBIDA
               /* 
                if (strcmp(command, "START") == 0)      
                {
					ESP_LOGI("UART_TASK", "RECIBÍ START");
					msg_to_send.cmd = CMD_START;
				}
                else if (strcmp(command, "STOP") == 0){
					ESP_LOGI("UART_TASK", "RECIBÍ STOP");
					msg_to_send.cmd = CMD_STOP;
				}  
                else if (strcmp(command, "SET_RPM") == 0)
                {
					ESP_LOGI("UART_TASK", "RECIBÍ RPM");
					msg_to_send.cmd = CMD_SET_RPM;
				} 
                else if (strcmp(command, "SET_SLOPE") == 0){
					 ESP_LOGI("UART_TASK", "RECIBÍ SLOPE");
					 msg_to_send.cmd = CMD_SET_SLOPE;
				}
                else msg_to_send.cmd = CMD_UNKNOWN;
				*/
				
                msg_to_send.value = atof(value_str); // Convertimos el valor aquí
                    
                if (msg_to_send.cmd != CMD_UNKNOWN) {
                   xQueueSend(uart_evt_queue, &msg_to_send, 0);
                }
            }
        }
        
    }
}

// --- TAREA: INPUTS (Lógica intacta) ---
void input_task(void *arg) {
    MotorParams_t *params = (MotorParams_t *)arg; //salida de tarea
    HmiParams_t uart_msg;
    
    for(;;) {
        
        if(xQueueReceive(uart_evt_queue, &uart_msg, portMAX_DELAY)) {
            
            switch(uart_msg.cmd)
            {
				case CMD_START:
							xEventGroupClearBits(s_system_event_group, EVENT_BIT_STOP | EVENT_BIT_ERROR_OC);
                        	xEventGroupSetBits(s_system_event_group, EVENT_BIT_RUNNING);
                        	gpio_set_level(DRV_MOTOR_ENABLE, 1); //habilito chip
                        	break;
                case CMD_STOP:
                			xEventGroupClearBits(s_system_event_group, EVENT_BIT_RUNNING);
                        	xEventGroupSetBits(s_system_event_group, EVENT_BIT_STOP);
                        	//deshabilito luego de rampa decreciente
                        	break;
                case CMD_SET_RPM:
                			
                			if(xSemaphoreTake(params->xMotorParamsMutex, portMAX_DELAY)) {
                           		 //Validacion por límites de dato a enviar
                           		 if(uart_msg.value <=  MAX_SETPOINT &&  uart_msg.value >= MIN_SETPOINT) 
                            	 {
								  	params->setpoint_rpm = (uint16_t)uart_msg.value; 
								 } else {
                            		ESP_LOGW(TAG, "RPM fuera de rango: %.1f", uart_msg.value);
                        		 }
                        		 //devuelvo mutex con dato actual (TRUE del if) o dato anterior (condicion FALSE)
                      			 xSemaphoreGive(params->xMotorParamsMutex);	 
                        	}
                        	break;
                case CMD_SET_SLOPE: 
                			
                			if(xSemaphoreTake(params->xMotorParamsMutex, portMAX_DELAY)) {
                           		 //Validacion por límites de dato a enviar
                           		 if(uart_msg.value <= MAX_SLOPE &&  uart_msg.value >= MIN_SLOPE) 
                            	 {
								  	params->ramp_slope = (uint16_t)uart_msg.value;
								 } else {
                            		ESP_LOGW(TAG, "Slope fuera de rango: %.1f", uart_msg.value);
                        		 }
                        		 //devuelvo mutex con dato actual (TRUE del if) o dato anterior (condicion FALSE)
                        		 xSemaphoreGive(params->xMotorParamsMutex); 
                        	}
                        	break;
               case CMD_UNKNOWN: //se enoja el compilador si no lo pongo, pero nunca sucedería
               default:
               				break;
			}        
    	}
	}
}

// --- TAREA: RAMPA (Lógica intacta) ---
void ramp_task(void *arg) {
    MotorParams_t *params = (MotorParams_t *)arg;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    MonitorData_t data_to_send;
    float acc_ramp_rpm = 0.0f; //acumulador instantaneo de valor rampa
    float temporal_current = 0.0f;
    const float dt = (float)CONTROL_LOOP_MS / 1000.0f;
    float last_output = 0.0f;
    
    float integral_error = 0.0f;
	const float Ki = 0.25f; // Constante integral muy suave
	const float Kp = 0.2f;  // Constante proporcional
	
	float filtered_real_rpm = 0.0f;
	const float alpha = 0.4f; // Filtro suave para el PID

    for(;;) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
        
        // Leemos una vez y guardamos en una variable local para usar en PID y telemetría
        float raw_rpm = encoder_get_rpm(); 
		// Aplicamos un filtro pasabajos digital para que el PID no se vuelva loco
		filtered_real_rpm = (alpha * raw_rpm) + ((1.0f - alpha) * filtered_real_rpm);
        
        uint16_t target_rpm = 0;
        float slope = 0;

        if(xSemaphoreTake(params->xMotorParamsMutex, pdMS_TO_TICKS(10))) {
            target_rpm = params->setpoint_rpm;
            slope = params->ramp_slope;
            xSemaphoreGive(params->xMotorParamsMutex);
        }

        EventBits_t status = xEventGroupGetBits(s_system_event_group);
        bool is_running = (status & EVENT_BIT_RUNNING) && !(status & EVENT_BIT_STOP) && !(status & EVENT_BIT_ERROR_OC);
		
        if(xQueueReceive(current_queue, &temporal_current, 0) ==pdFALSE) {
            // Si la cola estaba vacía, mantenemos el último valor o ponemos 0
        }
        data_to_send.current_ma = temporal_current;
		
		
		/***********GENERACIÓN DE RAMPA**********/
		
        if (is_running) {
            //1.case RUNNING y menor a setpoint
            if (acc_ramp_rpm < target_rpm) {
                acc_ramp_rpm += (slope * dt);
                if (acc_ramp_rpm > target_rpm) acc_ramp_rpm = target_rpm;
            } else if (acc_ramp_rpm > target_rpm) {
            //2. case RUNNING y temporalmente superado el setpoint
                acc_ramp_rpm -= (slope * dt);
                if (acc_ramp_rpm < target_rpm) acc_ramp_rpm = target_rpm;
            }
        } else {
            // Desaceleración controlada incluso en stop para proteger el puente H 
            if (acc_ramp_rpm > 0) {
                acc_ramp_rpm -= (slope * dt * 2.0f); // Frenado doble de rápido que la subida
                if (acc_ramp_rpm < 0) 
                {
					acc_ramp_rpm = 0; 
					gpio_set_level(DRV_MOTOR_ENABLE, 0); //luego de rampa decreciente, deshabilito
				} 
            } 
            
            if (!(status & EVENT_BIT_ERROR_OC)) data_to_send.current_ma = 0.0f;
        }
		
		/***********CORRECCIÓN CONTROLADOR PI**********/
		
		float motor_output_final = acc_ramp_rpm; // Valor base de la rampa

    	if (is_running && acc_ramp_rpm > 100) { // Solo corregimos cuando ya está girando
       		 
       		
        	float error = acc_ramp_rpm - filtered_real_rpm; // ¿Qué tan lejos está la realidad de mi rampa?
			
			// --- ZONA MUERTA (PARA QUITAR EL ZUMBIDO) ---
        	// Si el error es menor a x RPM, no dejamos que el PID oscile.
        	if (fabs(error) < DEADBAND_VALUE) {
          	  //dentro de zona muerta
          	  motor_output_final = last_output; 
        	} else {
         	   // Solo integramos si el error es significativo
            	integral_error += error * dt;
            
            	// Anti-windup (limite de la fuerza de la integral)
           		if (integral_error > 600) integral_error = 600;
           		if (integral_error < -600) integral_error = -600;
				motor_output_final = acc_ramp_rpm + (error * Kp) + (integral_error * Ki);
    		
			
			}
        	
    	} else {
        	
        	integral_error = 0; // Resetear si el motor se detiene
    	}
    	
    	// --- 3. SEGURIDAD EXTRA ---
        // Evitamos que el PID mande valores negativos o saltos imposibles
        if (motor_output_final < 0) motor_output_final = 0;
        if (motor_output_final > MAX_MOTOR_RPM) motor_output_final = MAX_MOTOR_RPM;
		
		last_output = motor_output_final;
   	    mcpwm_set_speed((float)((int)motor_output_final));
		
			
		//---ARMADO DE PAQUETE DE TELEMETRÍA---
        data_to_send.current_ramp_rpm = acc_ramp_rpm;
        data_to_send.setpoint_rpm = target_rpm;
        data_to_send.system_state = (uint8_t)status;
        data_to_send.current_real_rpm = filtered_real_rpm;
        data_to_send.rampslope = slope;
        
        xQueueSend(monitor_queue, &data_to_send, 0);
    }
}

// --- TAREA: MONITOR (Lógica intacta) ---
void monitor_task(void *arg) {
    float last_measured_rpm = 0.0f;
    MonitorData_t data;
    uint8_t count_uart = 0;
    char data_send[BUF_SIZE_TX];
    
    
    for(;;) {
        
        if(xQueueReceive(monitor_queue, &data, portMAX_DELAY)) {
            
            float current_rpm = data.current_real_rpm;
            bool is_running = (data.system_state & EVENT_BIT_RUNNING) && !(data.system_state & EVENT_BIT_STOP);
            
            int estado_numerico = 0;
            if(!is_running) estado_numerico = 0;
            else estado_numerico = 1;
            if (data.system_state & EVENT_BIT_ERROR_OC) estado_numerico = 2;
            
            //TELEMETRÍA SLAVE ESP32-S3 TO MASTER ESP32 WROOM
            count_uart++;
            if(count_uart >= 10)
            {
				//ARMADO DE CADENA A ENVIAR CON FORMATO $DAT:<REAL_RPM>,<CURRENT_RAMP_RPM>,<I>,<STATE_NUMBER>#
				//EJEMPLO #$DAT:1195.4,1200.0,340.5,1# 
				int len = snprintf(data_send, BUF_SIZE_TX, "$DAT:%.1f,%.1f,%.1f,%d#", current_rpm, data.current_ramp_rpm, data.current_ma, estado_numerico);
				uart_write_bytes(UART_PORT, data_send,len);
				count_uart = 0;
			}
			
			//TELEMETRÍA CONSOLA
            
            if (fabs(current_rpm - last_measured_rpm) > 2.0f || (data.system_state & EVENT_BIT_ERROR_OC)) {
                ESP_LOGI("MONITOR", "E:%s | SP:%d | Slp:%.1f | Rmp:%.1f | Real:%.1f | I:%.1f mA",
                    (data.system_state & EVENT_BIT_ERROR_OC) ? "ERR_OC" : (is_running ? "RUN" : "STOP"),
                    data.setpoint_rpm, data.rampslope,data.current_ramp_rpm, current_rpm, data.current_ma);
			
                last_measured_rpm = current_rpm;
            }
        }
    }
}


void app_main(void) {
    // Inicialización periféricos custom
    init_mcpwm_bts7960();
    init_encoder_pcnt();
    init_uart();
    

    uart_evt_queue = xQueueCreate(10, sizeof(HmiParams_t));
    monitor_queue = xQueueCreate(10, sizeof(MonitorData_t));
    current_queue = xQueueCreate(1, sizeof(float)); //xQueueOverwrite pide longitud 1
    s_system_event_group = xEventGroupCreate();
    
    xEventGroupSetBits(s_system_event_group, EVENT_BIT_STOP);

    static MotorParams_t motor_cfg;
    motor_cfg.setpoint_rpm = 500;
    motor_cfg.ramp_slope = 50.0f;
    motor_cfg.xMotorParamsMutex = xSemaphoreCreateMutex();

    // Crear tareas clavadas a núcleos para estabilidad
    xTaskCreatePinnedToCore(input_task, "input", 4096, &motor_cfg, 9, NULL, 0);
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 10, NULL,  0);
    xTaskCreatePinnedToCore(ramp_task, "ramp", 4096, &motor_cfg, 8, NULL, 1);
    xTaskCreatePinnedToCore(monitor_task, "monitor", 4096, NULL, 2, NULL, 0);
}