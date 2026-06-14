#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define NUM_SERVOS   8
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500
#define PWM_FREQ_HZ  50
#define PWM_RES      LEDC_TIMER_16_BIT
#define PWM_FULL     65536              // 16bit 周期分解能
#define UART_PORT    UART_NUM_0
#define LINE_MAX     128

// servo 0..3 = 左アーム, 4..7 = 右アーム
// 入力専用(34-39)/ストラップピン(0,2,12,15)は避ける
static const int servo_gpio[NUM_SERVOS] = {16, 17, 18, 19, 27, 26, 25, 33};

static uint32_t angle_to_duty(float deg){
    if(deg < 0) deg = 0;
    if(deg > 180) deg = 180;
    uint32_t us = SERVO_MIN_US + (uint32_t)((SERVO_MAX_US - SERVO_MIN_US) * deg / 180.0f);
    return (uint32_t)((uint64_t)us * PWM_FULL / 20000);   // 20ms 周期中のパルス幅
}

static void servo_set(int id, float deg){
    if(id < 0 || id >= NUM_SERVOS) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, id, angle_to_duty(deg));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, id);
}

static void servo_init(void){
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num  = LEDC_TIMER_0,
        .duty_resolution = PWM_RES,
        .freq_hz    = PWM_FREQ_HZ,        // 50Hz
        .clk_cfg    = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    for(int i = 0; i < NUM_SERVOS; i++){
        ledc_channel_config_t c = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = i,               // ch0..ch7 を1個ずつ
            .timer_sel  = LEDC_TIMER_0,    // 全部同じ50Hzタイマを共有
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = servo_gpio[i],
            .duty       = angle_to_duty(90),
            .hpoint     = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }
}

static void handle_line(char *line){
    if(line[0] == 'S' || line[0] == 's'){
        // 一括: "S 90 80 100 95 90 85 100 90"（8個）
        char *p = line + 1, *end;
        for(int i = 0; i < NUM_SERVOS; i++){
            float v = strtof(p, &end);
            if(end == p) break;            // 足りない分は現状維持
            servo_set(i, v);
            p = end;
        }
        printf("OK S\n");
    } else {
        // 単体: "<id> <angle>"  例: "3 120"
        int id; float deg;
        if(sscanf(line, "%d %f", &id, &deg) == 2){
            servo_set(id, deg);
            printf("OK %d %.1f\n", id, deg);
        } else {
            printf("ERR [%s]\n", line);
        }
    }
}

static void uart_init(void){
    uart_config_t c = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &c);
}

void app_main(void){
    esp_log_level_set("*", ESP_LOG_WARN);
    servo_init();
    uart_init();

    char line[LINE_MAX];
    int pos = 0;
    uint8_t ch;
    while(1){
        if(uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(20)) <= 0) continue;
        if(ch == '\n' || ch == '\r'){
            if(pos > 0){ line[pos] = '\0'; handle_line(line); }
            pos = 0;
        }else if(pos < LINE_MAX - 1){
            line[pos++] = (char)ch;
        }else{
            pos = 0;  // 長すぎる行は破棄
        }
    }
}