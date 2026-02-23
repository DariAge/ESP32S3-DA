/**
 * @file network_manager.h
 * @brief Orquestador central de interfaces de red (WiFi y Ethernet)
 * 		Created on: 9 feb. 2026
 *      Author: DaroA
 */

#ifndef MAIN_NETWORK_MANAGER_H_
#define MAIN_NETWORK_MANAGER_H_

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


/* --- Bits de Estado del Grupo de Eventos --- */
/** @brief Bit que indica que WiFi Station obtuvo una dirección IP */
#define WIFI_CONNECTED_BIT  	BIT0
/** @brief Bit que indica que el intento de conexión WiFi falló tras los reintentos */
#define WIFI_PROVISIONING_BIT   BIT1
/** @brief Bit que indica que el cable Ethernet está conectado y se obtuvo IP */
#define ETH_CONNECTED_BIT   	BIT2
/** @brief Bit que indica falla por hardware en Ethernet */
#define ETH_HARDWARE_FAULT_BIT  BIT3

#define TIMEOUT_WAITFOR_IP 		10000
/**
 * @brief Inicializa el sistema de red global.
 * * Realiza las siguientes operaciones:
 * 1. Inicializa NVS Flash (necesario para credenciales WiFi).
 * 2. Inicializa el stack TCP/IP (LwIP).
 * 3. Crea el bucle de eventos por defecto de ESP-IDF.
 * 4. Configura los drivers de hardware (llamando a eth_da_driver_init y wifi_init).
 * 5. Registra el event_handler centralizado para IP_EVENT, WIFI_EVENT y ETH_EVENT.
 */
void network_manager_init(void);

/**
 * @brief Retorna el handle del Event Group de red.
 * * Permite que otras capas (como la de MQTT o la aplicación principal)
 * esperen a que cualquiera de las interfaces esté lista.
 * * @return EventGroupHandle_t El handle del grupo de eventos creado en la inicialización.
 */
EventGroupHandle_t network_manager_get_event_group(void);

/**
 * @brief Handler centralizado de eventos de red.
 * * Se encarga de capturar eventos de WiFi, Ethernet e IP para actualizar
 * los bits del Event Group y gestionar reintentos de conexión.
 */
void network_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data);
                          
/**
 * @brief Bloquea la ejecución hasta obtener IP o detecta estados especiales.
 * * Esta función es el "gatekeeper" del main.c. Controla si el sistema
 * puede seguir adelante o si debe quedarse esperando por configuración.
 * * @param timeout_ms Tiempo máximo para esperar la IP (si no hay estados especiales).
 * @return EventBits_t Los bits finales que causaron la salida.
 */
EventBits_t network_manager_wait_for_connection(uint32_t timeout_ms);

#endif /* MAIN_NETWORK_MANAGER_H_ */
