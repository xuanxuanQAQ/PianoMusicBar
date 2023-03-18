#include <stdio.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"

#include "led_strip_types.h"
#include "led_strip_rmt.h"
#include "led_strip.h"

#define LED_GPIO_NUM 13         // LED灯条输出GPIO口
#define LED_NUM 190             // LED灯条LED灯数
#define RMT_RESOLUTION 10000000 // RMT分辨率
// #define UART_NUM UART_NUM_1
// #define UART_TX_PIN 17
// #define UART_RX_PIN 16
#define UART_NUM UART_NUM_0
#define UART_TX_PIN 35
#define UART_RX_PIN 34
#define UART_BAUDRATE 115200
#define UART_BUF_SIZE 1024

void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS_INFO", "NVS初始化完成喵~");
    }
    else
    {
        ESP_ERROR_CHECK(ret);
    }
}

/**
 * @brief HSV模型
 * @param[in] h-输入想要的颜色的色相（hue），范围[0，360]
 * @param[in] s-输入想要的颜色的饱和度（saturation），范围[0，100]
 * @param[in] v-输入想要的颜色的明度（value），范围[0，100]
 * @param[out] r,g,b-输出想要的颜色的rgb值
 *
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360;
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i)
    {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

void uart_port_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));

    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/**
 * @brief LED灯条初始化函数
 * @param[out] 输出所生成的LED灯条对象的句柄
 *
 */
led_strip_handle_t LED_strip_init(void)
{
    led_strip_handle_t led_handle = NULL;
    led_strip_config_t led_config = {
        .strip_gpio_num = LED_GPIO_NUM,
        .max_leds = LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t led_rmt_config = {
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = RMT_RESOLUTION,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_config, &led_rmt_config, &led_handle));
    return led_handle;
}

/**
 * @brief 灯光颜色初始化函数
 *
 */
void LED_color_init(uint32_t *h, uint32_t *s, uint32_t *v, int begin, int end)
{
    for (int i = begin; i < end; i++)
    {
        h[i] = 30;
        s[i] = 89;
        v[i] = 10;
    }
}

/**
 * @brief uart串口监视线程
 *
 * @param[in] h,s,v
 */
void uart_monitor(void *led_handle_ptr)
{
    uint32_t red[LED_NUM] = {0};
    uint32_t green[LED_NUM] = {0};
    uint32_t blue[LED_NUM] = {0};
    uint32_t hue[LED_NUM] = {0};
    uint32_t saturation[LED_NUM] = {0};
    uint32_t value[LED_NUM] = {0};

    led_strip_handle_t led_handle = *(led_strip_handle_t *)led_handle_ptr;

    LED_color_init(hue, saturation, value, 0, LED_NUM);
    ESP_LOGI("LED_INFO", "ESP灯条颜色初始化完成喵~");

    for (int i = 0; i < LED_NUM; i++)
    {
        led_strip_hsv2rgb(hue[i], saturation[i], value[i], &red[i], &green[i], &blue[i]);
        led_strip_set_pixel(led_handle, i, red[i], green[i], blue[i]);
    }
    led_strip_refresh(led_handle);

    while (1)
    {
        uint8_t data[UART_BUF_SIZE];
        int data_len;
        int led_position;
        int velocity;

        data_len = uart_read_bytes(UART_NUM, data, UART_BUF_SIZE, pdMS_TO_TICKS(20));

        if (data_len > 0)
        {
            for (int i = 0; i < data_len; i += 3)
            {
                if (data[i] == 1)
                {
                    led_position = (108 - data[i + 1]) * 2 + 8;
                    velocity = (data[i + 2] - 40) / 2 + 10;
                    for (int j = led_position - 1; j < led_position + 2; j++)
                    {
                        hue[j] = 240;
                        saturation[j] = 80;
                        value[j] = velocity;
                        led_strip_hsv2rgb(hue[j], saturation[j], value[j], &red[j], &green[j], &blue[j]);
                        led_strip_set_pixel(led_handle, j, red[j], green[j], blue[j]);
                    }
                    led_strip_refresh(led_handle);
                }
                else if (data[i] == 0)
                {
                    led_position = (108 - data[i + 1]) * 2 + 8;
                    for (int j = led_position - 1; j < led_position + 2; j++)
                    {
                        hue[j] = 30;
                        saturation[j] = 89;
                        value[j] = 10;
                        led_strip_hsv2rgb(hue[j], saturation[j], value[j], &red[j], &green[j], &blue[j]);
                        led_strip_set_pixel(led_handle, j, red[j], green[j], blue[j]);
                    }
                    led_strip_refresh(led_handle);
                }
            }
        }
    }
}

void app_main(void)
{
    // 初始化NVS
    nvs_init();

    // LED初始化
    led_strip_handle_t led_handle = LED_strip_init();
    ESP_LOGI("LED_INFO", "灯条驱动安装成功喵~");

    // 初始化UART串口
    uart_port_init();
    ESP_LOGI("UART_INFO", "UART初始化成功了喵~");

    // 创建串口监视线程
    TaskHandle_t uart_monitor_handle = NULL;
    xTaskCreate(uart_monitor, "LED_strip_run", 1024 * 8, (void *)&led_handle, 2, &uart_monitor_handle);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
