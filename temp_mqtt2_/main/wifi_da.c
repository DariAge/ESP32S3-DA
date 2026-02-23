/*
 * wifi_da.c

 * Implementa la lógica para provisionar (SoftAP)
 * a una red Wi-Fi local, y la conexión persistente a una lista de redes preferidas
 * mediante una TAREA ASÍNCRONA de FreeRTOS.
 
 * MODIFICACIÓN: Se añade la lógica de FAILOVER: Si falla al conectar a todas las redes preferidas,
 * cambia automáticamente a modo SoftAP para permitir la re-configuración.
 *
 * MODIFICACIÓN: cambio de funciones save_get para trabajar con "blob" y no con
 * JSON.
 *
 * Created on: 20 oct. 2025
 * Last modified on: 6 feb. 2026
 * Author: DaroA
 */


#include "wifi_da.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "string.h"
#include "stdlib.h"
#include "cJSON.h"
#include "network_manager.h"

// --- Definición de la macro MIN para garantizar portabilidad ---
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

// --- Definiciones de Módulos ---
static const char *TAG = "WIFI_MANAGER";

// --- Configuración de Provisionamiento SoftAP ---
#define SOFT_AP_SSID            "ESP32_S3_PROVISION"
#define SOFT_AP_PASS            "12345678"
#define SOFT_AP_CHANNEL         1
#define MAX_STA_CONN            1

// NVS Keys
#define NVS_NAMESPACE           "wifi_data"
#define NVS_KEY_PREF_LIST       "pref_list"
#define NVS_KEY_CONFIG "full_config"			//agregado nueva version


// --- Configuración de Conexión Iterativa ---
#define MAX_CONNECTION_ATTEMPTS_PER_CREDENTIAL 3
#define CONNECTION_TIMEOUT_MS                  10000 // 10 segundos por intento de conexión (interno de la tarea)
#define TOTAL_CONNECTION_TIMEOUT_MS            35000 // 35 segundos (El tiempo total que app_main debe esperar)

// --- Tamaño del buffer HTML (4096 bytes, ahora en Heap) ---
#define CONFIG_HTML_BUFFER_SIZE 4096 

// --- Estado Global ---
extern EventGroupHandle_t s_network_event_group;
//static EventGroupHandle_t wifi_event_group;
//const int WIFI_CONNECTED_BIT = BIT0;
//const int WIFI_PROVISIONING_BIT = BIT1; // Indica que estamos en modo AP de configuración
static httpd_handle_t server = NULL;
static esp_err_t start_webserver(void);
wifi_credential_t *s_preferred_list = NULL;
size_t s_preferred_list_count = 0; 

/**
 * @brief Recupera la lista de credenciales desde NVS mediante un BLOB binario.
 * @param list Puntero a puntero donde se alojará la memoria para la lista.
 * @param count Puntero para devolver la cantidad de estructuras recuperadas.
 * @return esp_err_t ESP_OK si tuvo éxito.
 * * NOTA: Esta función utiliza malloc. El llamador DEBE liberar la memoria con free(*list).
 */
esp_err_t wifi_manager_get_preferred_list(wifi_credential_t **list, size_t *count)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    size_t required_size = 0;

    // Inicializar valores de salida por seguridad
    if (list == NULL || count == NULL) return ESP_ERR_INVALID_ARG;
    *list = NULL;
    *count = 0;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // 1. Obtener el tamaño del blob almacenado
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, NULL, &required_size);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }

    if (required_size == 0) {
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }

    // 2. Alocar memoria dinámica para la lista
    *list = (wifi_credential_t *)malloc(required_size);
    if (*list == NULL) {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    // 3. Leer el contenido del blob directamente a la memoria alocada
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, *list, &required_size);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        // Calcular cuántas estructuras entran en el tamaño leído
        *count = required_size / sizeof(wifi_credential_t);
        ESP_LOGI(TAG, "Lista recuperada de NVS. Redes: %d (%d bytes)", (int)*count, (int)required_size);
    } else {
        free(*list);
        *list = NULL;
        ESP_LOGE(TAG, "Error al leer el blob de NVS: %s", esp_err_to_name(ret));
    }

    return ret;
}


