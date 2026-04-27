/*
 * mcpwm_da2.h
 *
 *  Created on: 14 ene. 2026
 *      Author: DaroA
 */

#ifndef MAIN_MCPWM_DA2_H_
#define MAIN_MCPWM_DA2_H_

#include <stdint.h>
#include "driver/gpio.h"

#define BDC_MCPWM_GPIO_RPWM   40		 
#define MAX_MOTOR_RPM         2800

void init_mcpwm_bts7960(void);
//void bts7960_enable(bool enable);
void mcpwm_set_speed(float rpm);

#endif /* MAIN_MCPWM_DA2_H_ */
