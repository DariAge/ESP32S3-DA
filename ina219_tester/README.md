INA219 Tester (ESP-IDF v5.3)

Este proyecto es una herramienta de prueba diseñada para validar la lectura de corriente y tensión de bus utilizando el sensor INA219 con un ESP32-S3. 
Implementa una arquitectura de Productor-Consumidor utilizando FreeRTOS Queues para separar la lectura del sensor de la impresión en consola.

🚀 Características

Alta Resolución: Configurado para cargas bajas (hasta 400mA) con un shunt de 0.1R.
Filtro de Ruido: Implementa oversampling de 128 muestras por hardware (RES_12BIT_128S).Estabilidad: Arquitectura desacoplada mediante colas de FreeRTOS.
Compatibilidad: Adaptado específicamente para evitar conflictos de drivers en ESP-IDF v5.3.

🔌 Conexiones (ESP32-S3)

| **SDA_INA219** 	   | GPIO 41      | SDA I2C0  |
| **SCL_INA219**       | GPIO 42      | SCL I2C0  |

Nota: el módulo viene con pull-ups externas y el programa por default,
viene con pull-up interna deshabilitadas (i2c_setup_port en i2cdev.c)

🛠️ Configuración del Proyecto
Este tester depende de la librería esp-idf-lib (UncleRus). 
Para que el compilador encuentre los componentes, el archivo CMakeLists.txt en la raíz del proyecto 
debe modificarse INCLUDE_DIRS y PRIV_REQUIRES, como se muestra a continuación:

 See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c         # list the source files of this component
    INCLUDE_DIRS "."       # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
    PRIV_REQUIRES  	ina219
    				i2cdev 
    				esp_idf_lib_helpers
    				esp_adc
    				esp_timer
    				driver     # optional, list the private requirements
)

⚠️ Troubleshooting (IMPORTANTE)
Si estás trabajando con ESP-IDF v5.3 o superior, podrías encontrarte con errores críticos. Aquí te explico cómo resolverlos:

1. Error: CONFLICT! driver_ng is not allowed to be used with this old driver
Este error ocurre porque la versión 5.3 intenta cargar el nuevo driver de I2C mientras la librería utiliza el legacy.

Solución: No inicialices el I2C manualmente con i2c_driver_install(). Deja que la librería gestione el bus llamando a i2cdev_init() al inicio de la tarea del sensor. Esto permite que la capa de abstracción de la librería decida el método de conexión correcto.

2. Error: assert failed: xQueueSemaphoreTake
Este error suele indicar que el driver de I2C no se ha inicializado correctamente antes de intentar acceder al sensor.

Solución: Asegúrate de que i2cdev_init() sea llamado antes de ina219_init_desc().

3. Lecturas en 0 mA o "I2C Read Error"
Check de Hardware: El INA219 mide corriente en el lado de "Alta" (High Side). El pin VIn+ debe ir a la fuente y VIn- a la carga.

Dirección I2C: Por defecto es 0x40. Si usas un módulo con puentes de soldadura (A0/A1), verifica la dirección con un I2C Scanner.

Notas de Versión
Framework: ESP-IDF v5.3.1 (Master/Dirty)