/**
 * @brief Guarda una lista de credenciales en NVS como un BLOB binario único.
 * @param list Puntero al array de estructuras a guardar.
 * @param count Cantidad de elementos en el array.
 * @return esp_err_t ESP_OK si tuvo éxito.
 */
esp_err_t wifi_manager_save_preferred_list(const wifi_credential_t *list, size_t count)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    if (list == NULL || count == 0) return ESP_ERR_INVALID_ARG;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al abrir NVS para escritura: %s", esp_err_to_name(ret));
        return ret;
    }

    // Calcular el tamaño total del blob binario
    size_t size = count * sizeof(wifi_credential_t);

    // Guardar el bloque binario completo
    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, list, size);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Lista de %d credenciales guardada exitosamente en NVS.", (int)count);
    } else {
        ESP_LOGE(TAG, "Error al escribir el blob en NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

// --- FUNCIÓN DE CAMBIO DE MODO ---

/**
 * @brief Detiene el modo Wi-Fi actual y cambia a modo SoftAP para provisionamiento.
 */
void wifi_manager_switch_to_softap(void)
{
    ESP_LOGW(TAG, "!! CAMBIANDO A MODO SOFTAP (PROVISIONAMIENTO) POR FALLO DE CONEXIÓN !!");
    
    // 1. Detener Wi-Fi (esto detiene el modo STA)
    esp_wifi_stop();

    // 2. Iniciar la configuración de SoftAP y el servidor web
    wifi_init_softap();
}


//NO ESTA SIENDO EJECUTADA PERO NO TIRA ERROR
// --- Tarea de Conexión Asíncrona (Lógica Iterativa) ---

/**
 * @brief Tarea de FreeRTOS que intenta conectar iterativamente a las redes preferidas.
 */
static void preferred_list_connect_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Tarea de Conexión iniciada. Intentando conectar a la lista...");

    for (size_t i = 0; i < s_preferred_list_count; i++) {

        if (xEventGroupGetBits(s_network_event_group) & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Conexión activa, terminando la tarea de reintento.");
            goto end_task;
        }

        const wifi_credential_t *cred = &s_preferred_list[i];

        for (int attempt = 0; attempt < MAX_CONNECTION_ATTEMPTS_PER_CREDENTIAL; attempt++) {

            wifi_config_t wifi_config = { .sta = { .bssid_set = false }, };

            strncpy((char *)wifi_config.sta.ssid, cred->ssid, sizeof(wifi_config.sta.ssid) - 1);
            strncpy((char *)wifi_config.sta.password, cred->password, sizeof(wifi_config.sta.password) - 1);

            // CLAVE: Determinar el modo de autenticación según la credencial guardada
            if (cred->is_open || strlen(cred->password) == 0) {
                 wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
                 ESP_LOGI(TAG, "Autenticación: ABIERTA (WIFI_AUTH_OPEN)");
            } else {
                 wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
                 ESP_LOGI(TAG, "Autenticación: PSK (WIFI_AUTH_WPA_WPA2_PSK)");
            }

            ESP_LOGI(TAG, "Intento %d/%d (Red %d/%d): Conectando a SSID: %s",
                     attempt + 1,
                     MAX_CONNECTION_ATTEMPTS_PER_CREDENTIAL,
                     (int)i + 1,
                     (int)s_preferred_list_count,
                     cred->ssid);

            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_connect());

            EventBits_t bits = xEventGroupWaitBits(s_network_event_group,
                                                   WIFI_CONNECTED_BIT,
                                                   pdFALSE,
                                                   pdTRUE,
                                                   pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS)); 

            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "¡Conexión exitosa a %s!", cred->ssid);
                goto end_task; // Salir de todos los bucles
            } else {
                ESP_LOGW(TAG, "Fallo al conectar a %s. Intentando el siguiente intento...", cred->ssid);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(500)); // Pequeña pausa
            }
        } // Fin de intentos por credencial
    } // Fin de iteración de credenciales

    // Si llegamos a esta línea, significa que falló en TODAS las redes y en TODOS los intentos.
    ESP_LOGE(TAG, "Conexión Fallida: No se pudo conectar a ninguna red preferida.");
    
    // LLAMAMOS A LA FUNCIÓN DE CAMBIO DE MODO
    wifi_manager_switch_to_softap(); 
    
