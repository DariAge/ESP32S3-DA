/*
 * mcpwm_da2.c
 *  Se adicionan definiciones Capture mode para ZCD, a las anteriores 
 *  de PWM Output Edge Aligned.  
 *  Created on: 14 ene. 2026
 *      Author: DaroA
 */


#include "mcpwm_da2.h"
#include "thermal_control.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"
#include "inttypes.h"

static mcpwm_cmpr_handle_t comparator = NULL;
static uint32_t duty_ciclos_limit = 0; // Cuántos semiciclos debe estar ON
static mcpwm_cmpr_handle_t motor_comparator = NULL;

static const char *TAG = "MCPWM_DRIVER";

extern thermal_system_t ts_ctx;
// Inicializacion de módulo para todas sus funciones
void init_mcpwm(void){
	ESP_LOGI(TAG, "Iniciando Motor (PWM) y SSR (Control por Ciclos)");
/*	
	mcpwm_gen_handle_t gen_r = NULL;
	mcpwm_gen_handle_t gen_l = NULL;
    // 1. MOTOR DC - PWM 20kHz (Igual que antes)
    mcpwm_timer_handle_t motor_timer = NULL;
    mcpwm_timer_config_t m_timer_conf = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, 
        .period_ticks = 50, // 20kHz
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP, //edge aligned
    };
    mcpwm_new_timer(&m_timer_conf, &motor_timer);

    mcpwm_oper_handle_t motor_oper = NULL;
    mcpwm_operator_config_t m_oper_conf = { .group_id = 0 };
    mcpwm_new_operator(&m_oper_conf, &motor_oper);
    mcpwm_operator_connect_timer(motor_oper, motor_timer);

    mcpwm_comparator_config_t m_cmp_conf = { .flags.update_cmp_on_tep = true };
    mcpwm_new_comparator(motor_oper, &m_cmp_conf, &motor_comparator);

   
    mcpwm_generator_config_t gen_r_conf = { .gen_gpio_num = BDC_MCPWM_GPIO_RPWM };
    mcpwm_new_generator(motor_oper, &gen_r_conf, &gen_r);

    mcpwm_generator_set_actions_on_timer_event(gen_r,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
        MCPWM_GEN_TIMER_EVENT_ACTION_END());
    mcpwm_generator_set_actions_on_compare_event(gen_r,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, motor_comparator, MCPWM_GEN_ACTION_LOW),
        MCPWM_GEN_COMPARE_EVENT_ACTION_END());
	
	// Configurar LPWM siempre LOW
    mcpwm_generator_set_actions_on_timer_event(gen_l,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW),
        MCPWM_GEN_TIMER_EVENT_ACTION_END());

    mcpwm_timer_enable(motor_timer);
    mcpwm_timer_start_stop(motor_timer, MCPWM_TIMER_START_NO_STOP);
*/    
    //mcpwm_comparator_set_compare_value(comparator, 0);

    // 2. CAPTURE - Sincronismo de Red
    mcpwm_cap_timer_handle_t cap_timer = NULL;
    mcpwm_capture_timer_config_t cap_timer_conf = { .group_id = 0,
    												.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT, };
    mcpwm_new_capture_timer(&cap_timer_conf, &cap_timer);

    mcpwm_cap_channel_handle_t cap_chan = NULL;
    mcpwm_capture_channel_config_t cap_chan_conf = {
        .gpio_num = ZERO_CROSS_GPIO,
        .prescale = 1,
        .flags.pos_edge = true, // Detecta cada ciclo (o semiciclo según circuito)
    	.flags.pull_up = true, // ZCD suele ser open collector
    };
 /*   
    gpio_config_t io_conf = {
    	.intr_type = GPIO_INTR_DISABLE,
    	.mode = GPIO_MODE_INPUT,
    	.pin_bit_mask = (1ULL << ZERO_CROSS_GPIO),
    	.pull_up_en = 1, //importante ya que ZCD es open collector
	};
	gpio_config(&io_conf);
*/	
    mcpwm_new_capture_channel(cap_timer, &cap_chan_conf, &cap_chan);

    mcpwm_capture_event_callbacks_t cbs = { .on_cap = on_zcd_event };
    mcpwm_capture_channel_register_event_callbacks(cap_chan, &cbs, NULL);
    
    mcpwm_capture_channel_enable(cap_chan);
    mcpwm_capture_timer_enable(cap_timer);
    mcpwm_capture_timer_start(cap_timer);
    ESP_LOGI(TAG, "ZCD Inicializado correctamente");
}

// Control Motor
void mcpwm_set_speed(float rpm) {
    if (rpm > MAX_MOTOR_RPM) rpm = MAX_MOTOR_RPM;
    if (rpm < 0) rpm = 0;
    
    // Mapeo 0-1000 RPM a 0-50 ticks (20kHz)
    uint32_t duty = (uint32_t)((rpm / MAX_MOTOR_RPM) * 50.0f);
    mcpwm_comparator_set_compare_value(comparator, duty);
}


// Control Temperatura (SSR) - Recibe potencia 0.0 a 100.0
void mcpwm_set_ssr_power(float percentage) {
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;

    // Convertimos el porcentaje a cantidad de semiciclos activos dentro de la ventana
    //duty_ciclos_limit = (uint32_t)((percentage / 100.0f) * VENTANA_CONTROL_CICLOS);
    ts_ctx.pulse_target = (uint32_t)((percentage / 100.0f) * BURST_WINDOW);
    
    
    ESP_LOGI(TAG, "SSR Power: %.1f%% %u / %u ciclos",(double)percentage, (unsigned int)ts_ctx.pulse_target, (unsigned int)BURST_WINDOW);
}
