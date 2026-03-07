/*
 * pid_controller.c
 *  Funciones para el control PID.
 *  Created on: 24 feb. 2026
 *      Author: DaroA
 */

#include "pid_controller.h"

static const char *TAG = "NVS_PID";

void pid_init(pid_ctrl_t *pid, float kp, float ki, float kd, float ts, float min, float max) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->ts = ts;
    pid->output_limit_min = min;
    pid->output_limit_max = max;
    pid->integral = 0;
    pid->last_measured = 0; // Usamos la medida anterior en lugar del error
    pid->setpoint = 0;
}


float pid_compute(pid_ctrl_t *pid, float measured_value) {

	float error = pid->setpoint - measured_value;
    
    // 1. Proporcional
    float p_term = pid->kp * error;
    
    // 2. Integral con integración rectangular
    pid->integral += (pid->ki * error * pid->ts);
    
    // --- MEJORA: Anti-Windup (Clamping) ---
    // Si la integral sola ya supera los límites, la frenamos
    if (pid->integral > pid->output_limit_max) pid->integral = pid->output_limit_max;
    if (pid->integral < pid->output_limit_min) pid->integral = pid->output_limit_min;
    
    // 3. Derivada sobre la medida (evita "kick" al cambiar setpoint)
    // d/dt = (medida_actual - medida_previa) / ts
    float d_term = -pid->kd * (measured_value - pid->last_measured) / pid->ts;
    
    float output = p_term + pid->integral + d_term;
    
    // 4. Saturación final de la salida
    if (output > pid->output_limit_max) {
        output = pid->output_limit_max;
    } else if (output < pid->output_limit_min) {
        output = pid->output_limit_min;
    }
    
    pid->last_measured = measured_value;
    return output;
}


// --- FUNCIONES DE AUTO-TUNE ---

bool pid_atune_start(pid_atune_t *atune, pid_ctrl_t *pid, float current_temp) {
    // Validación 1: Setpoint mínimo para que haya oscilación térmica útil
    if (pid->setpoint < 50.0f) {
        ESP_LOGW(TAG, "Autotune abortado: Setpoint (%.1f) demasiado bajo.", pid->setpoint);
        return false;
    }

    // Validación 2: Temperatura actual debe estar por debajo del setpoint
    if (current_temp > (pid->setpoint + 2.0f)) {
        ESP_LOGW(TAG, "Autotune abortado: Temp actual (%.1f) mayor al setpoint.", current_temp);
        return false;
    }

    // Inicialización de la estructura de estado
    atune->running = true;
    atune->noise_band = 0.5f; // Histéresis de 0.5 grados
    atune->peak_count = 0;
    atune->last_peak_time = 0;
    atune->sum_periods = 0;
    atune->sum_amplitude = 0;
    atune->last_direction = 0;

    ESP_LOGI(TAG, "Autotune iniciado. Setpoint: %.1f C", pid->setpoint);
    return true;
}


float pid_atune_execute(pid_atune_t *atune, pid_ctrl_t *pid, float measured_value) {
    float output;
    
    // 1. Lógica de Relevo (Relay ON/OFF)
    if (measured_value > pid->setpoint + atune->noise_band) {
        output = pid->output_limit_min;
    } else if (measured_value < pid->setpoint - atune->noise_band) {
        output = pid->output_limit_max;
    } else {
        // En la banda de ruido mantenemos el estado anterior
        output = (atune->last_direction == 1) ? pid->output_limit_min : pid->output_limit_max;
    }

    // 2. Detección de cruce y picos para Ziegler-Nichols
    int current_direction = (measured_value > pid->setpoint) ? 1 : -1;
    
    if (atune->last_direction != 0 && current_direction != atune->last_direction) {
        // Se produjo un cruce por el setpoint
        unsigned long now = esp_timer_get_time() / 1000; // ms
        
        if (atune->last_peak_time != 0) {
            float period = (float)(now - atune->last_peak_time) * 2.0 / 1000.0; // Periodo completo en seg
            float amplitude = fabsf(measured_value - pid->setpoint);
            
            atune->sum_periods += period;
            atune->sum_amplitude += amplitude;
            atune->peak_count++;
            
            // Si ya tenemos 4 oscilaciones estables, calculamos
            if (atune->peak_count >= 4) {
                float Tu = atune->sum_periods / atune->peak_count;      // Periodo promedio
                float A  = atune->sum_amplitude / atune->peak_count;    // Amplitud promedio
                
                // Ganancia Crítica Ku
                float Ku = (4.0 * pid->output_limit_max) / (3.14159 * A);
                
                // Fórmulas clásicas de Ziegler-Nichols para PID
                pid->kp = 0.60 * Ku;
                pid->ki = 1.20 * Ku / Tu;
                pid->kd = 0.075 * Ku * Tu;
                
                atune->running = false; // Fin del proceso
            }
        }
        atune->last_peak_time = now;
    }
    
    atune->last_direction = current_direction;
    return output;
}


/**
 * @brief Guarda los parámetros del PID en la NVS
 */
esp_err_t save_pid_params(pid_ctrl_t *pid) {
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open("pid_storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    // Guardamos los valores como blobs o floats individuales
    nvs_set_blob(my_handle, "kp", &pid->kp, sizeof(float));
    nvs_set_blob(my_handle, "ki", &pid->ki, sizeof(float));
    nvs_set_blob(my_handle, "kd", &pid->kd, sizeof(float));

    err = nvs_commit(my_handle);
    nvs_close(my_handle);
    
    ESP_LOGI(TAG, "Parámetros PID guardados en Flash.");
    return err;
}

/**
 * @brief Intenta cargar los parámetros del PID desde la NVS
 * @return ESP_OK si los cargó, ESP_ERR_NVS_NOT_FOUND si no existen
 */
esp_err_t load_pid_params(pid_ctrl_t *pid) {
    nvs_handle_t my_handle;
    esp_err_t err;
    size_t size = sizeof(float);

    err = nvs_open("pid_storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_blob(my_handle, "kp", &pid->kp, &size);
    if (err == ESP_OK) err = nvs_get_blob(my_handle, "ki", &pid->ki, &size);
    if (err == ESP_OK) err = nvs_get_blob(my_handle, "kd", &pid->kd, &size);

    nvs_close(my_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Parámetros PID cargados desde Flash: Kp:%.2f, Ki:%.2f, Kd:%.2f", 
                 pid->kp, pid->ki, pid->kd);
    }
    return err;
}
