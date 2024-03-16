#include "driver/gpio.h"
#include "driver/hw_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

#define LED_GPIO 4
#define LED_GPIO_MASK (1ULL << GPIO_Pin_4)

#define PB_GPIO 5
#define PB_GPIO_MASK (1ULL << GPIO_Pin_5)

#define DEBOUNCE_THRESHOLD 5
#define RESET_THRESHOLD 10
#define FINISHED_DIALING 125000

uint32_t state = 0;
volatile uint32_t prev_trigger = 0;
volatile uint32_t num_dialed = 0;
volatile uint32_t prev_num_dialed = 0;
volatile uint32_t new_dialed_flag = 0;

static xQueueHandle gpio_evt_queue = NULL;

static void callback(void *arg) {
  if (prev_num_dialed == num_dialed) {
    gpio_set_level(LED_GPIO, 1);
    new_dialed_flag = 1;
  }
}

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
        if ((current_trigger - prev_trigger) > RESET_THRESHOLD) {
          num_dialed = 0;

        } else {
          num_dialed++;
          prev_num_dialed = num_dialed;
          hw_timer_alarm_us(FINISHED_DIALING, false);
        }
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

  printf("Initiated and ready\n");

  hw_timer_init(callback, NULL);

  while (1) {
    vTaskDelay(100 / portTICK_RATE_MS);
    if (new_dialed_flag) {
      printf("dialed %d\n", num_dialed);
      new_dialed_flag = 0;
    }
  }
}
