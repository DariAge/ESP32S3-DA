/*
 * mcpwm_da.c
 *
 *  Created on: 7 mar. 2026
 *      Author: DaroA
 */
#include "driver/gpio.h"
#include "mcpwm_da2.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"


static mcpwm_cmpr_handle_t comparator = NULL;

void init_mcpwm_bts7960(void) {
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_oper_handle_t oper = NULL;
    mcpwm_gen_handle_t gen_r = NULL;
    //mcpwm_gen_handle_t gen_l = NULL;

    mcpwm_timer_config_t timer_conf = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000, // 1MHz
        .period_ticks = 1000,       // 20kHz(50)
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    mcpwm_new_timer(&timer_conf, &timer);

    mcpwm_operator_config_t oper_conf = { .group_id = 0 };
    mcpwm_new_operator(&oper_conf, &oper);
    mcpwm_operator_connect_timer(oper, timer);

    mcpwm_comparator_config_t cmp_conf = { .flags.update_cmp_on_tep = true };
    mcpwm_new_comparator(oper, &cmp_conf, &comparator);

    mcpwm_generator_config_t gen_r_conf = { .gen_gpio_num = BDC_MCPWM_GPIO_RPWM };
    mcpwm_new_generator(oper, &gen_r_conf, &gen_r);

    //mcpwm_generator_config_t gen_l_conf = { .gen_gpio_num = BDC_MCPWM_GPIO_LPWM };
    //mcpwm_new_generator(oper, &gen_l_conf, &gen_l);

    // RPWM (PWM activo)
    mcpwm_generator_set_actions_on_compare_event(gen_r,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW),
        MCPWM_GEN_COMPARE_EVENT_ACTION_END());
    mcpwm_generator_set_actions_on_timer_event(gen_r,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
        MCPWM_GEN_TIMER_EVENT_ACTION_END());

    // LPWM (Siempre LOW para giro en un solo sentido)
    //mcpwm_generator_set_actions_on_timer_event(gen_l,
    //    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW),
    //    MCPWM_GEN_TIMER_EVENT_ACTION_END());
	
	
    mcpwm_timer_enable(timer);
    mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP);
    mcpwm_comparator_set_compare_value(comparator, 0);
}
/*
void bts7960_enable(bool enable) {
    gpio_set_level(BTS7960_RL_EN, enable ? 1 : 0);
}
*/
void mcpwm_set_speed(float rpm) {
    if (rpm > MAX_MOTOR_RPM) rpm = MAX_MOTOR_RPM;
    if (rpm < 0) rpm = 0;
    
    // Mapeo 0-MAX_RPM a 0-50 ticks
    uint32_t duty = (uint32_t)((rpm / (float)MAX_MOTOR_RPM) * 1000.0f);
    mcpwm_comparator_set_compare_value(comparator, duty);
}



