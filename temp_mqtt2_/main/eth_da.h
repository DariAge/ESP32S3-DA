/*
 * eth_da.h
 *
 *  Created on: 9 feb. 2026
 *      Author: DaroA
 */

#ifndef MAIN_ETH_DA_H_
#define MAIN_ETH_DA_H_

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"

/**
 * @brief Inicializa el hardware y el driver Ethernet para el chip ENC28J60.
 * * Configura el bus SPI, registra el dispositivo MAC/PHY 
 * y vincula la interfaz a esp-netif.
 * * @return 
 * - ESP_OK: Éxito
 * - ESP_FAIL: Error en la inicialización
 */
esp_err_t eth_driver_init(void);

/**
 * @brief Obtiene el manejador del driver Ethernet.
 * * @return esp_eth_handle_t Manejador del driver.
 */
esp_eth_handle_t eth_get_handle(void);

esp_netif_t* eth_get_netif(void); //NUEVO

#endif /* MAIN_ETH_DA_H_ */
