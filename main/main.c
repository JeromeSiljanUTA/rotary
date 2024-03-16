#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

#define LED_GPIO 4
#define LED_GPIO_MASK (1ULL << GPIO_Pin_4)

#define PB_GPIO 5
#define PB_GPIO_MASK (1ULL << GPIO_Pin_5)

#define DEBOUNCE_THRESHOLD 30

uint32_t state = 0;
volatile uint32_t prev_trigger = 0;

static xQueueHandle gpio_evt_queue = NULL;

static void gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void toggle_LED(void *arg) {
  uint32_t io_num;

  for (;;) {
    if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      uint32_t current_trigger = xTaskGetTickCount();
      if ((current_trigger - prev_trigger) > DEBOUNCE_THRESHOLD) {
        state = ~state;
        gpio_set_level(LED_GPIO, state);
      }
      prev_trigger = xTaskGetTickCount();
    }
  }
}

void app_main() {

  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(PB_GPIO, GPIO_MODE_INPUT);

  gpio_set_intr_type(PB_GPIO, GPIO_INTR_POSEDGE);

  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

  xTaskCreate(toggle_LED, "toggle_LED", 2048, NULL, 10, NULL);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(PB_GPIO, gpio_isr_handler, (void *)PB_GPIO);

  while (1) {
    vTaskDelay(100 / portTICK_RATE_MS);
  }
}
