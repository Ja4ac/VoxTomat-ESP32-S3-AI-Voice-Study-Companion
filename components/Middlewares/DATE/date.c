#include "date.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "DATE";
static esp_http_client_handle_t s_http_client;
static date_t s_base_date = {0};
static int64_t s_base_us = 0;
static SemaphoreHandle_t s_mutex = NULL;

// 闰年判断
static bool date_is_leap_year(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

// 天数计算
static int date_days_in_month(int y, int m)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};

    if(m == 2 && date_is_leap_year(y)) {
        return 29;
    }

    return days[m - 1];
}

// 调用一次时间api接口获取时间
esp_err_t date_get_time()
{
    esp_err_t err = ESP_OK;
    cJSON *root = NULL;

    if(s_http_client == NULL) 
    {
        return ESP_ERR_INVALID_STATE;
    }
    
    err = esp_http_client_open(s_http_client, 0);
    if(err != ESP_OK)
    {
        goto cleanup;
    }

    int content = esp_http_client_fetch_headers(s_http_client);
    int status_code = esp_http_client_get_status_code(s_http_client);
    if(status_code != 200)
    {
        ESP_LOGE(TAG, "Status code error: %d", status_code);
        err = ESP_FAIL;
        goto cleanup;
    }

    char buf[512];
    int read_len = esp_http_client_read(s_http_client, buf, sizeof(buf) - 1);

    int64_t recv_us = esp_timer_get_time();         // 暂存当前计数器值，如果 JSON 解析成功就存入 s_base_us
    
    if(read_len < 0) 
    {
        err = ESP_FAIL;
        goto cleanup;
    }
    buf[read_len] = '\0';

    root = cJSON_Parse(buf);
    if(root == NULL)
    {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *y = cJSON_GetObjectItem(root, "y");
    cJSON *m = cJSON_GetObjectItem(root, "m");
    cJSON *d = cJSON_GetObjectItem(root, "d");
    cJSON *h = cJSON_GetObjectItem(root, "h");
    cJSON *i = cJSON_GetObjectItem(root, "i");
    cJSON *s = cJSON_GetObjectItem(root, "s");
    if(!cJSON_IsNumber(code) || code->valueint != 200 || !cJSON_IsString(y) || !cJSON_IsString(m) || !cJSON_IsString(d) || !cJSON_IsString(h) || !cJSON_IsString(i) || !cJSON_IsString(s)) 
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    date_t net_date = {0};

    net_date.code = code->valueint;
    net_date.year = atoi(y->valuestring);
    net_date.month = atoi(m->valuestring);
    net_date.day = atoi(d->valuestring);
    net_date.hour = atoi(h->valuestring);
    net_date.minute = atoi(i->valuestring);
    net_date.second = atoi(s->valuestring);
    ESP_LOGI(TAG, "获取时间成功: %04d-%02d-%02d %02d:%02d:%02d",
         net_date.year, net_date.month, net_date.day,
         net_date.hour, net_date.minute, net_date.second);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_base_date = net_date;
    s_base_us = recv_us;
    xSemaphoreGive(s_mutex);

    goto cleanup;
    cleanup:
        if(root != NULL)
        {
            cJSON_Delete(root);
            root = NULL;
        }
        if(s_http_client != NULL) 
        {
            esp_http_client_close(s_http_client);
        }
        return err;
}

static esp_err_t date_http_init(void)
{
    esp_err_t err = ESP_OK;
    char url[256];
    
    snprintf(url, sizeof(url), "%s?id=%s&key=%s&type=%s", DATE_BASE_URL, DATE_USER_ID, DATE_USER_KEY, DATE_TYPE);
    esp_http_client_config_t http_client_config = 
    {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 512,
    };
    s_http_client = esp_http_client_init(&http_client_config);
    if(s_http_client == NULL)
    {
        err = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Faied to init date http");
        return err;
    }
    ESP_LOGI(TAG, "Init date http");
        return err;
}

esp_err_t date_init(void)
{
    esp_err_t err;

    if(!wifi_sta_is_initialized())
    {
        err = wifi_sta_init();
        if(err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init wifi");
            return err;
        }
    }
    if(!wifi_sta_is_connected())
    {
        err = wifi_sta_connect(15000);
        if(err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init wifi");
            return err;
        }
    } 

    if(s_mutex == NULL)
    {
        s_mutex = xSemaphoreCreateMutex();
        if(s_mutex == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    err = date_http_init();
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init date");
        return err;
    }

    err = date_get_time();
    if(err != ESP_OK) 
    {
        return err;
    }

    return err;
}

esp_err_t date_deinit(void)
{
    esp_err_t err = ESP_OK;
    if(s_http_client != NULL)
    {
        err = esp_http_client_cleanup(s_http_client);
        if(err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to cleanup http client handle");
            return err;
        }
        s_http_client = NULL;
    }
    return err;
}

static void date_add_seconds(date_t *app_date, int64_t elapsed_sec)
{
    if(app_date == NULL || elapsed_sec <= 0) {
        return;
    }

    app_date->second += elapsed_sec;

    app_date->minute += app_date->second / 60;
    app_date->second %= 60;

    app_date->hour += app_date->minute / 60;
    app_date->minute %= 60;

    app_date->day += app_date->hour / 24;
    app_date->hour %= 24;

    while(app_date->day > date_days_in_month(app_date->year, app_date->month)) {
        app_date->day -= date_days_in_month(app_date->year, app_date->month);
        app_date->month++;

        if(app_date->month > 12) {
            app_date->month = 1;
            app_date->year++;
        }
    }
}

esp_err_t date_get_current_time(date_t *app_date)
{
    if(app_date == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }
    if(s_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    date_t base_date = {0};
    int64_t base_us;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    base_date = s_base_date;
    base_us = s_base_us;
    xSemaphoreGive(s_mutex);

    int64_t elapsed_sec = (esp_timer_get_time() - base_us) / 1000000;

    *app_date = base_date;
    date_add_seconds(app_date, elapsed_sec);

    return ESP_OK;
}