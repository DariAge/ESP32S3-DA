/*
 * thermal_control.c
 *  Funciones y tarea para control de temperatura por PID
 *  Created on: 24 feb. 2026
 *      Author: DaroA
 */

#include "thermal_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "pid_controller.h"
#include "telemetry_task.h"

static const char *TAG = "THERMAL_CTRL";
static thermal_system_t ts_ctx;			//variable global con datos del sistema térmico
static volatile int pulse_count = 0;	//contador de pulsos de ZCD

//Función de interrupción ante detección de pulso de ZCD
bool IRAM_ATTR on_zcd_event(mcpwm_cap_channel_handle_t cap_chan, const mcpwm_capture_event_data_t *edata, void *user_data) {
    pulse_count++;
    if (pulse_count >= BURST_WINDOW) {
        pulse_count = 0;
    }

    // Lógica de tren de pulsos (Burst Firing) con chequeo ante overheat y valor coherente de pulsos
    if (pulse_count < ts_ctx.pulse_target && ts_ctx.pulse_target > 0 && !ts_ctx.overheat_fault) {
        gpio_set_level(GPIO_TRIAC_OUT, 1);
    } else {
        gpio_set_level(GPIO_TRIAC_OUT, 0);
    }
    return false;
}

//Inicialización de variables de control y gpio ssr
esp_err_t thermal_control_init(thermal_config_t *sensors_handle) {
    ts_ctx.sensors = sensors_handle;
    ts_ctx.overheat_fault = false;
    ts_ctx.autotune_active = false;
    ts_ctx.pulse_target = 0;

    // 1. Configurar GPIO de salida SSR
    gpio_config_t ssr_conf = {
        .pin_bit_mask = (1ULL << GPIO_TRIAC_OUT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
    };
    gpio_config(&ssr_conf);

    // 2. Inicializar PID (Valores iniciales)
    pid_init(&ts_ctx.pid, 2.5, 0.1, 0.05, 1.0, 0, BURST_WINDOW);

    // 3. Configurar Canal de Captura usando el Timer compartido
    //en mcpwm_da2.c
    
    return ESP_OK;
}

void thermal_set_target(float temp_celsius) {
    ts_ctx.pid.setpoint = temp_celsius;
    ESP_LOGI(TAG, "Nuevo setpoint: %.1f C", temp_celsius);
}

void thermal_set_autotune(bool enable) {
    if (enable && !ts_ctx.autotune_active) {
        // Intentamos iniciar con la temperatura actual leída en la estructura
        if (pid_atune_start(&ts_ctx.atune, &ts_ctx.pid, ts_ctx.data.temp_rtd)) {
            ts_ctx.autotune_active = true;
        }
    } else {
        ts_ctx.autotune_active = false;
        ts_ctx.atune.running = false;
    }
}


void vTaskTemperatureControl(void *pvParameters) {
    telemetry_data_t packet; // Estructura para telemetría
    extern QueueHandle_t telemetry_queue; 
   	
   	// 1. Intento de carga desde NVS al iniciar
    // Si falla, podríamos marcar que requiere autotune obligatorio o usar defaults
    if (load_pid_params(&ts_ctx.pid) != ESP_OK) {
        ESP_LOGW(TAG, "No se encontró calibración. Usando valores por defecto.");
        // pid_init(&ts_ctx.pid, DEFAULT_KP, DEFAULT_KI, DEFAULT_KD); 
    }

   	// Configuración de la frecuencia del lazo (1Hz)
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);
    // Se inicializa con el tiempo actual una sola vez
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        // 1. Lectura de Sensores
        esp_err_t st_tc = read_max6675_temp(ts_ctx.sensors->max6675_handle, &ts_ctx.data.temp_tc);
        esp_err_t st_rtd = read_max31865_temp(ts_ctx.sensors->max31865_handle, &ts_ctx.data.temp_rtd);
		
		/* VALIDACIONES:
		*	1ER FILTRO: VALIDACIÓN POR COMUNICACIÓN DE SENSORES.
		*   2DO FILTRO: VALIDACIÓN POR OVERHEATING
		*   3ER FILTRO: AUTOTUNE O MODO NORMAL
		*/
        if (st_tc == ESP_OK && st_rtd == ESP_OK) {
            // 2. Seguridad: Límite duro de 200°C en termopar de superficie
            if (ts_ctx.data.temp_tc > 200.0) {
                
                ts_ctx.pulse_target = 0;
                ts_ctx.overheat_fault = true;
                ESP_LOGE(TAG, "CRITICAL: Surface overheat!");
            
            } else {
                
                ts_ctx.overheat_fault = false;
                
                // --- 3. Lógica de Control: ¿Autotune o PID? ---
                
                if (ts_ctx.autotune_active && ts_ctx.atune.running) {
                    // MODO AUTO-TUNE
                    
                    float power = pid_atune_execute(&ts_ctx.atune, &ts_ctx.pid, ts_ctx.data.temp_rtd);
                    ts_ctx.pulse_target = (int)power;

                    if (!ts_ctx.atune.running) {
                        // El proceso terminó solo
                        ts_ctx.autotune_active = false;
                        save_pid_params(&ts_ctx.pid);
                        ESP_LOGI(TAG, "Auto-tune finalizado: Kp=%.2f, Ki=%.2f, Kd=%.2f", 
                                 ts_ctx.pid.kp, ts_ctx.pid.ki, ts_ctx.pid.kd);
                    }
                    
                 } else {
                    // Modo de operación normal
                    
                    float power = pid_compute(&ts_ctx.pid, ts_ctx.data.temp_rtd);
                    // Limitación de salida (0-100% de PWM/Duty)
                    if (power > 100.0f) power = 100.0f;
                    if (power < 0.0f) power = 0.0f;
                    
                    ts_ctx.pulse_target = (int)power;
                 }
                
            }
            
        } else {
			//FALLA EN LECTURA DE SENSORES 
			if (st_tc != ESP_OK) ts_ctx.data.temp_tc = -9999.9f; 
			if (st_rtd != ESP_OK) ts_ctx.data.temp_rtd = -9999.9f;
            ts_ctx.pulse_target = 0; // Error de lectura = Apagado
            ESP_LOGE(TAG, "Sensor reading failed");
        }

        // 4. Preparar paquete para Telemetría/MQTT
        packet.log_data.temp_tc = ts_ctx.data.temp_tc;
        packet.log_data.temp_rtd = ts_ctx.data.temp_rtd;
        packet.log_data.status_tc = st_tc;
        packet.log_data.status_rtd = st_rtd;
        packet.log_power = ts_ctx.pulse_target;
        packet.log_setpoint = ts_ctx.pid.setpoint;
       
        
        if (telemetry_queue != NULL) {
            xQueueOverwrite(telemetry_queue, &packet);
        }

    }
}


