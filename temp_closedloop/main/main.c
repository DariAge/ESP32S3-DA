/**
 * main.c
 * @brief: Lectura de Sensores de temperatura (Core 1) y
 * sistema de Telemetría Adafruit (Core 0).
 * Respecto de la versión original de temp_mqtt: 
 * Agregado de conectividad via Ethernet (aparte de WiFi) y gestor de red
 */
 
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "wifi_da.h"
#include "thermal_sensors_da.h"
#include "telemetry_task.h" // Cabecera para colas y tarea de red
#include "network_manager.h"
#include "thermal_control.h"

#define TELEMETRY_QUEUE_LENGTH 1
#define TELEMETRY_QUEUE_ITEM_SIZE sizeof(telemetry_data_t)

QueueHandle_t telemetry_queue = NULL;
static const char *TAG = "MAIN";

void app_main(void) {
    static thermal_config_t sensors;

    // 1. Inicialización NVS y WiFi-Ethernet
    network_manager_init();

    // 2. Inicialización Sensores (Bus SPI)
    init_thermal_bus(&sensors);
  
    // 3. Cola de comunicación entre Cores
    telemetry_queue = xQueueCreate(1, sizeof(telemetry_data_t));
    
    // 4. Inicializar el sistema térmico pasándole el handler del timer
	thermal_control_init(&sensors);
    
    //INGRESO DE DATO: POR AHORA HARCODEADo, A FUTURO INGRESADO POR USUARIO
    thermal_set_target(100.0f); // El sistema intentará llegar a 100°C al arrancar

    // 5. Lanzar Tareas de Hardware en Core 1
	xTaskCreatePinnedToCore(vTaskTemperatureControl, "TempCtrl", 4096, NULL, 10, NULL, 1);

    // 6. Esperar conexión antes de lanzar MQTT
    EventBits_t res = network_manager_wait_for_connection(TIMEOUT_WAITFOR_IP );

    if (res & (WIFI_CONNECTED_BIT | ETH_CONNECTED_BIT)) {
		
		if (res & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Conectado vía WIFI. Lanzando telemetría...");
        } else {
            ESP_LOGI(TAG, "Conectado vía ETHERNET. Lanzando telemetría...");
        }
        // 7. Lanzar Tarea de Telemetría en Core 0
        xTaskCreatePinnedToCore(vTaskMqttAdafruit, "AdafruitIO", 8192, NULL, 4, NULL, 0);
    } else {
        // Si res es 0 o no tiene los bits, es porque hubo Timeout
        ESP_LOGE("MAIN", "Error: Timeout agotado sin conexión IP.");
        // Aquí podrías decidir si reiniciar o intentar modo offline
    }

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}