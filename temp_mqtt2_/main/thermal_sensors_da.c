/**
 * @file thermal_sensors_da.c
 * @brief Implementación de lectura de sensores MAX6675 y MAX31865 para ESP32-S3.
 * 
 *
 *  Created on: 6 feb. 2026
 *      Author: DaroA
 */

#include "thermal_sensors_da.h"
#include <string.h>

esp_err_t init_thermal_bus(thermal_config_t *config) {
    esp_err_t ret;

    // 1. Configuración del Bus compartido (Adaptado para ESP32-S3)
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32, // Tamaño pequeño suficiente para estos sensores
    };

    // Usamos SPI_DMA_CH_AUTO para que el IDF gestione el GDMA del S3
    ret = spi_bus_initialize(SPI_BUS_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    // 2. Dispositivo MAX6675 (Termocupla K)
    spi_device_interface_config_t dev_max6675 = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0, // CPOL=0, CPHA=0
        .spics_io_num = PIN_CS_MAX6675,
        .queue_size = 3,
    };
    ret = spi_bus_add_device(SPI_BUS_HOST, &dev_max6675, &config->max6675_handle);
    if (ret != ESP_OK) return ret;

    // 3. Dispositivo MAX31865 (RTD PT100)
    spi_device_interface_config_t dev_max31865 = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 1, // CPOL=0, CPHA=1
        .spics_io_num = PIN_CS_MAX31865,
        .queue_size = 3,
    };
    ret = spi_bus_add_device(SPI_BUS_HOST, &dev_max31865, &config->max31865_handle);

    return ret;
}

esp_err_t read_max6675_temp(spi_device_handle_t handle, float *temperature) {
    spi_transaction_t t = {
        .length = 16,
        .rx_buffer = NULL,          // Null cuando se usa SPI_TRANS_USE_RXDATA
        .flags = SPI_TRANS_USE_RXDATA,
    };

    esp_err_t ret = spi_device_transmit(handle, &t);
    if (ret != ESP_OK) return ret;

    // En ESP32-S3, rx_data[0] es el byte más significativo de la recepción
    uint16_t res = (t.rx_data[0] << 8) | t.rx_data[1];
    
    // Bit 2 indica si la termocupla está abierta (desconectada)
    if (res & 0x04) return ESP_ERR_NOT_FOUND; 

    // Los 12 bits de temperatura están del bit 3 al 14
    *temperature = (float)(res >> 3) * 0.25f;
    return ESP_OK;
}

esp_err_t read_max31865_temp(spi_device_handle_t handle, float *temperature) {
    // 0x80: Registro de Configuración, 0xB0: Vbias On + 1-Shot
    uint8_t write_buf[] = {0x80, 0xB0};
    spi_transaction_t t_write = { 
        .length = 16, 
        .tx_buffer = write_buf 
    };
    spi_device_transmit(handle, &t_write);

    // Esperar brevemente a que termine la conversión
    vTaskDelay(pdMS_TO_TICKS(10));

    // Lectura del registro de RTD (0x01 MSB, 0x02 LSB)
    uint8_t read_addr = 0x01;
    uint8_t rx_buf[3] = {0};
    spi_transaction_t t_read = {
        .length = 24, // 8 bits dirección + 16 bits datos
        .tx_buffer = &read_addr,
        .rx_buffer = rx_buf,
    };

    esp_err_t ret = spi_device_transmit(handle, &t_read);
    if (ret == ESP_OK) {
        // rx_buf[1] es MSB, rx_buf[2] es LSB
        uint16_t rtd = ((uint16_t)rx_buf[1] << 8) | rx_buf[2];
        rtd >>= 1; // El bit 0 es bit de falla
        
        // Cálculo para resistencia de referencia de 430 ohms (PT100)
        float resistance = ((float)rtd * 430.0f) / 32768.0f;
        // Aproximación lineal de Callendar-Van Dusen
        *temperature = (resistance - 100.0f) / 0.3851f;
    }
    return ret;
}


