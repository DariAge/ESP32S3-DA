/*
 * encoder_da.c
 *
 *  Created on: 14 ene. 2026
 *      Author: DaroA
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "encoder_da.h"
#include "esp_timer.h"

static const char *TAG = "ENCODER_CALIBRACION";
pcnt_unit_handle_t pcnt_unit = NULL;
pcnt_channel_handle_t pcnt_chan = NULL;

static int last_pulse_count = 0;
static uint32_t last_time_us = 0;


void init_encoder_pcnt() {
    ESP_LOGI(TAG, "Instalando Unidad PCNT...");

    pcnt_unit_config_t unit_config = {
        .low_limit = PCNT_LOW_LIMIT,
        .high_limit = PCNT_HIGH_LIMIT,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    ESP_LOGI(TAG, "Instalando Canal PCNT...");
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = ENCODER_A_PIN,
        .level_gpio_num = -1, // Solo velocidad, ignoramos B
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // Acción: Incrementar en flanco de subida, nada en bajada (para 46kHz es suficiente)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
	
	// Filtro de Glitch (Importante en S3 si hay ruido del motor)
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Habilitar e Iniciar (Funciones validadas en tu pulse_cnt.c)
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit)); // <--- Función corregida
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}

float encoder_get_rpm()
{
	int current_pulses = 0;
    uint32_t current_time_us = esp_timer_get_time(); // Tiempo exacto en microsegundos
    
    // 1. Obtener cuenta total acumulada (SIN RESETEAR)
    pcnt_unit_get_count(pcnt_unit, &current_pulses);
    
    // 2. Calcular delta de tiempo real (por si el RTOS se atrasó)
    float dt = (float)(current_time_us - last_time_us) / 1000000.0f;
    
    // 3. Calcular delta de pulsos (manejando el overflow del PCNT)
    int delta_pulses = current_pulses - last_pulse_count;
    
    // Manejo básico de overflow si el PCNT llega al límite
    if (delta_pulses < -10000) delta_pulses += PCNT_HIGH_LIMIT; 
    else if (delta_pulses > 10000) delta_pulses -= PCNT_HIGH_LIMIT;

    // 4. Guardar para la próxima
    last_pulse_count = current_pulses;
    last_time_us = current_time_us;

    if (dt <= 0) return 0.0f;

    // 5. Cálculo preciso: (Pulsos / dt) = Hz -> (Hz * 60) / PPR = RPM
    float rpm = ((float)delta_pulses / dt) * 60.0f / ENCODER_PPR;
    
    return (rpm > 0) ? rpm : 0; // Evitar RPM negativas si no hay reversa
	
	/*
	int pulses = 0;
	
	// Obtener cuenta acumulada
    ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulses));
        
    // Reiniciar contador para la siguiente ventana
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
	
	  // Cálculo:
    // 1. Convertir pulsos a frecuencia (Hz)
    float frequency_hz = (float)pulses / SAMPLE_TIME_SECONDS;
    
    // 2. Convertir frecuencia a RPM: (Hz * 60) / PPR
    float rpm = (frequency_hz * 60.0f) / ENCODER_PPR;
	
	return rpm;*/
}
