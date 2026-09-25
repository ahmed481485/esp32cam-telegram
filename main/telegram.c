#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_camera.h"
#include "telegram.h"
#include "personal.h"

#define API      "https://api.telegram.org/bot" TG_BOT_TOKEN
#define BOUNDARY "esp32camboundary"

// Take a picture and upload it to your chat
static void send_photo(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    // Body = head + JPEG bytes + tail (a normal "form upload")
    const char *head =
        "--" BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        TG_CHAT_ID "\r\n"
        "--" BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n\r\n";
    const char *tail = "\r\n--" BOUNDARY "--\r\n";

    esp_http_client_config_t cfg = {
        .url = API "/sendPhoto",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type", "multipart/form-data; boundary=" BOUNDARY);

    if (esp_http_client_open(c, strlen(head) + fb->len + strlen(tail)) == ESP_OK) {
        esp_http_client_write(c, head, strlen(head));
        esp_http_client_write(c, (const char *)fb->buf, fb->len);
        esp_http_client_write(c, tail, strlen(tail));
        esp_http_client_fetch_headers(c);
    }
    esp_http_client_cleanup(c);
    esp_camera_fb_return(fb);
}

// Keep asking Telegram for new messages; on "/photo" send a picture
static void telegram_task(void *arg)
{
    static char buf[2048];
    char url[256];
    long long offset = 0;   // id of the next message we haven't handled yet

    while (1) {
        snprintf(url, sizeof(url), API "/getUpdates?timeout=20&limit=1&offset=%lld", offset);
        esp_http_client_config_t cfg = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 30000,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        int len = 0;
        if (esp_http_client_open(c, 0) == ESP_OK) {
            esp_http_client_fetch_headers(c);
            len = esp_http_client_read_response(c, buf, sizeof(buf) - 1);
        }
        esp_http_client_cleanup(c);

        if (len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        buf[len] = '\0';

        char *id = strstr(buf, "\"update_id\":");
        if (id) {
            offset = strtoll(id + strlen("\"update_id\":"), NULL, 10) + 1;   // mark as handled
            if (strstr(buf, "\"text\":\"/photo\"")) {
                send_photo();
            }
        }
    }
}

void telegram_start(void)
{
    xTaskCreate(telegram_task, "telegram", 8192, NULL, 5, NULL);
}
