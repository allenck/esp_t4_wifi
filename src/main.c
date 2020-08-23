/*

MIT No Attribution

Copyright (c) 2020 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-cut-

SPDX-License-Identifier: MIT-0

*/

#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>

#include <hagl_hal.h>
#include <hagl.h>
#include <font6x9.h>
#include "fnt9x18b.h"
#include <fps.h>


//#include "metaballs.h"
//#include "plasma.h"
//#include "rotozoom.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_event_loop.h"
//#include "esp_log.h"
#include "nvs_flash.h"
//#include "freertos/timers.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_http_client.h"
#include "lwip/apps/sntp.h"
//#include "courB14.h"
#include "helvR18.h"
#include "font7x13.h"
#include <cJSON.h>

#define EXAMPLE_ESP_WIFI_SSID      "WAUDUBON"//CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      "298 W Audubon Dr"//CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   11//CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       3//CONFIG_MAX_STA_CONN

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "main";
static EventGroupHandle_t event;
static float fb_fps;
static uint8_t effect = 0;
static bool connected=false;
static bool time_display_started = false;
static bool sntp_time_started = false;

static const uint8_t RENDER_FINISHED = (1 << 0);
static const uint8_t FLUSH_STARTED= (1 << 1);
static int s_retry_num = 0;
static char * ipAddr;

// Bitmap_WiFi
 extern const uint8_t wifi_1[];
 extern uint8_t wifi_2[];
 extern uint8_t wifi_3[];
#if 0
static char demo[3][32] = {
    "3 METABALLS   ",
    "PALETTE PLASMA",
    "ROTOZOOM      ",
};
#endif

char* jsonBuffer = NULL;
size_t jsonBuffer_len = 0;

cJSON* current = NULL;
cJSON *root = NULL;
cJSON* weather_array = NULL;
cJSON* weather =NULL;

esp_err_t _http_event_handle(esp_http_client_event_t *evt);
void time_display(void *params);
void time_cb(struct timeval *tv);

/*
 * Flushes the backbuffer to the display. Needed when using
 * double or triple buffering.
 */
void flush_task(void *params)
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            event,
            RENDER_FINISHED,
            pdTRUE,
            pdFALSE,
            0
        );

        /* Flush only when RENDER_FINISHED is set. */
        if ((bits & RENDER_FINISHED) != 0 ) {
            xEventGroupSetBits(event, FLUSH_STARTED);
            hagl_flush();
            fb_fps = fps();
        }
    }

    vTaskDelete(NULL);
}

/*
 * Software vsync. Waits for flush to start. Needed to avoid
 * tearing when using double buffering, NOP otherwise. This
 * could be handler with IRQ's if the display supports it.
 */
static void wait_for_vsync()
{
#ifdef CONFIG_HAGL_HAL_USE_DOUBLE_BUFFERING
    xEventGroupWaitBits(
        event,
        FLUSH_STARTED,
        pdTRUE,
        pdFALSE,
        10000 / portTICK_RATE_MS
    );
    ets_delay_us(25000);
#endif /* CONFIG_HAGL_HAL_USE_DOUBLE_BUFFERING */
}

/*
 * Changes the effect every 15 seconds.
 */
void switch_task(void *params)
{
    while (1) {
        hagl_clear_screen();
        effect = (effect + 1) % 3;

        vTaskDelay(15000 / portTICK_RATE_MS);
    }

    vTaskDelete(NULL);
}

void start_NTP()
{
    // start sntp NTP client
    ESP_LOGI(TAG,"start sntp NTP client");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_cb);
    sntp_init();
    sntp_time_started = true; 

    
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s", ipAddr =
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        connected = true;
        
        hagl_fill_rectangle(70,50,170, 150, 0); // erase completely
        
        if(!time_display_started)
        {
        xTaskCreatePinnedToCore(time_display, "Switch", 8092, NULL, 2, NULL, 1);
        time_display_started = true;
        }
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_retry_num++;
            ESP_LOGI(TAG,"retry to connect to the AP");
        }
        ESP_LOGI(TAG,"connect to the AP fail\n");
        connected = false;
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

/*
 * Runs the actual demo effect.
 */
