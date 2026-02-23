/**
 *  @file thermal_sensors_da.h
 *  @brief Configuración de pines y prototipos para ESP32-S3.
 *
 *  Created on: 6 feb. 2026
 *      Author: DaroA
 */

#ifndef MAIN_THERMAL_SENSORS_DA_H_
#define MAIN_THERMAL_SENSORS_DA_H_

#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// --- Configuración de Pines para ESP32-S3 ---
// Se eligen pines del 1 al 6, evitando el rango 26-32 por conflictos con Flash/PSRAM
#define SPI_BUS_HOST          SPI3_HOST     // Host estándar en S3 (reemplaza VSPI)
#define PIN_NUM_MISO          2             // GPIO 2
#define PIN_NUM_MOSI          1             // GPIO 1
#define PIN_NUM_CLK           3             // GPIO 3

#define PIN_CS_MAX6675        4             // GPIO 4
#define PIN_CS_MAX31865       5             // GPIO 5

// --- Estructuras de Control ---
typedef struct {
    spi_device_handle_t max6675_handle;
    spi_device_handle_t max31865_handle;
} thermal_config_t;

// Estructura compartida entre cores
typedef struct {
    float temp_rtd;
    float temp_tc;
    esp_err_t status_rtd;
    esp_err_t status_tc;
} thermal_data_packet_t;

/**
 * @brief Inicializa el bus SPI2 y añade los dispositivos esclavos.
 */
esp_err_t init_thermal_bus(thermal_config_t *config);

/**
 * @brief Lee la temperatura del MAX6675.
 */
esp_err_t read_max6675_temp(spi_device_handle_t handle, float *temperature);

/**
 * @brief Lee la temperatura del MAX31865.
 */
esp_err_t read_max31865_temp(spi_device_handle_t handle, float *temperature);


void vTaskMqttAdafruit(void *pvParameters); 

#endif /* MAIN_THERMAL_SENSORS_DA_H_ */
