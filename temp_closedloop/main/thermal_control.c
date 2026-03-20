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
#include "mcpwm_da2.h"

static const char *TAG = "THERMAL_CTRL";
thermal_system_t ts_ctx;			//variable global con datos del sistema térmico
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
    pid_init(&ts_ctx.pid_master, 2.5, 0.1, 0.05, 1.0, OUTPUT_LIM_MIN_MASTER , PLATE_ABS_MAX_TEMP);
	// Slave: Entrada Placa -> Salida % Potencia (Rango 0 a 100)
    pid_init(&ts_ctx.pid_slave, 5.0, 0.1, 0.1, 1.0, OUTPUT_LIM_MIN_SLAVE, 100);
    // 3. Configurar Canal de Captura usando el Timer compartido
    //init_mcpwm();
    //en mcpwm_da2.c
    
    return ESP_OK;
}

void thermal_set_target(float temp_celsius) {
    ts_ctx.pid_master.setpoint = temp_celsius;
    ESP_LOGI(TAG, "Nuevo setpoint: %.1f C", temp_celsius);
}

void thermal_set_autotune(bool enable) {
    if (enable && !ts_ctx.autotune_active) {
        // Intentamos iniciar con la temperatura actual leída en la estructura
        if (pid_atune_start(&ts_ctx.atune, &ts_ctx.pid_master, ts_ctx.data.temp_rtd)) {
            ts_ctx.autotune_active = true;
        }
    } else {
        ts_ctx.autotune_active = false;
        ts_ctx.atune.running = false;
    }
}

// Función de ayuda para evitar que el PID se rompa si los params son nulos
void check_pid_integrity(pid_ctrl_t *pid, float default_kp) {
    if (isnan(pid->kp) || pid->kp < 0) pid->kp = default_kp;
    if (isnan(pid->ki)) pid->ki = 0.0f;
    if (isnan(pid->kd)) pid->kd = 0.0f;
    if (isnan(pid->setpoint)) pid->setpoint = 0.0f;
    //pid->integral = 0;
}

