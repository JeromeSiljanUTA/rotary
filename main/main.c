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

uint8_t new_number = 0;
uint8_t value = 0;

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

void debounce_rotary() {
  while (1) {
    // Inspired by https://www.ganssle.com/debouncing-pt2.htm
    static uint8_t padded_state = 0;
    static uint8_t state = 0;
    padded_state = (padded_state << 1) | gpio_get_level(PB_GPIO) | 0xf0;
    // 10 * 2 = at least 20 ms of consecutive high followed by at least 20 ms of
    // consecutive low 11110000 11111100
    state = (state << 1) | gpio_get_level(PB_GPIO);
    // 10 * 6 = 60 ms of consecutive low
    if (state == 0xc0) {
      value = value % 10; // return 0 if 10
      // printf("Dialed %d\n", value);
      new_number = 1;
    } else if (padded_state == 0xfc) {
      value++;
    }
    vTaskDelay(10 / portTICK_RATE_MS);
  }
}

void handle_number() {
  while (1) {
    if (new_number) {
      printf("Dialed %d\n", value);
      new_number = 0;
      value = 0;
      http_post();
    }
    vTaskDelay(300 / portTICK_RATE_MS);
  }
}

void app_main() {
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

  gpio_set_direction(PB_GPIO, GPIO_MODE_INPUT);
  gpio_set_intr_type(PB_GPIO, GPIO_INTR_ANYEDGE);

  gpio_set_level(LED_GPIO, 1);

  wifi_init_sta();

  printf("Initiated and ready\n");

  xTaskCreate(debounce_rotary, "debounce_rotary", 2048, NULL, 10, NULL);
  xTaskCreate(handle_number, "handle_number", 2048, NULL, 10, NULL);

  while (1) {
    vTaskDelay(1000 / portTICK_RATE_MS);
  }
}
