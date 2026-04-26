#include "wifi.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "WIFI";

// 回调里置位，wifi_sta_connect() 里等待这些位
static bool s_wifi_connected = false;
static bool s_wifi_initialized = false;
static int s_retry_count = 0;
static const int s_max_retry = 10;
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_event_handler_instance_t s_instance_any_id;
static esp_event_handler_instance_t s_instance_got_ip;

// wifi/IP事件回调，处理三种事件：
// STA_START：日志记录
// STA_DISCONNECTED：自动重连，超过最大重试次数后置 FAIL 位
// STA_GOT_IP：置CONNECTED位，标记连接成功
static void wifi_event_handler(void *args, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        // 这里只表示 Wi-Fi 驱动已启动，真正发起连接放在 wifi_sta_connect()
        ESP_LOGI(TAG, "WIFI started, connecting...");
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WIFI disconnected, reason: %d, reconnecting...", event->reason);
        s_wifi_connected = false;
        
        if(s_retry_count < s_max_retry)
        {
            // 断线后在事件回调里自动重连
            s_retry_count++;
            esp_wifi_connect();
        }
        else
        {
            // 唤醒等待方：这次连接流程已经失败
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP))
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_retry_count = 0;
        // 只有拿到 IP 才算真正连通
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Wi-Fi 初始化顺序基本固定：NVS -> netif -> event loop -> wifi driver -> start
esp_err_t wifi_sta_init(void)
{
    if(s_wifi_initialized)
    {
        return ESP_OK;
    }
    esp_err_t err;

    err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = (nvs_flash_init());
    }
    if(err != ESP_OK)
    {
        return err;
    }

    err = esp_netif_init();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        return err;
    }

    err = esp_event_loop_create_default();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        return err;
    }

    s_wifi_event_group = xEventGroupCreate();
    if(s_wifi_event_group == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if(s_sta_netif == NULL)
    {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if(err != ESP_OK)
    {
        return err;
    }

    // 注册 Wi-Fi/IP 事件，后面所有连接状态都从这里回调回来
    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &wifi_event_handler,
                                              NULL,
                                              &s_instance_any_id);
    if(err != ESP_OK)
    {
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler,
                                              NULL,
                                              &s_instance_got_ip);
    if(err != ESP_OK)
    {
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err != ESP_OK)
    {
        return err;
    }
    err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if(err != ESP_OK) 
    {
        return err;
    }
    err = esp_wifi_start();
    if(err != ESP_OK)
    {
        return err;
    }
    s_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_sta_connect(TickType_t timeout_ticks)
{
    EventBits_t bits;

    if (!s_wifi_initialized || s_wifi_event_group == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_connected) {
        return ESP_OK;
    }

    // 每次发起新连接前，先清掉上一次等待留下的状态位
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    esp_wifi_connect();

    // 把“异步回调”转成“同步等待结果”的接口
    bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        timeout_ticks
    );

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

void wifi_sta_deinit(void)
{
    if(!s_wifi_initialized)
    {
        return;
    }
    if(s_wifi_connected)
    {
        esp_wifi_disconnect();
        s_wifi_connected = false;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    if(s_instance_any_id != NULL)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_instance_any_id);
        s_instance_any_id = NULL;
    }
    if(s_instance_got_ip != NULL)
    {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_instance_got_ip);
        s_instance_got_ip = NULL;
    }
    if(s_sta_netif != NULL)
    {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    esp_event_loop_delete_default();
    nvs_flash_deinit();
    s_wifi_initialized = false;
    s_retry_count = 0;
}

bool wifi_sta_is_connected(void)
{
    return s_wifi_connected;
}

bool wifi_sta_is_initialized(void)
{
    return s_wifi_initialized;
}