void demo_task(void *params)
{
#if 0
    color_t green = hagl_color(0, 255, 0);
    char16_t message[128];

    metaballs_init();
    plasma_init();
    rotozoom_init();

    while (1) {
        switch(effect) {
        case 0:
            metaballs_animate();
            wait_for_vsync();
            metaballs_render();
            break;
        case 1:
            plasma_animate();
            wait_for_vsync();
            plasma_render();
            break;
        case 2:
            rotozoom_animate();
            wait_for_vsync();
            rotozoom_render();
            break;
        }
        /* Notify flush task that rendering has finished. */
        xEventGroupSetBits(event, RENDER_FINISHED);

        /* Print the message on top left corner. */
        swprintf(message, sizeof(message), u"%s    ", demo[effect]);
        hagl_set_clip_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
        hagl_put_text(message, 4, 4, green, font6x9);

        /* Print the message on lower right corner. */
        swprintf(message, sizeof(message), u"%.*f FPS  ", 0, fb_fps);
        hagl_put_text(message, DISPLAY_WIDTH - 40, DISPLAY_HEIGHT - 14, green, font6x9);
        hagl_set_clip_window(0, 20, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 21);
    }
#else
    esp_http_client_config_t config = {
    .url = "https://api.openweathermap.org/data/2.5/onecall?lat=37.98&lon=-85.71&units=metric&lang=en&exclude=minutely,hourly&appid=0e40f74ecc24c41f6df71c302267dfba",
    .event_handler = _http_event_handle,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
    ESP_LOGI(TAG, "Status = %d, content_length = %d",
            esp_http_client_get_status_code(client),
            esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);
#endif
    vTaskDelete(NULL);
}

void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                              int16_t w, int16_t h, color_t color) {

  int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
  uint8_t byte = 0;

  //startWrite();
  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      if (i & 7)
        byte <<= 1;
      else
        byte = /*pgm_read_byte*/(bitmap[j * byteWidth + i / 8]);
      if (byte & 0x80)
        hagl_hal_put_pixel(x + i, y, color);
    }
  }
  //endWrite();
}


void display_wifi(void *params)
{
    // display WiFi searching icon
    // Block for 200ms.
    const TickType_t xDelay = 200 / portTICK_PERIOD_MS;
    int ix=0;
    color_t color = hagl_color(0, 255, 0);

    while(!connected)
    {
        switch (ix%4)
        {
        case 0:
            drawBitmap( 70, 50, wifi_1, 100, 100, color);        
            break;
        case 1:
            drawBitmap( 70, 50, wifi_2, 100, 100, color);        
            break;
        case 2:
            drawBitmap( 70, 50, wifi_3, 100, 100, color);        
            break;
        case 3:
            hagl_fill_rectangle(70,50,170, 150, 0); // erase completely
            break;
        default:
            break;
        }
        ix++;
        vTaskDelay( xDelay );
    }
    vTaskDelete(NULL);
}

esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
                if(jsonBuffer == 0)
                {
                    jsonBuffer = (char*)malloc(evt->data_len);
                    memcpy(jsonBuffer, (char*)evt->data, evt->data_len);
                    jsonBuffer_len = evt->data_len;
                }
                else
                {
                    jsonBuffer = (char*)realloc(jsonBuffer, jsonBuffer_len +evt->data_len);
                    memcpy(jsonBuffer + jsonBuffer_len, (char*)evt->data, evt->data_len);
                    jsonBuffer_len += evt->data_len;
                }
                
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if(root)
            {
                free(root);
            }
            root = cJSON_Parse(jsonBuffer);
            //free(jsonBuffer);
            //jsonBuffer_len = 0;
            current = cJSON_GetObjectItem(root, "current");
            if(current)
            {
                ESP_LOGI(TAG, "current found!");
                cJSON* dt = cJSON_GetObjectItem(current, "dt");
                cJSON* temp = cJSON_GetObjectItem(current, "temp");
                cJSON* humidity = cJSON_GetObjectItem(current, "humidity");
                cJSON* pressure = cJSON_GetObjectItem(current, "pressure");
                cJSON* dewPoint = cJSON_GetObjectItem(current, "dew_point");
                weather_array = cJSON_GetObjectItem(current, "weather");
                cJSON* rain = cJSON_GetObjectItem(current, "rain");
                char *string = NULL;
                char16_t message[40];
                if(temp)
                {
                    ESP_LOGI(TAG, "temp found!");
                    time_t utc;
                    char* pEnd;
                    char strftime_buf[64];
                    struct tm timeinfo;
                    string = cJSON_Print(dt);
                    utc = (time_t)strtol(string, &pEnd,10);
                    localtime_r(&utc, &timeinfo);
                    strftime(strftime_buf, sizeof(strftime_buf), "%T", &timeinfo);
                    ESP_LOGI(TAG, "The last report at: %s", strftime_buf);
                    swprintf(message, sizeof(message), u"%s    ", strftime_buf);
                    hagl_put_text(message, 4, 41, hagl_color(255, 0, 255), fnt9x18b);
                    
                    string = cJSON_Print(temp);
                    if(string)
                    {
                        ESP_LOGI(TAG, "temperature = %s", string);
                        char* pEnd;
                        double tC = strtod(string, &pEnd);
                        double tF = (tC*9/5) +32;
                        int rh = strtol(cJSON_Print(humidity), &pEnd, 10);
                        
                        swprintf(message, sizeof(message), L"%.*f°C %.*f°F rh: %d%%", 0, tC,0, tF, rh);
                        hagl_put_text(message, 4, 61, hagl_color(255, 255, 0), fnt9x18b);
                        free(string);

                        string = cJSON_Print(pressure);
                        double pM = strtod(string, &pEnd);
                        double pI = pM * 0.02953;
                        swprintf(message, sizeof(message), L"bar: %.*f mb / %.*f\"", 0, pM,0, pI);
                        hagl_put_text(message, 4, 101, hagl_color(255, 255, 0), fnt9x18b);
                        free(string);

                        string = cJSON_Print(dewPoint);
                        tC = strtod(string, &pEnd);
                        tF = (tC*9/5) +32;
                        swprintf(message, sizeof(message), L"dew point: %.*f°C %.*f°F", 0, tC,0, tF);
                        hagl_put_text(message, 4, 121, hagl_color(0, 255, 0), fnt9x18b);
                        free(string);

                        if(rain)
                        {

                            cJSON* lastHr = cJSON_GetObjectItem(rain, "1h");
                            if(lastHr)
                            { 
                                char *string = cJSON_Print(lastHr);
                                ESP_LOGI(TAG, "rain %s", string);
                                double dRain = strtod(string, &pEnd);
                                swprintf(message, sizeof(message), L"rain: %.2f\"", 0, dRain);
                                hagl_put_text(message, 4, 141, hagl_color(0, 0, 255), fnt9x18b);
                                free(string);
                                free(lastHr);
                            }
                            else{
                                ESP_LOGI(TAG, "no rain lh");
                            }
                            free(rain);
                        }
                    else{
                                ESP_LOGI(TAG, "no rain object");
                        }
                    }
                    free(dt);
                    free(temp);  
                    free(humidity);
                    free(pressure);
                    free(dewPoint);
                }
                if(weather_array && cJSON_IsArray(weather_array))
                {
                    ESP_LOGI(TAG, "weather array found.");
                    ESP_LOGI(TAG, "weather %d items", cJSON_GetArraySize(weather_array));
                    weather = cJSON_GetArrayItem(weather_array, 0);
                    if(weather) 
                    {
                        cJSON* description = cJSON_GetObjectItem(weather, "description");
                        cJSON* icon = cJSON_GetObjectItem(weather, "icon");
                        char* string = cJSON_Print(description);
                        char16_t message[20];
                        swprintf(message, sizeof(message), u"%s    ", string);
                        hagl_put_text(message, 0, 81, hagl_color(255, 0, 0), fnt9x18b );
                        free(description);
                        free(string);
                        free(weather);
                        free(icon);
                    }    
                    free(weather_array);          
                }
                else
                {
                    ESP_LOGI(TAG, "weather array not found");
                }
                free(current);
            }
            free(root);
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

