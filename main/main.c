/* MQTT over Secure Websockets Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"


#include "mqtt_client.h"

/* Note: ESP32 don't support temperature sensor */
#include "driver/temp_sensor.h"

#define ESP_WIFI_SSID      		""		// Please insert your SSID
#define ESP_WIFI_PASS      		""		// Please insert your password
#define ESP_WIFI_AUTH_MODE		WIFI_AUTH_WPA2_PSK // See esp_wifi_types.h
#define ESP_WIFI_MAX_RETRY 		5U

#define THETHINGSIO_TOKEN_ID 	""		// Please insert your TOKEN ID
#define THETHINGSIO_MQTT_TOPIC	"v2/things/" THETHINGSIO_TOKEN_ID

#define TEMP_SENSOR_TASK_DELAY	1000U	// In milliseconds
#define MQTT_POST_DELAY			300000U	// In milliseconds

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static ip_event_got_ip_t* event = NULL;
static uint8_t u8RetryCounter = 0U;

static const char *pcTAG = "TTIO_MQTT_WS_CLIENT";

static float fTemperature = 0.0f;

static void WIFI_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{

	if (event_base == WIFI_EVENT)
	{
		switch (event_id)
		{
			case WIFI_EVENT_STA_START:
				esp_wifi_connect();
				break;
			case WIFI_EVENT_STA_DISCONNECTED:
				if (u8RetryCounter < ESP_WIFI_MAX_RETRY)
				{
					esp_wifi_connect();
					u8RetryCounter++;
					ESP_LOGI(pcTAG, "Retry to connect to the access point");
				}
				else
				{
					xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
					ESP_LOGI(pcTAG,"Connect to the access point fail");
				}
				break;
			default:
				// Do nothing (see WiFi event declarations in the esp_wifi_types.h)
				break;
		}
	}
	else if (event_base == IP_EVENT)
	{
		switch (event_id)
		{
			case IP_EVENT_STA_GOT_IP:
				event = (ip_event_got_ip_t*) event_data;
				u8RetryCounter = 0U;
				xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
				ESP_LOGI(pcTAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
				break;
			default:
				// Do nothing (see WiFi event declarations in the esp_netif_types.h)
				break;
		}
	}

}

static void MQTT_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{

	const char acThingData[] = "{\"values\":[{\"key\":\"Temperature\",\"value\":\"25.00\"}]}";

	esp_mqtt_event_handle_t event = event_data;

    switch (event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(pcTAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(event->client, THETHINGSIO_MQTT_TOPIC, 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(pcTAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(pcTAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            sprintf((char *)acThingData, "{\"values\":[{\"key\":\"Temperature\",\"value\":\"%2.2f\"}]}", fTemperature);
            esp_mqtt_client_publish(event->client, THETHINGSIO_MQTT_TOPIC, acThingData, strlen(acThingData), 0, 0);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(pcTAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            esp_mqtt_client_disconnect(event->client);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(pcTAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(pcTAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            esp_mqtt_client_unsubscribe(event->client, THETHINGSIO_MQTT_TOPIC);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(pcTAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(pcTAG, "Other event id:%d", event->event_id);
            break;
    }

}

void wifi_init_sta(void)
{

	wifi_init_config_t sWifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;

	EventBits_t WifiEventBits = 0U;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_init(&sWifiInitCfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WIFI_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WIFI_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t sWifiConfig = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = ESP_WIFI_AUTH_MODE,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sWifiConfig));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(pcTAG, "Wi-Fi initializated");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
    WifiEventBits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (WifiEventBits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(pcTAG, "Connected to access point SSID: %s, Password: %s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (WifiEventBits & WIFI_FAIL_BIT) {
        ESP_LOGI(pcTAG, "Failed to connect to SSID: %s, Password: %s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(pcTAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));

    vEventGroupDelete(s_wifi_event_group);

}

void TempSensor_Task(void *pvParams)
{

	temp_sensor_config_t sTemperatureInitCfg = TSENS_CONFIG_DEFAULT();

    // Initialize touch pad peripheral, it will start a timer to run a filter
    ESP_LOGI(pcTAG, "Initializing Temperature sensor");

    temp_sensor_get_config(&sTemperatureInitCfg);
    ESP_LOGI(pcTAG, "Default offset: %d, clk_div: %d", sTemperatureInitCfg.dac_offset, sTemperatureInitCfg.clk_div);
    temp_sensor_set_config(sTemperatureInitCfg);
    temp_sensor_start();
    ESP_LOGI(pcTAG, "Temperature sensor started");

    vTaskDelay(1000U / portTICK_RATE_MS);

    while (1)
    {
        temp_sensor_read_celsius(&fTemperature);
        ESP_LOGI(pcTAG, "Temperature out Celsius: %2.2f", fTemperature);
        vTaskDelay(TEMP_SENSOR_TASK_DELAY / portTICK_RATE_MS);
    }

    ESP_LOGI(pcTAG, "Finish temperature sensor");
    vTaskDelete(NULL);

}

static void MQTT_Task(void *pvParameters)
{

    const esp_mqtt_client_config_t sMqttInitCfg = {
        .uri = "mqtt://mqtt.thethings.io",
		.port = 1883,
		.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
		.disable_auto_reconnect = true,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&sMqttInitCfg);

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, MQTT_event_handler, client);

    vTaskDelay(1100U / portTICK_RATE_MS);

    while(1)
    {
    	esp_mqtt_client_start(client);

		vTaskDelay(MQTT_POST_DELAY / portTICK_RATE_MS);
    }

    ESP_LOGI(pcTAG, "Finish mqtt requests");
	vTaskDelete(NULL);

}

void app_main(void)
{

	// Initialize NVS
	esp_err_t ret = nvs_flash_init();

	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}

	ESP_ERROR_CHECK(ret);

	// Initialize station mode
	wifi_init_sta();

	xTaskCreate(TempSensor_Task, "tempsensor_task", 2048, NULL, 5, NULL);
	xTaskCreate(&MQTT_Task, "mqtt_task", 8192U, NULL, 5U, NULL);

}