end_task:
    vTaskDelete(NULL);
}


// --- Servidor Web SoftAP (Provisionamiento) ---

/**
 * @brief Handler para la solicitud HTTP de configuración de credenciales (POST).
 */
static esp_err_t config_post_handler(httpd_req_t *req)
{
     // Aumentamos un poco el buffer para asegurarnos que entren los 4 campos + etiquetas
    char content[256]; 
    size_t recv_len = MIN(req->content_len, sizeof(content));
    int ret = httpd_req_recv(req, content, recv_len);

    if (ret <= 0) { if (ret == HTTPD_SOCK_ERR_TIMEOUT) { httpd_resp_send_408(req); } return ESP_FAIL; }
    content[recv_len] = '\0';

    wifi_credential_t new_cred = {0};
    char ssid_temp[32] = {0};
    char pass_temp[64] = {0};
    char user_temp[64] = {0}; //AGREGADOS PARA ADAFRUIT
    char key_temp[64] = {0};  //AGREGADOS PARA ADAFRUIT
    
    //PREGUNTO POR DATOS DE ADAFRUIT
    bool has_user = (httpd_query_key_value(content, "aio_user", user_temp, sizeof(user_temp)) == ESP_OK);
    bool has_key  = (httpd_query_key_value(content, "aio_key", key_temp, sizeof(key_temp)) == ESP_OK);

    if (httpd_query_key_value(content, "ssid", ssid_temp, sizeof(ssid_temp)) == ESP_OK &&
        httpd_query_key_value(content, "pass", pass_temp, sizeof(pass_temp)) == ESP_OK) {

        strncpy(new_cred.ssid, ssid_temp, sizeof(new_cred.ssid) - 1);
        strncpy(new_cred.password, pass_temp, sizeof(new_cred.password) - 1);
        
        // CORRECCIÓN V6: Determinar explícitamente si es red abierta.
        // Si la longitud de la contraseña es 0, asumimos red abierta.
        new_cred.is_open = (strlen(pass_temp) == 0);

        ESP_LOGI(TAG, "Credenciales recibidas. SSID: %s. Abierta: %s. Guardando.", 
                 new_cred.ssid, 
                 new_cred.is_open ? "Sí" : "No");
		
		// Copiamos datos de Adafruit IO si existen
        if (has_user) strncpy(new_cred.aio_user, user_temp, sizeof(new_cred.aio_user) - 1);
        if (has_key)  strncpy(new_cred.aio_key, key_temp, sizeof(new_cred.aio_key) - 1);

        // Logs específicos para Adafruit IO
        if (has_user && has_key) {
            ESP_LOGI(TAG, "Config recibida. AIO User: %s", new_cred.aio_user);
        } else {
            ESP_LOGW(TAG, "Atención: Faltan campos de Adafruit IO en el formulario");
        }
       
        // Guardamos SOLO 1 credencial (sobrescribe la lista, como se desea ahora).
        wifi_manager_save_preferred_list(&new_cred, 1);

        const char* resp = "<h1>Guardado OK!</h1><p>El dispositivo se reiniciará para conectar a su red.</p>";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart(); // Reinicio forzado después de guardar
    } else {
        httpd_resp_send_404(req);
    }
    return ESP_OK;
}

