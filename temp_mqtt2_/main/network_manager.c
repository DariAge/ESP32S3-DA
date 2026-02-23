/*
 * network_manager.c
 *
 *  Created on: 9 feb. 2026
 *      Author: DaroA
 */
#include "esp_netif.h"
#include "network_manager.h"
#include "wifi_da.h"
#include "eth_da.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stddef.h>
#include "inttypes.h"
#include "esp_eth_com.h"


#ifndef ETH_CMD_G_ETH_LINK
#define ETH_CMD_G_ETH_LINK (0x101) // Valor estándar en esp_eth_com.h
#endif

static const char *TAG = "NET_MANAGER";

// Grupo de eventos para sincronizar el estado de la red
EventGroupHandle_t s_network_event_group;

// Variables externas que vienen de tu lógica de wifi_da (lista de preferidos)
extern wifi_credential_t *s_preferred_list;
extern size_t s_preferred_list_count;


// Interfaces Netif (necesarias para configurar prioridades)
static esp_netif_t* s_eth_netif = NULL;
static esp_netif_t* s_wifi_netif = NULL;

/**
 * @brief Manejador de eventos centralizado
 * Gestiona eventos de WiFi, Ethernet y la obtención de direcciones IP.
 */
void network_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                
                ESP_LOGI(TAG, "WiFi iniciado, intentando conectar...");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                // Podrías implementar aquí un contador de reintentos
         
                xEventGroupClearBits(s_network_event_group, WIFI_CONNECTED_BIT);
                ESP_LOGW(TAG, "WiFi desconectado, reintentando...");
                break;
            case WIFI_EVENT_AP_START:
            	ESP_LOGI(TAG, "EVENTO: SoftAP iniciado. IP: 192.168.4.1");
            default:
                break;
        }
    } 
    else if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Cable Ethernet conectado(Link Up).");
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Cable Ethernet desconectado (Link Down).");
                xEventGroupClearBits(s_network_event_group, ETH_CONNECTED_BIT);
                break;
            default:
                break;
        }
    }
    else if (event_base == IP_EVENT) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ESP_LOGI(TAG, "IP obtenida por WiFi: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(s_network_event_group, WIFI_CONNECTED_BIT);
        } else if (event_id == IP_EVENT_ETH_GOT_IP) {
            ESP_LOGI(TAG, "IP obtenida por Ethernet: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(s_network_event_group, ETH_CONNECTED_BIT);
        }
    }
}

void network_manager_init(void)
{
    // --- PARTE 1: NVS ---
    //nvs_flash_erase(); //descomentar cuando quiero borrar credenciales
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupta o sin formato. Borrando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- PARTE 2: INICIALIZACIÓN BASE DEL STACK ---
    ESP_ERROR_CHECK(esp_netif_init());
    s_network_event_group = xEventGroupCreate();//CREACION DE CAMPO DE BITS DE ESTADO!!
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // --- PARTE 3: REGISTRO DE HANDLERS ---
    // Registramos el handler unificado para las tres bases de eventos. IP tanto para WiFi como Ethernet
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP , &network_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,  IP_EVENT_ETH_GOT_IP , &network_event_handler, NULL));

    // --- PARTE 4: INICIALIZACIÓN DE HARDWARE --
    
    // 4.1. WiFi (Creamos las interfaces por defecto)
    s_wifi_netif = esp_netif_create_default_wifi_sta();
  
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	// Seteamos almacenamiento en RAM para no desgastar la Flash con cada cambio de modo
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
	
	  
    // --- ETHERNET ---
    if (eth_driver_init() == ESP_OK) {
        s_eth_netif = eth_get_netif(); // Debes asegurar que eth_da devuelva el esp_netif_t
        esp_eth_handle_t eth_handle = eth_get_handle();
        if (eth_handle) {
            esp_eth_start(eth_handle);
            ESP_LOGI(TAG, "Ethernet arrancado.");
        }
    }
 

    // 6. Lógica de Decisión Inicial
    // Esperamos un momento breve para ver si Ethernet levanta (Link Up es rápido)
    vTaskDelay(pdMS_TO_TICKS(500)); 

    // Consultamos credenciales
    if (s_preferred_list) { free(s_preferred_list); s_preferred_list = NULL; }
    ret = wifi_manager_get_preferred_list(&s_preferred_list, &s_preferred_list_count);

    if (ret == ESP_OK && s_preferred_list_count > 0) {
        ESP_LOGI(TAG, "Iniciando WiFi Station como respaldo...");
        wifi_init_station(); 
    } else {
        // Solo abrimos AP si no hay Ethernet activo
        EventBits_t bits = xEventGroupGetBits(s_network_event_group);
        if (!(bits & ETH_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "Sin cable y sin WiFi. Modo Provis.");
            wifi_init_softap();
            xEventGroupSetBits(s_network_event_group, WIFI_PROVISIONING_BIT);
        }
    }
	
}

EventBits_t network_manager_wait_for_connection(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Esperando conectividad de red...");

    // 1. Esperamos los bits de conexión O el de provisionamiento
    EventBits_t bits = xEventGroupWaitBits(
        s_network_event_group,
        WIFI_CONNECTED_BIT | ETH_CONNECTED_BIT | WIFI_PROVISIONING_BIT | ETH_HARDWARE_FAULT_BIT,
        pdFALSE,        // No limpiar bits
        pdFALSE,        // Desbloquear con CUALQUIERA
        pdMS_TO_TICKS(timeout_ms)
    );

    // 2. CASO ESPECIAL: Modo Provisionamiento (WiFi Manager)
    if (bits & WIFI_PROVISIONING_BIT) {
        ESP_LOGW(TAG, "Sistema en modo PROVISIONAMIENTO..");
        while(1) {
         	vTaskDelay(pdMS_TO_TICKS(10000));
            ESP_LOGI(TAG, "Esperando configuración vía SoftAP...");
            // Si durante el SoftAP el usuario conecta un cable Ethernet:
            if (xEventGroupGetBits(s_network_event_group) & ETH_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Se detectó conexión Ethernet durante el provisionamiento. Continuando...");
                return xEventGroupGetBits(s_network_event_group);
            }
       }
    }

    // 3. CASO ESPECIAL: Falla de hardware Ethernet
    if (bits & ETH_HARDWARE_FAULT_BIT) {
        ESP_LOGE(TAG, "Error detectado en el controlador Ethernet.");
        // Aquí podrías decidir si seguir solo con WiFi o alertar al usuario
    }

    // 4. ÉXITO: Tenemos IP
    if (bits & (WIFI_CONNECTED_BIT | ETH_CONNECTED_BIT)) {
        if (bits & ETH_CONNECTED_BIT) ESP_LOGI(TAG, "Conectado vía Ethernet.");
        if (bits & WIFI_CONNECTED_BIT) ESP_LOGI(TAG, "Conectado vía WiFi.");
        return bits;
    }

    // 5. TIMEOUT: No pasó nada de lo anterior
    ESP_LOGE(TAG, "Timeout: No se obtuvo conexión después de %" PRIu32 " ms.", timeout_ms);
    return bits;
}

EventGroupHandle_t network_manager_get_event_group(void)
{
    return s_network_event_group;
}

