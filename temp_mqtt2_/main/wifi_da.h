/*
 * wifi_da.h
 *
 *  Created on: 20 oct. 2025
 *      Author: DaroA
 */

#ifndef MAIN_WIFI_DA_H_
#define MAIN_WIFI_DA_H_

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h" // Se mantiene para compatibilidad, aunque la lógica del botón no esté aquí.

// Estructura para almacenar credenciales Wi-Fi
typedef struct {
    char ssid[32];
    char password[64];
    char aio_user[64];
    char aio_key[64]; 
    bool is_open; // true si es red abierta (sin contraseña)
} wifi_credential_t;

// Estado de la lista de credenciales cargada desde NVS
//wifi_credential_t *s_preferred_list = NULL;
//size_t s_preferred_list_count = 0;

/**
 * @brief Inicializa el sistema Wi-Fi. Carga credenciales o inicia modo Provisionamiento (SoftAP).
 */
void wifi_manager_init(void);

/**
 * @brief Espera por un tiempo LIMITADO (10 segundos) a que el dispositivo obtenga una IP.
 * Si no conecta, retorna, permitiendo que el resto de la aplicación se inicialice.
 * Esta función ya no bloquea indefinidamente.
 */
void wifi_manager_wait_for_ip(void);

/**
 * @brief Verifica si el dispositivo está actualmente conectado a una red Wi-Fi.
 * @return true si está conectado, false en caso contrario.
 */
bool wifi_manager_is_connected(void);


/**
 * @brief Guarda una lista de credenciales Wi-Fi preferidas en el NVS.
 * @param list Puntero al array de credenciales.
 * @param count Número de credenciales en el array.
 * @return ESP_OK si se guardó correctamente.
 */
esp_err_t wifi_manager_save_preferred_list(const wifi_credential_t *list, size_t count);

/**
 * @brief Obtiene la lista de credenciales Wi-Fi preferidas del NVS.
 * @param list Puntero a puntero donde se asignará la memoria para la lista (debe ser liberada con free()).
 * @param count Puntero donde se almacenará el número de elementos.
 * @return ESP_OK si se cargó correctamente.
 */
 
esp_err_t wifi_manager_get_preferred_list(wifi_credential_t **list, size_t *count);

// Declaración de funciones internas para el cambio de modo
void wifi_init_softap(void); 
void wifi_init_station(void);


#endif /* MAIN_WIFI_DA_H_ */
