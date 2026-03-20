/*
 * ex telemetry_sender_task_2.c
 *
 *	Modificado para utilizar con Adafruit IO
 *  Modificado para enviar paquetes jSON
 *  Created on: 25 oct. 2025
 *  Last modified on: 03 mar. 2026
 * Author: DaroA
 */

#include "math.h"
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
#include "cJSON.h"  // ESP-IDF standard JSON library
#include "wifi_da.h"
#include "thermal_sensors_da.h" // Para la estructura thermal_data_packet_t

#include "network_manager.h"

static const char *TAG = "MQTT_TASK";
static const char *TAG2 = "MQTT_EVENT_GROUP";

// Bits de control de eventos
#define MQTT_CONNECTED_BIT BIT0

static esp_mqtt_client_handle_t mqtt_client = NULL; // Cliente MQTT global

extern QueueHandle_t telemetry_queue; // Cola definida en main.c

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
    
    telemetry_data_t rx_data;	    //buffer de queue datos
    
    //credenciales SSID - PWD - AIO_USER - AIO_PWD
    wifi_credential_t *creds = NULL;		
    size_t count = 0;					
    //PARA PASAR A FORMATO FEEDS SEPARADOS, DESCOMENTAR 4 LÍNEAS
    //char topic_tc[256];					//buffer de string de topic
    //char topic_rtd[256];				//buffer de string de topic
    //char topic_sp[256];
     //char payload[16];					//buffer de datos a publicar 
    char topic_full[256];				//buffer de string de topic
    char aio_user[128] = {0};			//copia para trabajar de manera local
    char aio_key[128] = {0};			//copia para trabajar de manera local
    
    bool mqtt_started = false;			//flag de inicio de transmision mqtt
    
    // Variables de sincronismo
    TickType_t xLastWakeTime = 0;
    bool sync_initialized = false;
    const TickType_t xPublishInterval = pdMS_TO_TICKS(5000); // 5 segundos para Adafruit

    // 1. Cargar credenciales (creds) desde NVS y validar 
    esp_err_t ret = wifi_manager_get_preferred_list(&creds, &count);
	
    if (ret == ESP_OK && count > 0) {
        if (strlen(creds[0].aio_user) > 0 && strlen(creds[0].aio_key) > 0) {
            strncpy(aio_user, creds[0].aio_user, sizeof(aio_user) - 1);
            strncpy(aio_key, creds[0].aio_key, sizeof(aio_key) - 1);
            
            //PARA PASAR A FORMATO FEEDS SEPARADOS, DESCOMENTAR 3 LÍNEAS
       		//snprintf(topic_tc, sizeof(topic_tc), "%s/feeds/termocupla", aio_user);
            //snprintf(topic_rtd, sizeof(topic_rtd), "%s/feeds/pt100", aio_user);
            //snprintf(topic_rtd, sizeof(topic_sp), "%s/feeds/setpoint", aio_user);
            
            // Adafruit allows receiving JSON in a group feed or a specific data feed
        	snprintf(topic_full, sizeof(topic_full), "%s/feeds/sistema-control", aio_user);
        	free(creds);
            ESP_LOGI(TAG, "Configuración de Adafruit cargada para el usuario: %s", aio_user);
        } else {
            ESP_LOGE(TAG, "Credenciales de NVS encontradas pero sin datos de Adafruit (User/Key vacíos).");
            free(creds);
            vTaskDelete(NULL);
            return;
        }
        //free(creds); // Liberamos la memoria dinámica de la lista
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
	
	//Impresión offline del sistema de calefacción
    printf("\n--- SISTEMA DE AGITACIÓN INICIADO ---\n");
    printf("LEYENDA RED: [W:WiFi, E:Eth] (C:Conectado, D:Buscando, O:Desactivado)\n");
    printf("FORMATO: DATA:RTD,TC,SP,PWR,W_STAT,E_STAT\n\n");

    for (;;) {
        // 3. Esperar datos de la cola de sensores (timeout 1 seg para poder ver log de estado en consola)
        if (xQueueReceive(telemetry_queue, &rx_data, pdMS_TO_TICKS(1000)) == pdPASS) {
 			
 			// Obtenemos el estado de las redes para el log
            EventBits_t net_status = xEventGroupGetBits(network_manager_get_event_group());
            char wifi_char = (net_status & WIFI_CONNECTED_BIT) ? 'C' : 'D';
            char eth_char = (net_status & ETH_CONNECTED_BIT) ? 'C' : 'O';

            // Imprimimos siempre en consola para monitoreo local (Cada 1s si hay dato)
            // Esto permite que un script de Python o el IDE grafiquen con baja latencia.
            printf("DATA:%.2f,%.2f,%.1f,%d,%c,%c\n", 
                   rx_data.log_data.temp_rtd, rx_data.log_data.temp_tc,
                   rx_data.log_setpoint, rx_data.log_power, wifi_char,eth_char);
       		 /*
        		printf("DATA:%.2f,%.2f,%.1f,%d,%c,%c\n",
              	 rx_data.log_data.temp_rtd,
              	 rx_data.log_data.temp_tc,
               	rx_data.log_setpoint,
               	rx_data.log_power,
               	get_net_status_char(wifi_connected),
               	get_net_status_char(eth_connected));
        	*/
        }   
        
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
            /*
            EventBits_t mqtt_bits = xEventGroupWaitBits(mqtt_event_group, 
                                                        MQTT_CONNECTED_BIT, 
                                                        pdFALSE, pdTRUE, 
                                                        pdMS_TO_TICKS(2000));
            */
            EventBits_t mqtt_bits = xEventGroupGetBits(mqtt_event_group);
            if (mqtt_bits & MQTT_CONNECTED_BIT)
            {
				// --- SINCRONISMO PARA ADAFRUIT ---
                if (!sync_initialized) {
                    xLastWakeTime = xTaskGetTickCount();
                    sync_initialized = true;
                    ESP_LOGI(TAG, "Sincronismo de red establecido.");
                }
				TickType_t xNow = xTaskGetTickCount();
				if ((xNow - xLastWakeTime) >= xPublishInterval){
				// Create JSON Object
                    cJSON *root = cJSON_CreateObject();
					
					// 5. Publicación de datos con validación de estado del sensor
           			 if (rx_data.log_data.status_tc == ESP_OK) {
               			 cJSON_AddNumberToObject(root, "temp_tc", rx_data.log_data.temp_tc);
                		 //PARA PASAR A FORMATO FEEDS SEPARADOS, DESCOMENTAR 2 LÍNEAS
                		 //snprintf(payload, sizeof(payload), "%.2f", rx_data.log_data.temp_tc);
                		 //esp_mqtt_client_publish(mqtt_client, topic_tc, payload, 0, 1, 0);
                		 //ESP_LOGD(TAG2, "TC Publicada: %s", payload);
          		     }else {
                        ESP_LOGW(TAG, "TC con error de lectura. Publicación omitida.");
                     } 

            		 if (rx_data.log_data.status_rtd == ESP_OK) {
               			 float val = roundf(rx_data.log_data.temp_rtd * 100.0f) / 100.0f;
               			 cJSON_AddNumberToObject(root, "temp_rtd", val);
               			 //PARA PASAR A FORMATO FEEDS SEPARADOS, DESCOMENTAR 2 LÍNEAS
               			 //snprintf(payload, sizeof(payload), "%.2f", val);
               			 //esp_mqtt_client_publish(mqtt_client, topic_rtd, payload, 0, 1, 0);
               			 //ESP_LOGD(TAG2, "RTD Publicada: %s", payload);
           			 }else {
                        ESP_LOGW(TAG, "RTD con error de lectura. Publicación omitida.");
                     }
					 
					//PARA PASAR A FORMATO FEEDS SEPARADOS, DESCOMENTAR 4 LINEAS Y COMENTAR LA QUINTA
					//float sp_round = roundf(rx_data.log_setpoint * 10.0f) / 10.0f;
                    //snprintf(payload, sizeof(payload), "%.1f", sp_round);
                    //cJSON_AddNumberToObject(root, "setpoint", sp_round); 
                    //esp_mqtt_client_publish(mqtt_client, topic_sp, payload, 0, 1, 0);
					cJSON_AddNumberToObject(root, "setpoint", rx_data.log_setpoint);   
					
					cJSON_AddNumberToObject(root, "power", rx_data.log_power);
                    
                	// Convert to string
                	char *json_string = cJSON_PrintUnformatted(root);
					if (json_string != NULL) {
                		// Publish everything in a single message
                		int msg_id = esp_mqtt_client_publish(mqtt_client, topic_full, json_string, 0, 1, 0);
                		ESP_LOGI(TAG, "Telemetry JSON sent (ID:%d): %s", msg_id, json_string);
                        
                    	// Clean up
                    	free(json_string);
                	}
                    
                	cJSON_Delete(root);
                	
                	// Actualización estricta del tiempo para el próximo intervalo
                	xLastWakeTime += xPublishInterval;
				} 
		
			}else{
                ESP_LOGE(TAG2, "Broker conectado pero sesión MQTT no lista.");
            	sync_initialized = false; // Resetear sincronismo si perdemos sesión MQTT
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
        
    }//fin has_ip
}//fin bucle infinito