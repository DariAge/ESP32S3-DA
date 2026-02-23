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

#define TELEMETRY_QUEUE_LENGTH 1
#define TELEMETRY_QUEUE_ITEM_SIZE sizeof(telemetry_data_t)

QueueHandle_t thermal_queue = NULL;
static const char *TAG = "MAIN";
 
/**
 * @brief Tarea Productora de Sensores (Ejecutada en Core 1)
 */
void vTaskSensorProducer(void *pvParameters) {
    thermal_config_t *sensors = (thermal_config_t *)pvParameters;
    thermal_data_packet_t packet;

    for (;;) {
        packet.status_tc = read_max6675_temp(sensors->max6675_handle, &packet.temp_tc);
        packet.status_rtd = read_max31865_temp(sensors->max31865_handle, &packet.temp_rtd);

        xQueueOverwrite(thermal_queue, &packet);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Muestreo interno a 1Hz
    }
}

void app_main(void) {
    static thermal_config_t sensors;

    // 1. Inicialización NVS y WiFi-Ethernet
    network_manager_init();

    // 2. Inicialización Sensores (Bus SPI)
    init_thermal_bus(&sensors);
  
    // 3. Cola de comunicación entre Cores
    thermal_queue = xQueueCreate(1, sizeof(thermal_data_packet_t));
    
    // 4. Lanzar Tarea de Hardware en Core 1
    xTaskCreatePinnedToCore(vTaskSensorProducer, "SensorProd", 4096, (void *)&sensors, 5, NULL, 1);

    // 5. Esperar conexión antes de lanzar MQTT
    EventBits_t res = network_manager_wait_for_connection(TIMEOUT_WAITFOR_IP );

    if (res & (WIFI_CONNECTED_BIT | ETH_CONNECTED_BIT)) {
		
		if (res & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Conectado vía WIFI. Lanzando telemetría...");
        } else {
            ESP_LOGI(TAG, "Conectado vía ETHERNET. Lanzando telemetría...");
        }
        // 6. Lanzar Tarea de Telemetría en Core 0
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