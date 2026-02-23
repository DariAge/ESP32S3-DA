Proyecto temp_mqtt2: Telemetría Térmica con Respaldo de Red

Este proyecto implementa un sistema de monitoreo de temperatura industrial utilizando un ESP32. La característica principal es su alta disponibilidad de red, utilizando Ethernet (ENC28J60) como conexión primaria y WiFi como respaldo automático (failover), enviando datos en tiempo real a Adafruit IO mediante MQTT.

🚀 Características

Conectividad Dual: Soporte para Ethernet mediante el controlador SPI ENC28J60 y WiFi integrado.

Gestión de Red Inteligente: Cambio automático entre interfaces según disponibilidad de enlace.

Telemetría Ligera: Envío de datos en formato de texto plano hacia el broker de Adafruit IO.

Soporte PSRAM de 8MB: Optimizado para módulos con memoria extendida para manejo de buffers y tareas complejas.

Portal de Configuración (SoftAP): Modo de punto de acceso integrado para configuración remota de credenciales.

🛠️ Configuración y Dependencias

📂 Carpeta Components

Para este proyecto es obligatorio incluir la carpeta components en la raíz del directorio. Esta carpeta debe contener los drivers específicos para el controlador Ethernet ENC28J60, ya que el ESP32 requiere este componente externo para manejar la pila de red sobre SPI.

💾 Gestión de Memoria y Particiones

Dado que el hardware cuenta con una PSRAM de 8MB, el proyecto utiliza una tabla de particiones personalizada para optimizar el almacenamiento del firmware y el sistema de archivos (SPIFFS/NVS).

Archivo partitions.csv: Debe estar en la raíz del proyecto. Este archivo define el esquema de memoria necesario para acomodar el stack de red y las funciones de telemetría sin restricciones de espacio.

Configuración de PSRAM: En idf.py menuconfig, asegúrese de habilitar:

Component config -> ESP32-specific -> Support for external, SPI-connected RAM.

SPI RAM config -> Set RAM parameters -> Make RAM allocatable using malloc().

🌐 Modo SoftAP y Portal de Configuración

Si el dispositivo no detecta una configuración válida o se activa el modo de rescate, el ESP32 iniciará un punto de acceso WiFi propio.

SSID por defecto: TEMP_MQTT_CONFIG (o el configurado en el código).

Interfaz Web: Al conectarse, el usuario debe navegar a la IP 192.168.4.1.

Parámetros Configurables:

WiFi: SSID y Password de la red local.

Adafruit IO: Username y AIO Key.

Feeds: Nombres de los canales para termocupla y PT100.

⚙️ Menuconfig (Configuración HTTP)

Para que el servidor web de configuración sea compatible con las cabeceras de navegadores modernos y formularios de configuración:

HTTP Server:

Max HTTP Request Header Length: 2048.

Max HTTP URI Length: 512.

📂 Estructura del Software

main.c: Punto de entrada y orquestación del sistema.

eth_da.c/h: Driver y configuración de la interfaz Ethernet ENC28J60.

wifi_da.c/h: Manejo de la pila WiFi y escaneo de redes.

network_manager.c/h: Lógica de gestión de prioridades de red (Ethernet > WiFi).

telemetry_sender_task_2.c: Lógica de envío de datos vía MQTT.

telemetry_task.h: Definiciones y prototipos para las tareas de telemetría.

partitions.csv: Definición de la tabla de particiones del sistema.

📡 Protocolo de Comunicación (Adafruit IO)

Los datos se envían a los siguientes feeds por defecto:

Termocupla: DaroA_1/Feeds/termocupla

RTD (PT100): DaroA_1/Feeds/pt100

Nota: Es fundamental que el archivo partitions.csv esté correctamente referenciado en el archivo CMakeLists.txt del proyecto mediante la instrucción set(partition_csv "partitions.csv").