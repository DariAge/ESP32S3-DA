/*
 * ex telemetry_sender_task_2.c
 *
 *	Modificado para utilizar con Adafruit IO
 * Created on: 25 oct. 2025
 * Modified on: 07 feb. 2026
 * Author: DaroA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <mqtt_client.h>
#include "telemetry_task.h"
#include <inttypes.h>

#include "wifi_da.h"
#include "thermal_sensors_da.h" // Para la estructura thermal_data_packet_t

#include "network_manager.h"

static const char *TAG = "MQTT_TASK";
static const char *TAG2 = "MQTT_EVENT_GROUP";

// Bits de control de eventos
#define MQTT_CONNECTED_BIT BIT0

static esp_mqtt_client_handle_t mqtt_client = NULL; // Cliente MQTT global

extern QueueHandle_t thermal_queue; // Cola definida en main.c

static EventGroupHandle_t mqtt_event_group; // Estado de la conexión MQTT


/**
 * @brief Manejador de eventos del cliente MQTT
 * @param handler_args Argumentos del manejador
 * @param base Base del evento
 * @param event_id ID del evento
 * @param event_data Datos del evento
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client;
    
    // CRUCIAL: Asignar el handle cuando se inicializa.
    if (mqtt_client == NULL) {
        mqtt_client = event->client; 
    }
    
    int msg_id;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG2, "MQTT_EVENT_CONNECTED. Conectado a Adafruit IO.");
        xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        
        // Opcional: Suscribirse a un tema para comandos
        msg_id = esp_mqtt_client_subscribe(event->client, "termocupla/command", 0);
        ESP_LOGI(TAG2, "Suscrito al tema 'termocupla/command', msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG2, "MQTT_EVENT_DISCONNECTED. Desconectado del Broker.");
        xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_PUBLISHED:
        // Se activa después de que el broker confirma la recepción del mensaje
        ESP_LOGI(TAG2, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        // Si el ESP32 recibe un mensaje (ej: un comando)
        ESP_LOGI(TAG2, "Mensaje recibido - TEMA=%.*s DATA=%.*s", 
        		 event->topic_len, event->topic, event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG2, "MQTT_EVENT_ERROR. Code:%d", event->error_handle->esp_transport_sock_errno);
        break;
        
    default:
         ESP_LOGI(TAG2, "Otro evento MQTT ID:%" PRIi32, event_id);
        break;
    }
}

/**
 * @brief Tarea de Telemetría Adafruit IO (Ejecutada en Core 0)
 * Consume datos de la cola y los publica vía MQTT.
 */
 
void vTaskMqttAdafruit(void *pvParameters) {
    
    thermal_data_packet_t rx_data;	    //buffer de queue datos
    
    //credenciales SSID - PWD - AIO_USER - AIO_PWD
    wifi_credential_t *creds = NULL;		
    size_t count = 0;					
    
    char topic_tc[256];					//buffer de string de topic
    char topic_rtd[256];				//buffer de string de topic
    char payload[16];					//buffer de datos a publicar 
    char aio_user[128] = {0};			//copia para trabajar de manera local
    char aio_key[128] = {0};			//copia para trabajar de manera local
    
    bool mqtt_started = false;			//flag de inicio de transmision mqtt
    
    // 1. Cargar credenciales (creds) desde NVS y validar 
    esp_err_t ret = wifi_manager_get_preferred_list(&creds, &count);
	
    if (ret == ESP_OK && count > 0) {
        if (strlen(creds[0].aio_user) > 0 && strlen(creds[0].aio_key) > 0) {
            strncpy(aio_user, creds[0].aio_user, sizeof(aio_user) - 1);
            strncpy(aio_key, creds[0].aio_key, sizeof(aio_key) - 1);
            
            // Preparar strings de tópicos dinámicos
            snprintf(topic_tc, sizeof(topic_tc), "%s/feeds/termocupla", aio_user);
            snprintf(topic_rtd, sizeof(topic_rtd), "%s/feeds/pt100", aio_user);
            
            ESP_LOGI(TAG, "Configuración de Adafruit cargada para el usuario: %s", aio_user);
        } else {
            ESP_LOGE(TAG, "Credenciales de NVS encontradas pero sin datos de Adafruit (User/Key vacíos).");
            free(creds);
            vTaskDelete(NULL);
            return;
        }
        free(creds); // Liberamos la memoria dinámica de la lista
    } else {
        ESP_LOGE(TAG, "No hay configuración en NVS. Configure el dispositivo vía Portal Captivo.");
        if (creds) free(creds);
        vTaskDelete(NULL);
        return;
    }	
    
    // 2. Preparar Grupo de Eventos e Inicializar MQTT
    mqtt_event_group = xEventGroupCreate();
    
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = AIO_SERVER,
        .broker.address.port = AIO_PORT,
        .credentials.username = aio_user, //SE INGRESAN POR PORTAL AP
        .credentials.authentication.password = aio_key,
    };
	
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    for (;;) {
        // 3. Esperar datos de la cola de sensores (bloqueante)
        if (xQueueReceive(thermal_queue, &rx_data, portMAX_DELAY) == pdPASS) {
            
           //4. Chequear estado de red (Capa Física/IP)
            EventBits_t net_bits = xEventGroupGetBits(network_manager_get_event_group());
			bool has_ip = (net_bits & (WIFI_CONNECTED_BIT | ETH_CONNECTED_BIT));
			
			if(has_ip)
			{
				// Si hay red pero el servicio MQTT no arrancó, lo iniciamos
				if (!mqtt_started) 
				{
                    esp_mqtt_client_start(mqtt_client);
                    mqtt_started = true;
                }
                 // Esperamos hasta 2 segundos a que el MQTT esté realmente logueado
                EventBits_t mqtt_bits = xEventGroupWaitBits(mqtt_event_group, 
                                                           MQTT_CONNECTED_BIT, 
                                                           pdFALSE, pdTRUE, 
                                                           pdMS_TO_TICKS(2000));
                if (mqtt_bits & MQTT_CONNECTED_BIT)
                {
					// 5. Publicación de datos con validación de estado del sensor
           			 if (rx_data.status_tc == ESP_OK) {
               			 snprintf(payload, sizeof(payload), "%.2f", rx_data.temp_tc);
                		 esp_mqtt_client_publish(mqtt_client, topic_tc, payload, 0, 1, 0);
                		 ESP_LOGD(TAG2, "TC Publicada: %s", payload);
          		     }else {
                        ESP_LOGW(TAG2, "TC con error de lectura. Publicación omitida.");
                     } 

            		 if (rx_data.status_rtd == ESP_OK) {
               			 snprintf(payload, sizeof(payload), "%.2f", rx_data.temp_rtd);
               			 esp_mqtt_client_publish(mqtt_client, topic_rtd, payload, 0, 1, 0);
               			 ESP_LOGD(TAG2, "RTD Publicada: %s", payload);
           			 }else {
                        ESP_LOGW(TAG2, "RTD con error de lectura. Publicación omitida.");
                    }
				
				}else{
                    ESP_LOGE(TAG2, "Timeout: Broker conectado pero sesión MQTT no lista.");
                }
			
			} else {
                // Si perdemos la IP, apagamos el motor MQTT para ahorrar recursos
                if (mqtt_started) {
                    ESP_LOGW(TAG2, "Red caída. Deteniendo cliente MQTT...");
                    esp_mqtt_client_stop(mqtt_client);
                    mqtt_started = false;
                    xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT);
                }
            }
            
            // Delay de seguridad para no saturar el limite de Adafruit IO (Free tier)
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}