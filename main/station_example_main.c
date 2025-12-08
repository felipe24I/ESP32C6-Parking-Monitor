/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "lwip/err.h"
#include "lwip/sys.h"
#include "mqtt_client.h"
#include "led_strip.h"

// Configuración del LED RGB WS2812 en ESP32-C6-DevKitC-1
#define LED_STRIP_GPIO    8
#define LED_NUMBERS       1

#define NUM_SPOTS 4   // número de plazas de parqueo simuladas

static bool parking_spots[NUM_SPOTS] = { false }; // false = libre, true = ocupado
static uint32_t msg_counter = 0;                  // contador de mensajes enviados

static esp_mqtt_client_handle_t mqtt_client = NULL;
static led_strip_handle_t led_strip = NULL;

static void led_set_color(const char* color) {
    ESP_LOGI("LED", "Encendiendo LED RGB color: %s", color);
    
    if (strcmp(color, "red") == 0) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 0, 0)); // RGB: Rojo puro
    } else if (strcmp(color, "green") == 0) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 255, 0)); // RGB: Verde puro
    } else if (strcmp(color, "blue") == 0) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 255)); // RGB: Azul puro
    }
    
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void update_led_from_parking_state(void) {
    int occupied = 0;
    for (int i = 0; i < NUM_SPOTS; i++) {
        if (parking_spots[i]) {
            occupied++;
        }
    }

    if (occupied == 0) {
        // Todas libres
        led_set_color("green");
    } else if (occupied == NUM_SPOTS) {
        // Todas ocupadas
        led_set_color("red");
    } else {
        // Parcialmente ocupado
        led_set_color("blue");
    }
}

static void simulate_parking_state(void) {
    // Elegir una plaza al azar
    uint32_t rand_val = esp_random();
    int spot = rand_val % NUM_SPOTS;

    // Probabilidad del 30% de cambiar esa plaza
    if ((esp_random() % 100) < 30) {
        parking_spots[spot] = !parking_spots[spot];
        ESP_LOGI("PARKING", "Plaza %d ahora está %s",
                 spot + 1,
                 parking_spots[spot] ? "OCUPADA" : "LIBRE");
    }
}

static void build_parking_json(char *buffer, size_t len) {
    int offset = snprintf(
        buffer,
        len,
        "{\"device_id\":\"esp32c6_parking_1\",\"msg_id\":%" PRIu32 ",\"spots\":[",
        msg_counter
    );

    for (int i = 0; i < NUM_SPOTS && offset < (int)len; i++) {
        offset += snprintf(
            buffer + offset,
            len - offset,
            "{\"id\":%d,\"status\":\"%s\"}%s",
            i + 1,
            parking_spots[i] ? "occupied" : "free",
            (i < NUM_SPOTS - 1) ? "," : ""
        );
    }

    if (offset < (int)len) {
        snprintf(buffer + offset, len - offset, "]}");
    }
}

static void parking_task(void *pvParameters) {
    char payload[256];

    while (1) {
        // Simular cambios en las plazas
        simulate_parking_state();

        // Actualizar LED según ocupación
        update_led_from_parking_state();

        // Incrementar contador de mensajes
        msg_counter++;

        // Construir JSON
        memset(payload, 0, sizeof(payload));
        build_parking_json(payload, sizeof(payload));

        // Publicar por MQTT
        if (mqtt_client) {
            int msg_id = esp_mqtt_client_publish(
                mqtt_client,
                "esp32/parking",   // 🔹 NUEVO TEMA MQTT
                payload,
                0,                 // length 0 = se calcula por strlen
                1,                 // QoS 1
                0                  // no retain
            );

            ESP_LOGI("PARKING", "Publicado mensaje %u, msg_id=%d: %s",
                     msg_counter, msg_id, payload);
        } else {
            ESP_LOGW("PARKING", "mqtt_client NULL, no se publicó");
        }

        // Esperar 3 segundos
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "Connected to MQTT broker");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("MQTT", "Disconnected from MQTT broker");
            break;
        default:
            break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://54.219.187.119:1883",
        .credentials.username = "esp32",
        .credentials.authentication.password = "esp32",
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "FIQ PLUS"
#define EXAMPLE_ESP_WIFI_PASS      "3218514197"
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // Configuración del LED RGB WS2812
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_NUMBERS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "LED RGB WS2812 inicializado correctamente");

    // Prueba inicial: ciclo de colores reales
    ESP_LOGI(TAG, "Probando LED RGB...");
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 0, 0)); // Rojo
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 255, 0)); // Verde
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 255)); // Azul
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 0)); // Apagar
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "Prueba de LED RGB completada");

    // Inicializa MQTT
    mqtt_app_start();

    // Crea la tarea para cambiar el color del LED y publicar en MQTT
    // Crea la tarea del controlador de parqueadero
    xTaskCreate(parking_task, "parking_task", 4096, NULL, 5, NULL);
}