/**
 * @brief Handler para la página de inicio (formulario HTML GET).
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html_form =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Configuración Dispositivo</title>"
    "<style>body { font-family: sans-serif; background: #121212; color: #eee; padding: 20px; }"
    ".card { max-width: 400px; margin: auto; background: #1e1e1e; padding: 20px; border-radius: 12px; }"
    "input { width: 100%; padding: 10px; margin: 10px 0; border-radius: 6px; border: 1px solid #333; background: #222; color: #fff; }"
    "button { width: 100%; padding: 12px; background: #9c27b0; color: #fff; border: none; border-radius: 6px; font-weight: bold; cursor: pointer; }"
    "h2 { color: #9c27b0; }</style></head><body>"
    "<div class='card'>"
    "<h2>Configuración de Red</h2>"
    "<form method='POST' action='/config'>"
    "  <label>WiFi SSID</label><input type='text' name='ssid' required>"
    "  <label>WiFi Password</label><input type='password' name='pass'>"
    "  <hr style='border: 0.5px solid #333; margin: 20px 0;'>"
    "  <h2>Adafruit IO</h2>"
    "  <label>AIO Username</label><input type='text' name='aio_user' required>"
    "  <label>AIO Key</label><input type='password' name='aio_key' required>"
    "  <button type='submit'>Guardar y Reiniciar</button>"
    "</form></div></body></html>";

    // Asignación dinámica en el HEAP para evitar Stack Overflow
    char *dynamic_html = (char *)malloc(CONFIG_HTML_BUFFER_SIZE);
    
    if (dynamic_html == NULL) {
        ESP_LOGE(TAG, "Fallo al asignar buffer HTML en Heap.");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int len = snprintf(dynamic_html, CONFIG_HTML_BUFFER_SIZE, html_form, SOFT_AP_SSID);

    if (len < 0 || (size_t)len >= CONFIG_HTML_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Buffer HTML demasiado pequeño. Necesitamos más de %d bytes.", CONFIG_HTML_BUFFER_SIZE);
        free(dynamic_html); // Liberar en caso de fallo
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_send(req, dynamic_html, HTTPD_RESP_USE_STRLEN);
    
    free(dynamic_html); // CRÍTICO: Liberar la memoria asignada en el Heap al terminar
    
    return ESP_OK;
}

/**
 * @brief Inicia el servidor HTTP para el SoftAP Provisioning.
 * USA LAS FUNCIONES config_post_handler y root_get_handler
 */
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Aumentar el tamaño del stack aquí para el servidor HTTP (si se necesita)
    config.stack_size = 4096; 
    config.core_id = 1; // Asignar a Core 1 para mejor rendimiento (opcional)

    // Registrar URIs
    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
    httpd_uri_t config_uri = { .uri = "/config", .method = HTTP_POST, .handler = config_post_handler, .user_ctx = NULL };

    ESP_LOGI(TAG, "Iniciando servidor en puerto: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &config_uri);
        ESP_LOGI(TAG, "Servidor HTTP iniciado. Conéctese a '%s' y navegue a 192.168.4.1", SOFT_AP_SSID);
        xEventGroupSetBits(s_network_event_group, WIFI_PROVISIONING_BIT);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Error iniciando servidor HTTP!");
    return ESP_FAIL;
}

/**
 * @brief Configura e inicia el ESP32 en modo Access Point (AP).
 */
void wifi_init_softap(void)
{
    ESP_LOGI(TAG, "Configurando Wi-Fi en modo SoftAP");

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SOFT_AP_SSID,
            .ssid_len = strlen(SOFT_AP_SSID),
            .channel = SOFT_AP_CHANNEL,
            .password = SOFT_AP_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
	
	//ACÁ ES DONDE SE PIDEN LAS CREDENCIALES LLAMANDO A config_post_handler y root_get_handler
    start_webserver();
}

/**
 * @brief Configura e inicia el ESP32 en modo Station (STA) y LANZA TAREA DE CONEXIÓN
 * OJO AL SINCRONIZAR.
 */
void wifi_init_station(void)
{
    ESP_LOGI(TAG, "Configurando Wi-Fi en modo Estación (STA). Lanzando tarea de conexión.");

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 1. Lanzar la tarea de conexión asíncrona (preferred_list_connect_task)
    xTaskCreate(preferred_list_connect_task,
                "wifi_conn_task",
                4096, // Stack suficiente
                NULL,
                5,    // Prioridad Media
                NULL);
}