void vTaskTemperatureControl(void *pvParameters) {
    telemetry_data_t packet; // Estructura para telemetría
    extern QueueHandle_t telemetry_queue; 
   /*	
    if (load_pid_params(&ts_ctx.pid_master) != ESP_OK) {
        ESP_LOGW(TAG, "Master PID: Usando defaults.");
        check_pid_integrity(&ts_ctx.pid_master, 2.0f);
    }  
    
    if (load_pid_params(&ts_ctx.pid_slave) != ESP_OK) {
        ESP_LOGW(TAG, "Slave PID: Usando defaults.");
        check_pid_integrity(&ts_ctx.pid_slave, 5.0f);
    }
   	*/
   	// 1. Intento de carga desde NVS al iniciar
    // Si falla, podríamos marcar que requiere autotune obligatorio o usar defaults
    /*if (load_pid_params(&ts_ctx.pid_master) != ESP_OK) {
        ESP_LOGW(TAG, "No se encontró calibración. Usando valores por defecto.");
        // pid_init(&ts_ctx.pid, DEFAULT_KP, DEFAULT_KI, DEFAULT_KD); 
    }*/

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
                ESP_LOGE(TAG, "CRITICAL: Surface overheat! SSR OFF");
            
            } else {
                
                ts_ctx.overheat_fault = false;
                
                // --- 3. Lógica de Control: ¿Autotune o PID? ---
                
                if (ts_ctx.autotune_active && ts_ctx.atune.running) {
                    // MODO AUTO-TUNE
                    
                    float power = pid_atune_execute(&ts_ctx.atune, &ts_ctx.pid_master, ts_ctx.data.temp_rtd);
                    ts_ctx.pulse_target = (int)power;

                    if (!ts_ctx.atune.running) {
                        // El proceso terminó solo
                        ts_ctx.autotune_active = false;
                        save_pid_params(&ts_ctx.pid_master);
                        ESP_LOGI(TAG, "Auto-tune finalizado: Kp=%.2f, Ki=%.2f, Kd=%.2f", 
                                 ts_ctx.pid_master.kp, ts_ctx.pid_master.ki, ts_ctx.pid_master.kd);
                    }
                    
                 } else {
                    // Modo de operación normal
                    
                    // A. Master PID: Controla temperatura del AGUA (RTD)
                    // Salida: Temperatura requerida en la PLACA (SP para el Esclavo)
                    float error_agua = ts_ctx.pid_master.setpoint - ts_ctx.data.temp_rtd;
					// 2. PID Maestro: Calcula el setpoint ideal para la placa
					// IMPORTANTE: Aquí pid_compute debe devolver un valor absoluto de temperatura 
					// (ej: si el agua está en 40 y el SP es 45, el PID debería pedir unos 55 o 60 para la placa)
					float plate_sp_requested = pid_compute(&ts_ctx.pid_master, ts_ctx.data.temp_rtd);

                    // 3. Lógica de GRADIENTE DINÁMICO ESTRICTO (Tu premisa)
					// Si el agua está cerca del objetivo, prohibimos que la placa se aleje.
					float max_allowable_gradient = 40.0f;
					
					if (error_agua < 10.0f) {
                         max_allowable_gradient = 20.0f;  // Estacionario: Diferencia mínima
                    } else if (error_agua < 5.0f) {
                         max_allowable_gradient = 0.0f;  // Estamos cerca: Placa pegada al agua para evitar overshoot
                    }
                    
                    // B. Limitador de Seguridad de Cascada (Gradient & Absolute)
                    float plate_sp_limited = plate_sp_requested;
                    // Protección por gradiente (No calentar la placa a >20C que el agua actual)
                    float grad_limit = ts_ctx.data.temp_rtd +  max_allowable_gradient;
                    if (plate_sp_limited > grad_limit) {
                        plate_sp_limited = grad_limit;
                         
                    }
                    // --- MEJORA: CORTE TOTAL POR INERCIA ---
					// Si ya pasamos el setpoint del agua, forzamos que el setpoint de la placa 
					// sea igual al del agua (o incluso menor) para que el Esclavo apague el SSR.
					if (error_agua <= 0) {
    					plate_sp_limited = ts_ctx.pid_master.setpoint - 5.0; // Forzamos enfriamiento
						max_allowable_gradient = 0.0;
					}
                     
                    // Protección por límite absoluto de hardware
                    if (plate_sp_limited > PLATE_ABS_MAX_TEMP) plate_sp_limited = PLATE_ABS_MAX_TEMP;
                    if (plate_sp_limited < 0) plate_sp_limited = OUTPUT_LIM_MIN_SLAVE;
                    
                    // C. Slave PID: Controla temperatura de la PLACA (TC)
                    ts_ctx.pid_slave.setpoint = plate_sp_limited;
                    float power = pid_compute(&ts_ctx.pid_slave, ts_ctx.data.temp_tc);
                    
                    // D. Saturación final de potencia (0-100%)
                    if (isnan(power)) power = 0;
                    if (power > 100.0f) power = 100.0f;
                    if (power < 0.0f) power = 0.0f;
                    
                    ts_ctx.pulse_target = (int)power;

					// --- LOG DE DIAGNÓSTICO MAESTRO ---
					static uint8_t log_counter = 0;
					if (++log_counter >= 10) {
                        float slave_error = ts_ctx.pid_slave.setpoint - ts_ctx.data.temp_tc;
                        
                        ESP_LOGI("DIAG_CTRL", 
                            "AGUA:[Err:%.2f] PLACA:[Err:%.2f | SP_Req:%.1f -> SP_Lim:%.1f] "
                            "INT_M:%.2f | INT_S:%.2f | PWR:%d%% | GRAD:%.1f",
                            error_agua, slave_error, plate_sp_requested, plate_sp_limited, 
                            ts_ctx.pid_master.integral, ts_ctx.pid_slave.integral, 
                            ts_ctx.pulse_target, max_allowable_gradient);
                            
                        log_counter = 0;
                    }
					
					/*
					// Este log te dirá si el Maestro está "loco" pidiendo temperatura de más
					ESP_LOGI("DIAG_M", "ERR_W:%.2f | MASTER_INT:%.2f | SP_PLT_REQ:%.1f | SP_PLT_LIM:%.1f",
         				error_agua, ts_ctx.pid_master.integral, plate_sp_requested, plate_sp_limited);

					// --- LOG DE DIAGNÓSTICO ESCLAVO ---
					// Este log te dirá por qué el SSR está encendido
					float slave_error = ts_ctx.pid_slave.setpoint - ts_ctx.data.temp_tc;
					ESP_LOGI("DIAG_S", "ERR_P:%.2f | SLAVE_INT:%.2f | PWR:%d%%",
         				slave_error, ts_ctx.pid_slave.integral, ts_ctx.pulse_target);
					*/
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
        packet.log_setpoint = ts_ctx.pid_master.setpoint;
       
        
        if (telemetry_queue != NULL) {
            xQueueOverwrite(telemetry_queue, &packet);
        }

    }
}


