Sistema de Control Térmico con PID

Este proyecto consiste en un sistema embebido de alta precisión diseñado para el control de temperatura y agitación, con conectividad dual (WiFi + Ethernet) y telemetría en tiempo real hacia la nube.

🚀 Características Principales

Controlador: ESP32-S3 (Octal PSRAM 8MB / Flash 16MB).

Sensores de Precisión: Interfaz con MAX31865 para lectura de RTD (PT100) y MAX6675 para lectura de termocupla K.

Control de Potencia: Algoritmo PID para manejar SSR con ZCD, para control de resistencia calefactora.

Conectividad Redundante:

Ethernet: Controlador ENC28J60 vía bus SPI.

WiFi: Gestión con Portal Cautivo para configuración de credenciales sin reprogramar, propio del ESP32-S3.

Telemetría: Publicación de datos en formato JSON vía MQTT (Adafruit IO).

Almacenamiento: Configuración de red y calibración guardada en NVS (Non-Volatile Storage).

Portal de Configuración (SoftAP): Modo de punto de acceso integrado para configuración remota de credenciales.

🛠 Arquitectura de Software

main.c: Punto de entrada y orquestación del sistema.

eth_da.c/h: Driver y configuración de la interfaz Ethernet ENC28J60.

wifi_da.c/h: Manejo de la pila WiFi y escaneo de redes.

network_manager.c/h: Lógica de gestión de prioridades de red (Ethernet > WiFi).

telemetry_sender_task_2.c: Lógica de envío de datos vía MQTT.

telemetry_task.h: Definiciones y prototipos para las tareas de telemetría.

pid_controller.c/h: Lógica y funciones para PID de temperatura. 

thermal_control.c/h: Lógica de accionamiento por PID y envío de datos a telemetry_sender_task_2.c.

thermal_sensors_da.c/h: Drivers de MAX6675 y MAX31865 junto a sus definiciones.

mcpwm_da2.c/h: Drivers para lectura de ZCD por Capture Mode. 

partitions.csv: Definición de la tabla de particiones del sistema.

Core 0 (Networking & Telemetry):

vTaskMqttAdafruit: Gestión de la pila MQTT y serialización JSON para su envío cada 5 seg. Muestra de datos en consola Espressif IDE cada 1 seg.

Network Manager: Supervisión de la capa física (Link up/down) e IP.

Core 1 (Control & Hard Real-Time):

vTaskTemperatureControl: Lectura de sensores y ejecución del lazo PID (cada 100ms). Envío de cola cada 1000ms.


🌐 Modo SoftAP y Portal de Configuración

Si el dispositivo no detecta una configuración válida o se activa el modo de rescate, el ESP32 iniciará un punto de acceso WiFi propio.

SSID por defecto: TEMP_MQTT_CONFIG (o el configurado en el código).

Interfaz Web: Al conectarse, el usuario debe navegar a la IP 192.168.4.1.

Parámetros Configurables:

WiFi: SSID y Password de la red local.

Adafruit IO: Username y AIO Key de usuario.

Feeds: Nombres de canal general a enviar jSON. 

📊 Estructura de Telemetría (JSON)

El sistema envía un paquete consolidado cada 5 segundos para optimizar el ancho de banda y cumplir con las políticas de throttling de los brokers MQTT:

{
  "temp_tc": 26.75,
  "temp_rtd": 25.23,
  "setpoint": 100.0,
  "power": 85
}


🔧 Configuración de Hardware (Pinout Sugerido)

Periférico

Pin ESP32-S3

Descripción

MOSI (SPI)

GPIO 11

Bus compartido (ENC28J60 / MAX31865)

MISO (SPI)

GPIO 13

Bus compartido

SCK (SPI)

GPIO 12

Bus compartido

CS (Ethernet)

GPIO 10

Chip Select ENC28J60

INT (Ethernet)

GPIO 46

Interrupción ENC28J60

RST (Ethernet)

GPIO 9

Reset manual Hardware

CS (MAX31865)  CS (MAX6675)

GPIO 5  GPIO4

Chip Select Sensor RTD  Chip Select Sensor Termocupla K

💻 Monitoreo Local

El dispositivo entrega un log estructurado por el puerto serial para depuración y graficación local:
FORMATO: DATA:<RTD>,<TC>,<SP>,<PWR>

Ejemplo: DATA:25.23,26.75,100.0,100,C,C


🛠️ Configuración y Dependencias

📂 Carpeta Components

Para este proyecto es obligatorio incluir la carpeta components en la raíz del directorio. Esta carpeta debe contener los drivers específicos para el controlador Ethernet ENC28J60, ya que el ESP32 requiere este componente externo para manejar la pila de red sobre SPI.

💾 Gestión de Memoria y Particiones

Dado que el hardware cuenta con una PSRAM de 8MB, el proyecto utiliza una tabla de particiones personalizada para optimizar el almacenamiento del firmware y el sistema de archivos (SPIFFS/NVS).

Archivo partitions.csv: Debe estar en la raíz del proyecto. Este archivo define el esquema de memoria necesario para acomodar el stack de red y las funciones de telemetría sin restricciones de espacio.

⚙️ Menuconfig (Configuración HTTP)

Para que el servidor web de configuración sea compatible con las cabeceras de navegadores modernos y formularios de configuración:

HTTP Server:

Max HTTP Request Header Length: 2048.

Max HTTP URI Length: 512.

Configuración de PSRAM: En idf.py menuconfig, asegúrese de habilitar:

Component config -> ESP32-specific -> Support for external, SPI-connected RAM.

SPI RAM config -> Set RAM parameters -> Make RAM allocatable using malloc().


📦 Instalación y Compilación

Configurar entorno ESP-IDF (v5.3.1).

Clonar repositorio.

Ejecutar idf.py menuconfig para ajustar parámetros de usuario.

Compilar y flashear: idf.py build flash monitor.

Nota sobre precisión: El sistema procesa los datos con punto flotante de 32 bits para el lazo de control, aplicando redondeo a dos decimales exclusivamente en la capa de telemetría para mejorar la legibilidad del usuario.