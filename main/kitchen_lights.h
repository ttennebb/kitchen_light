#ifndef _trKITCHEN_LIGHTS_H_
#define _trKITCHEN_LIGHTS_H_

#ifdef __cplusplus
 extern "C" {
#endif

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */
#if CONFIG_STATION_MODE
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

#define ESPNOW_QUEUE_SIZE           6

//
// structures
//
typedef struct
{
  union
  {
    struct
    {
    // {cmd b g r}  
      uint32_t cmd    :8;
      uint32_t blue   :8;
      uint32_t green  :8;
      uint32_t red    :8;
    };
    uint32_t val;
  };
}tr_data_t;
//
typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    tr_data_t data;
    int data_len;
} tr_espnow_data_t;
//
typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} tr_espnow_status_t;
//
typedef enum
{
  RP    = 0x1051,
  RM    = 0x1050,
  GP    = 0x1061,
  GM    = 0x1060,
  BP    = 0x1071,
  BM    = 0x1070,
  AP    = 0x1041,
  AM    = 0x1040,
  AO    = 0x1020,
  AF    = 0x1030
}_OPERATIONS_t;


#if 1
static void led_color_xform(LED_STRUCT *led, COLOR_t color, led_xform_t xform);
static void led_rgb_xform(LED_STRUCT *led, led_xform_t xform);
static void led_rgb_to_strip_rgb(LED_STRUCT *led, led_strip_t *strip);
static void led_pix_rgb_to_strip_rgb(LED_STRUCT *led, pixel_t *pix_rbg, led_strip_t *strip);
#endif
static esp_err_t tr_espnow_init(void);
static void tr_wifi_init(void);
static void tr_espnow_deinit(void);
static void tr_espnow_send_task(tr_espnow_data_t *send);
static void tr_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);
static void tr_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);

#ifdef __cplusplus
}
#endif
#endif