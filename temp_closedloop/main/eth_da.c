/*
 * eth_da.c
 *
 *  Created on: 9 feb. 2026
 *      Author: DaroA
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_eth.h"
#include "esp_eth_enc28j60.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_event.h"

static const char *TAG = "ETH_SPI_DRV";

// Definición de Pines para ESP32-S3 (Asegúrate que coincidan con tu hardware)
#define ETH_SPI_MISO_GPIO 13 //GPIO13
#define ETH_SPI_MOSI_GPIO 11 //GPIO11
#define ETH_SPI_SCLK_GPIO 12 //GPIO12
#define ETH_SPI_CS_GPIO   10 //GPIO10
#define ETH_SPI_INT_GPIO  46  // Pin de Interrupción del ENC28J60
#define ETH_SPI_RST_GPIO  9   // ANTES ERA GPIO9 Pin de Reset (si lo tienes conectado)

#define ETH_SPI_CLOCK_MHZ    4  // Velocidad segura para ENC28J60

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;

/**
 * @brief Función para realizar un Hard Reset manual al ENC28J60
 * Según el datasheet, el chip necesita un tiempo después del reset
 * antes de poder comunicarse por SPI.
 */
void hard_reset_enc28j60() {
    ESP_LOGI(TAG, "Iniciando Hard Reset manual en GPIO %d...", ETH_SPI_RST_GPIO);
    
    // 1. HARD RESET MANUAL ANTES DE EMPEZAR
    // Esto asegura que el registro de revision sea lo primero que se lea bien
    gpio_reset_pin(ETH_SPI_RST_GPIO);
    gpio_set_direction(ETH_SPI_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ETH_SPI_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(ETH_SPI_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}


/**
 * @brief Inicializa el driver Ethernet sobre el bus SPI
 */
esp_err_t eth_driver_init(void)
{
      // A. INSTALAR SERVICIO ISR (Crucial para que no explote)
    // El argumento 0 indica que no pasamos banderas especiales
    esp_err_t ret2 = gpio_install_isr_service(0);
	if (ret2 == ESP_ERR_INVALID_STATE) {
    	ESP_LOGW("ETH", "El servicio ISR ya estaba instalado, ignorando...");
	}
    hard_reset_enc28j60();
    
    // 1. Inicializar el stack TCP/IP: en netkork.manager.c previamente
    //ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 2. Crear instancia Netif para Ethernet
    
    //esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    
    // Modificá la prioridad ANTES de crear el netif!!!
	// Creamos una copia local de la configuración inherente (de esp_netif_defaults.h)
	// Usamos la variable global de referencia que provee el SDK
	// Obtenemos la referencia global del SDK
    extern const esp_netif_inherent_config_t _g_esp_netif_inherent_eth_config;
	esp_netif_inherent_config_t my_base_cfg = _g_esp_netif_inherent_eth_config;
	// 2. Modificamos el valor que queremos
	my_base_cfg.route_prio = 150; 
    // 3. Creamos la configuración de red manualmente
	// IMPORTANTE: Aquí inicializamos todo de una vez
	esp_netif_config_t cfg = {
    	.base = &my_base_cfg,
    	.driver = NULL,
    	.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
	};
    //esp_netif_t *eth_netif = esp_netif_new(&cfg);
    s_eth_netif = esp_netif_new(&cfg);
    if (s_eth_netif == NULL) {
        return ESP_FAIL;
    }
    // 3. Configuración del Bus SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = ETH_SPI_MISO_GPIO,
        .mosi_io_num = ETH_SPI_MOSI_GPIO,
        .sclk_io_num = ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
	if (ret2 == ESP_ERR_INVALID_STATE) {
    	ESP_LOGW("ETH", "Bus SPI ya inicializado anteriormente.");
	} else {
    	ESP_ERROR_CHECK(ret2);
	}
    // 4. Configuración del dispositivo ENC28J60 en el bus SPI
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = ETH_SPI_CS_GPIO,
        .queue_size = 20,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    // 5. Configuración del Driver de Ethernet (MAC y PHY)
    eth_enc28j60_config_t enc28j60_config = ETH_ENC28J60_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
    enc28j60_config.int_gpio_num = ETH_SPI_INT_GPIO;
	
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.sw_reset_timeout_ms = 500;
    esp_eth_mac_t *mac = esp_eth_mac_new_enc28j60(&enc28j60_config, &mac_config);
	
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = ETH_SPI_RST_GPIO;
    phy_config.reset_gpio_num = -1; 
    esp_eth_phy_t *phy = esp_eth_phy_new_enc28j60(&phy_config);

    // 4. Instalación del Driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
   // Si falla el install, no reiniciamos el ESP32, así podemos ver el log
    esp_err_t ret = esp_eth_driver_install(&config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "El driver de IDF sigue fallando a pesar de detectar la rev 6.");
    }

    // 5. Lógica de asignaciòn de MAC
    uint8_t base_mac_addr[6] = {0};
    // Leemos la MAC de fábrica del ESP32-S3 asignada a Ethernet
    ESP_ERROR_CHECK(esp_read_mac(base_mac_addr, ESP_MAC_ETH));
    // Se la pasamos a la instancia MAC del ENC28J60
    ESP_ERROR_CHECK(mac->set_addr(mac, base_mac_addr));
   	
   	// 6. Registrar manejadores de eventos: en network_manager.c previamente
   	
    // 7. Adjuntar el stack de red al driver
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));
	
    ESP_LOGI(TAG, "Arrancando Ethernet...");
    //esp_eth_start() despues en network.manager.c
    return ESP_OK;
}

/**
 * @brief Función para obtener el handle (por si necesitas control manual)
 */
esp_eth_handle_t eth_get_handle(void)
{
    return s_eth_handle;
}

/**
 * @brief Devuelve el handle de red (Netif)
 */
esp_netif_t* eth_get_netif(void)
{
    return s_eth_netif;
}
