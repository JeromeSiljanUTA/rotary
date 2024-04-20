#include "credentials.h"
#include "driver/gpio.h"
#include "driver/hw_timer.h"
#include "esp_event_loop.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

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

static const char *TAG = "wifi station";

static esp_err_t event_handler(void *ctx, system_event_t *event) {
  switch (event->event_id) {
  case SYSTEM_EVENT_STA_START:
    ESP_LOGI(TAG, "Wi-Fi started");
    esp_wifi_connect();
    break;
  case SYSTEM_EVENT_STA_CONNECTED:
    ESP_LOGI(TAG, "Connected to AP");
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    ESP_LOGI(TAG, "Got IP address");

    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    ESP_LOGI(TAG, "Disconnected from AP");
    esp_wifi_connect();
    break;
  default:
    break;
  }
  return ESP_OK;
}

void http_post() {
  // Perform HTTP POST request
  esp_http_client_config_t config = {
      .url = URL,
      .method = HTTP_METHOD_POST,
      .event_handler = NULL,
      .buffer_size = 4096,
      .timeout_ms = 10000,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  const char *post_data = "{\"entity_id\":\"light.desk_lamp_light\"}";
  esp_http_client_set_post_field(client, post_data, strlen(post_data));
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Authorization", HASS_TOKEN);
  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
}

void wifi_init_sta() {
  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASSWORD,
          },
  };
  ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

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
          if (num_dialed == 10) {
            num_dialed = 0;
          }
          prev_num_dialed = num_dialed;
          hw_timer_alarm_us(FINISHED_DIALING, false);
        }
      } else {
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

  wifi_init_sta();

  while (1) {
    vTaskDelay(100 / portTICK_RATE_MS);
    if (new_dialed_flag) {
      http_post();
      printf("dialed %d\n", num_dialed);
      new_dialed_flag = 0;
    }
  }
}
