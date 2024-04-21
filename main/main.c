#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

#define LED_GPIO 4
#define LED_GPIO_MASK (1ULL << GPIO_Pin_4)

#define PB_GPIO 5
#define PB_GPIO_MASK (1ULL << GPIO_Pin_5)

void debounce_rotary() {
  while (1) {
    static uint8_t padded_state = 0;
    static uint8_t state = 0;
    static uint8_t value = 0;
    padded_state = (padded_state << 1) | gpio_get_level(PB_GPIO) | 0xf0;
    // 10 * 2 = at least 20 ms of consecutive high followed by at least 20 ms of
    // consecutive low 11110000 11111100

    state = (state << 1) | gpio_get_level(PB_GPIO);
    // 10 * 6 = 60 ms of consecutive low
    if (state == 0xc0) {
      printf("Dialed %d\n", value);
      value = 0;
    } else if (padded_state == 0xfc) {
      value++;
      // printf("%d\n", value);
    }
    vTaskDelay(10 / portTICK_RATE_MS);
  }
}

void app_main() {
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

  gpio_set_direction(PB_GPIO, GPIO_MODE_INPUT);
  gpio_set_intr_type(PB_GPIO, GPIO_INTR_ANYEDGE);

  gpio_set_level(LED_GPIO, 1);
  printf("Initiated and ready\n");

  xTaskCreate(debounce_rotary, "debounce_rotary", 2048, NULL, 10, NULL);

  while (1) {
    vTaskDelay(1000 / portTICK_RATE_MS);
  }
}
