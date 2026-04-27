#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "ina219.h"
#include "driver/i2c.h"

static const char *TAG = "INA219_TEST";
static const char *TAG1 = "PRODUCER_TASK";
static const char *TAG2 = "DISPLAY_TASK";

// --- Configuración de I2C ---
#define I2C_PORT      0
#define SDA_GPIO      41 
#define SCL_GPIO      42
#define SHUNT_RES     0.1f  // Tu R100
#define MAX_CURR_EXP  0.2f  // 200mA máximo para ganar resolución (tu carga es 60mA)

// --- Queue ---
static QueueHandle_t data_queue = NULL;

// Estructura para el consumidor
typedef struct {
    float bus_voltage;
    float current_ma;
} sensor_data_t;

// --- PRODUCTOR: Lee el sensor ---
void ina219_producer_task(void *pvParameters) {
    
    ESP_ERROR_CHECK(i2cdev_init());
    ina219_t dev;
    memset(&dev, 0, sizeof(ina219_t));

    // 1. Inicialización
    ESP_ERROR_CHECK(ina219_init_desc(&dev, INA219_ADDR_GND_GND, I2C_PORT, SDA_GPIO, SCL_GPIO));
    
    // 2. Configuración
    // GAIN_1 (40mV) es perfecto para tus 60mA (apenas usás el 15% del rango, alta precisión)
    // RES_12BIT_128S hace 128 lecturas por cada dato. ¡Muy estable!
    ESP_ERROR_CHECK(ina219_configure(&dev, 
                                     INA219_BUS_RANGE_32V, 
                                     INA219_GAIN_1, 
                                     INA219_RES_12BIT_128S, 
                                     INA219_RES_12BIT_128S, 
                                     INA219_MODE_CONT_SHUNT_BUS));

    ESP_ERROR_CHECK(ina219_calibrate(&dev, SHUNT_RES));

    sensor_data_t out_data;
    ESP_LOGI(TAG1, "Productor INA219 iniciado...");

    while (1) {
        float v_bus = 0, i_ma = 0;
        
        // Obtenemos valores
        if (ina219_get_bus_voltage(&dev, &v_bus) == ESP_OK &&
            ina219_get_current(&dev, &i_ma) == ESP_OK) {
            
            out_data.bus_voltage = v_bus;
            out_data.current_ma = i_ma * 1000.0f; // Convertir A a mA

            // Enviamos a la cola (si está llena, ignoramos)
            xQueueSend(data_queue, &out_data, 0);
        } else {
            ESP_LOGE(TAG1, "Error de lectura I2C");
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz es suficiente para monitoreo
    }
}

// --- CONSUMIDOR: Imprime en consola ---
void display_consumer_task(void *pvParameters) {
    sensor_data_t in_data;
    ESP_LOGI(TAG2, "Consumidor iniciado...");
    
    
    while (1) {
        if (xQueueReceive(data_queue, &in_data, portMAX_DELAY)) {
            ESP_LOGI(TAG2,"DATA >> Bus: %.2f V | Corriente: %.2f mA\n", 
                   in_data.bus_voltage, in_data.current_ma);
        }
    }
}


void app_main(void)
{
    // Pequeño delay para que la consola se estabilice y no te pierdas el primer mensaje
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Iniciando App Main...");

    // Crear la cola para 10 elementos
    data_queue = xQueueCreate(10, sizeof(sensor_data_t));
	
	if (data_queue == NULL) {
        ESP_LOGE(TAG, "Error: No se pudo crear la cola. Abortando.");
        return; 
    }
    
    xTaskCreate(ina219_producer_task, "ina_prod", 4096, NULL, 5, NULL);
    xTaskCreate(display_consumer_task, "disp_cons", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "Tareas creadas exitosamente.");
}