void time_cb(struct timeval *tv)
{
    // NTP time received
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    time(&now);
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);

    if(!time_display_started)
    {
     xTaskCreatePinnedToCore(time_display, "Switch", 8092, NULL, 2, NULL, 1);
     time_display_started = true;
    }

    xTaskCreatePinnedToCore(demo_task, "Demo", 8092, NULL, 1, NULL, 1);
}

void time_display(void *params)
{
    color_t green = hagl_color(0, 255, 0);
    char16_t message[128];
    swprintf(message, sizeof(message), u"%s    ", ipAddr);
    hagl_set_clip_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    hagl_put_text(message, 4, 4, green, font6x9);
    
    // display time every second
    color_t yellow = hagl_color(255, 255, 0);
    // Block for 1000ms.
    const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
    
    // start sntp time
    if(!sntp_time_started)
    {
        start_NTP();
    }

    for( ;; )
    {
        // Simply toggle the LED every 500ms, blocking between each toggle.
        time_t now;
        char16_t message[128];
        char strftime_buf[64];
        struct tm timeinfo;

        time(&now);
        // Set timezone to Eastern Standard Time
        setenv("TZ", "EST5EDT", 1);
        tzset();

        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        swprintf(message, sizeof(message), u"%s    ", strftime_buf);
        hagl_set_clip_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
        hagl_put_text(message, 4, 21, yellow, fnt9x18b);
        //ESP_LOGI(TAG, "%s", strftime_buf);
        int min = timeinfo.tm_min;
        if(min%5 ==0 && timeinfo.tm_sec == 0)
         xTaskCreatePinnedToCore(demo_task, "Demo", 8092, NULL, 1, NULL, 1);
        vTaskDelay( xDelay );
    }
}

void wifi_init_softap(void *params)
{
    ESP_LOGI(TAG, "Heap wifi_init_softap enter: %d", esp_get_free_heap_size());
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);

    vTaskDelete(NULL);
}

void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "ESP_IDF version: %d.%d.%d", ESP_IDF_VERSION_MAJOR, 
    ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    ESP_LOGI(TAG, "Heap when starting: %d", esp_get_free_heap_size());

    event = xEventGroupCreate();

    hagl_init();
    /* Reserve 20 pixels in top and bottom for debug texts. */
    hagl_set_clip_window(0, 20, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 21);

    ESP_LOGI(TAG, "Heap after HAGL init: %d", esp_get_free_heap_size());

    //ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
     
#ifdef HAGL_HAL_USE_BUFFERING
    xTaskCreatePinnedToCore(flush_task, "Flush", 4096, NULL, 1, NULL, 0);
#endif
    //xTaskCreatePinnedToCore(demo_task, "Demo", 8092, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(display_wifi, "wifi_loading", 8092, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(wifi_init_softap, "wifi", 8092, NULL, 2, NULL, 1);

}
