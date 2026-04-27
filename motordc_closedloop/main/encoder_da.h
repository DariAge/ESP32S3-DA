/*
 * encoder_da.h
 *
 *  Created on: 14 ene. 2026
 *      Author: DaroA
 */

#ifndef MAIN_ENCODER_DA_H_
#define MAIN_ENCODER_DA_H_

// --- CONFIGURACIÓN ---
#define ENCODER_A_PIN    21 
#define PCNT_HIGH_LIMIT  20000  // Suficiente para 4600 pulsos en 100ms
#define PCNT_LOW_LIMIT   -100   // No esperamos retroceso, pero el driver pide un limite bajo < 0
#define ENCODER_PPR          1024.0f
#define SAMPLE_TIME_SECONDS  0.1f   // Basado en el delay de monitor_task en main.c

void init_encoder_pcnt(void);
float encoder_get_rpm(void);




#endif /* MAIN_ENCODER_DA_H_ */
