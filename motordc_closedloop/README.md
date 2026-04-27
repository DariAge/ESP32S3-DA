Closed-Loop Motor Control (PI) (motordc_closedloop)

Este proyecto implementa un sistema de control de velocidad de lazo cerrado
para motores DC utilizando un controlador PI (Proporcional-Integral). 
Está diseñado para aplicaciones que requieren una velocidad constante 
bajo carga, como agitadores magnéticos de laboratorio o sistemas de dosificación.

🚀 Funcionalidades Clave
* Control PI Ajustado: Estabilidad lograda con Kp=0.2 y Ki=0.25.
* Rampa de Aceleración Suave: Evita picos de corriente y picos de corriente iniciales (Soft Start).
* Control de Zona Muerta (Deadband): Implementación de un umbral de 15 RPM (ajustable) para eliminar el zumbido electrónico 
y el jitter del MOSFET en el punto de equilibrio.
* Lectura por Encoder: Procesamiento de pulsos en tiempo real para el cálculo de RPM.Arquitectura Multitarea: Separación de tareas de control (PID), sensado (Encoder) y monitoreo (Log).
* Control mediante dispositivo amo: Realiza lectura de comandos de seteo (start/stop - setpoint - slope)
mediante UART.

📊 Especificaciones del Sistema
* Setpoint (SP): Hasta 2500 RPM (ajustable).
* Precisión: Error en estado estable menor al 0.5% (+/- 10 RPM).
* Frecuencia PWM: 20kHz (fuera del rango audible).
* Salida de Control: PWM de 10 bits de resolución. 

🔧 Requisitos de Hardware

MCU: ESP32-S3.

Driver: MOSFET de potencia (ej. IRLZ44N) con optoacoplador de alta velocidad (6N137).

Alimentación: 24V DC para etapa de potencia. 5v para Driver. 3v3 para encoder y sistema amo.

Feedback: Sensor óptico o Hall (Encoder) en el eje del motor.

📂 Estructura del Código

main.c: Tarea principal del PID y gestión de PWM. Recepción de comandos
y envío de telemetría. 

encoder_da.c: Inicialización de pcnt y lógica de conteo de pulsos.

mcpwm_da.c: Inicialización de mcpwm y función de variación.

📂 División de tareas en núcleos

Core 0(Comunicaciones):

uart_task(10): parser de trama recibida del dispositivo amo. 
input_task(9): máquina de estados del sistema.
monitor_task(2): muestra sesgada a variación de velocidad en consola y envío de telemetría UART cada 1seg.

Core 1 (Control & Hard Real-Time):

ramp_task(8): Recibe argumento de tarea con datos de control de input_task. 
Genera la rampa de aceleración y desaceleración del motor. Ejecuta control PI.
Envía paquete completo para telemetría. 

 🔧 Configuración de Hardware (Pinout Utilizado)


| **UART RX**   | GPIO 18     | Receiver del módulo UART2       |
| **UART TX**   | GPIO 17     | Transceiver del módulo UART2    |

| **DRV_MOT_EN**| GPIO 39     | Salida Enable de driver motor   |
| **BDC_MCPWM** | GPIO 40     | Salida MCPWM para driver motor  |

| **ENCODER_A** | GPIO 21     | Entrada PCNT para encoder       |

Próximos Pasos
[ ] Integración de monitoreo de corriente en tiempo real (INA219).

[ ] Implementación de perfiles de aceleración programables.

