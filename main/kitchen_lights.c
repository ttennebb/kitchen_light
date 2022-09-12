
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_now.h"

//#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "driver/rmt.h"
#include "led_strip.h"
#include "addressable_led.h"
#include "kitchen_lights.h"


#define RMT_TX_CHANNEL RMT_CHANNEL_0

#define CHASE_SPEED_MS (16)
//
// tr start
// defines
#if 0
#define MAX_BRIGHTNESS  255
#define MIN_BRIGHTNESS  0
#define HALF_BRIGHTNESS 127
#endif
#define PLUS            0x1
#define MINUS           0x2
#define ENCODER_PULSE   false
#define SWTCH           true
typedef enum
{
  op_ALL      = 0,
  op_RED,
  op_GREEN,
  op_BLUE,
  op_OFF,    
  op_ON,
  op_STATUS,
  op_SNOOZE,
  op_WAKE_UP,
  op_ADJUST,
  op_END      = 90
  
} OPERATION_t;

typedef struct
{
  pixel_t   *pixel;
  bool      onoff;
}LIGHT_t;

// globals
pixel_t pixel_ary[NUMBER_LEDS];
LED_STRUCT led;
led_strip_t *strip;
//
static const char *TAG = "_KITCHEN_LIGHT_";
LIGHT_t kit_light;
//
static xQueueHandle tr_espnow_send_queue  = NULL;
static xQueueHandle tr_espnow_recv_queue  = NULL;
static TaskHandle_t tr_recv_h             = NULL;

static uint8_t tr_remote_mac[ESP_NOW_ETH_ALEN] = { 0x9c, 0x9c, 0x1f, 0x47, 0x86, 0xba };
// tr end
//
//
//
//
//
/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
//
// note: 
// when this callback fires, the mac_addr and status are passed in 
// i.e. some transmit buffer emptied and triggered some interrupt
//
static void tr_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    tr_espnow_status_t send_status;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    // should already know mac_addr, but.....
    memcpy(send_status.mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_status.status = status;
    if (xQueueSend(tr_espnow_send_queue, &send_status, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}
//
// note: 
// when this callback fires, the mac_addr, data and data_len are passed in 
// i.e. some receive buffer filled and triggered some interrupt
//      now deal with it
//
static void tr_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
  tr_espnow_data_t incoming;
  
  // test to be sure there are params to work on...
  if (mac_addr == NULL || data == NULL || len <= 0) 
  {
    ESP_LOGE(TAG, "Receive cb arg error");
    return;
  }
  // copy the mac
  memcpy(incoming.mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
  
  memcpy(&incoming.data.val, data, len);
  incoming.data_len = len;
  // call the receive task to process the incoming data
  if (xQueueSend(tr_espnow_recv_queue, &incoming, portMAX_DELAY) != pdTRUE) 
  {
    ESP_LOGW(TAG, "Send receive queue fail");
  }  
  
}
//
//
//
static void tr_espnow_send_task(tr_espnow_data_t *send)
{
  tr_espnow_status_t send_status;
//printf("sent val : 0x%08x\n", send->data.val);
  esp_err_t send_err = esp_now_send(send->mac_addr, (uint8_t *)&send->data.val, send->data_len);
  if (send_err != ESP_OK) 
  {
    ESP_LOGI(TAG, "data : 0x%x", send->data.val);
    ESP_LOGI(TAG, "len : %d", send->data_len);
    ESP_LOGE(TAG, "Send error : %s", esp_err_to_name(send_err));
    tr_espnow_deinit();
  }
  //
  // wait for send resp
  //
  if(xQueueReceive(tr_espnow_send_queue, &send_status, 1000 / portTICK_RATE_MS) != pdPASS)
  {
    // fail
    ESP_LOGE(TAG, "xQueueReceive fail");
  }
}
//
//
//
static esp_err_t tr_espnow_init(void)
{
  tr_espnow_data_t incoming;

  tr_espnow_send_queue = xQueueCreate(1, sizeof(tr_espnow_status_t));
  if (tr_espnow_send_queue == NULL) {
      ESP_LOGE(TAG, "Create mutex fail");
      return ESP_FAIL;
  }
  tr_espnow_recv_queue = xQueueCreate(1, sizeof(tr_espnow_data_t));
  if (tr_espnow_recv_queue == NULL) {
      ESP_LOGE(TAG, "Create mutex fail");
      return ESP_FAIL;
  }

  /* Initialize ESPNOW and register sending and receiving callback function. */
  ESP_ERROR_CHECK( esp_now_init() );
  ESP_ERROR_CHECK( esp_now_register_send_cb(tr_espnow_send_cb) );
  ESP_ERROR_CHECK( esp_now_register_recv_cb(tr_espnow_recv_cb) );

  /* Set primary master key. */
  ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

  /* Add broadcast peer information to peer list. */
  esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
  if (peer == NULL) {
    ESP_LOGE(TAG, "Malloc peer information fail");
    vSemaphoreDelete(tr_espnow_send_queue);
    vSemaphoreDelete(tr_espnow_recv_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }
  memset(peer, 0, sizeof(esp_now_peer_info_t));
  peer->channel = CONFIG_ESPNOW_CHANNEL;
  peer->ifidx = ESPNOW_WIFI_IF;
  peer->encrypt = false;
  //
  // set the initial peer mac 
  //
  memcpy(peer->peer_addr, tr_remote_mac, ESP_NOW_ETH_ALEN);
  ESP_ERROR_CHECK( esp_now_add_peer(peer) );
  free(peer);

  return ESP_OK;
}
//
//
//
/* WiFi should start before using ESPNOW */
static void tr_wifi_init(void)
{
    //tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK( esp_wifi_start());

    /* In order to simplify example, channel is set after WiFi started.
     * This is not necessary in real application if the two devices have
     * been already on the same channel.
     */
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) );
}
//
//
//
static void tr_espnow_deinit(void)
{
  vSemaphoreDelete(tr_espnow_send_queue);
  vSemaphoreDelete(tr_espnow_recv_queue);
  esp_now_deinit();
}
//
//
//
//static void all_(LED_STRUCT led, led_xform_t xform)
//{
//}
//
//
//
static void led_rgb_to_strip_rgb(LED_STRUCT *led, led_strip_t *strip)
{
  pixel_t pix_rgb;
  
  led_rgb_get(led, 0, &pix_rgb);
  for (int j = 0; j < NUMBER_LEDS; j++) 
  {
    ESP_ERROR_CHECK(strip->set_pixel(strip, j, pix_rgb.red, pix_rgb.green, pix_rgb.blue));
      
  }
}
#if 0
//
//
//
static void led_pix_rgb_to_strip_rgb(LED_STRUCT *led, pixel_t *pix_rgb, led_strip_t *strip)
{
  
  for (int j = 0; j < NUMBER_LEDS; j++) 
  {
    
    strip->set_pixel(strip, j, pix_rgb->red, pix_rgb->green, pix_rgb->blue);
      
  }
}
//
//
//
int led_pixel_to_rgb(LED_STRUCT *led, COLOR_t *rgb)
{
  pixel_t pixel_rgb;
  led_rgb_get(led, 0, &pixel_rgb);
  rgb[2] = pixel_rgb.red;
  rgb[1] = pixel_rgb.green;
  rgb[0] = pixel_rgb.blue;
  
  return 0;
}
#endif
//
//
//
int led_pixel_to_tr_data(LED_STRUCT *led, tr_data_t *tr_rgb)
{
  led_rgb_get(led, 0, (pixel_t *)&tr_rgb); 
  return 0;
}
//
// inserts a pixel into the array
// (r,g,b) -> ledX(r,g,b)
//
int led_pixel_insert(LED_STRUCT *led, int led_pixel, pixel_t *rgb)
{
  if(led_pixel >= NUMBER_LEDS)
    return -1;
  if(led_pixel < 0)
    return -2;

  led->pixel_ary[led_pixel].val = rgb->val;
  return 0;

}
//
//
//
//pixel_t all_on = {0xff, 0xff, 0xff};
//pixel_t all_off = {0x00, 0x00, 0x00};
//
//
//
static void tr_server_task(void *arg)
{
  tr_espnow_data_t *update = (tr_espnow_data_t *)arg;
  OPERATION_t current_op_index;
  pixel_t pixel_rgb;
  pixel_t *rgb;
  uint8_t active = op_OFF;
  uint32_t color_adj;
  
  pixel_rgb.val = 0x00000000;

  ESP_LOGI("BIRN", "xQ");
  while (xQueueReceive(tr_espnow_recv_queue, update, portMAX_DELAY) == pdTRUE) 
  {
    //if(xQueueReceive(tr_espnow_recv_queue, update, portMAX_DELAY) != pdTRUE)
    //  ESP_LOGW(TAG, "Receive queue failed");


    printf("update data : 0x%08x\n", update->data.val);    
    switch(update->data.cmd)
    {
      case op_OFF:
        // the lights are on
        // turn lights off 
        // save the current rgb for later
        led_rgb_get(&led, 0, &pixel_rgb);
        update->data.val = 0x00000000;
        active = op_OFF;
        // adjust local params as req'd
        // implement the color changes
        //for(int pixel=0; pixel<NUMBER_LEDS; pixel++)
        //{
        //  led_pixel_insert(&led, pixel, (pixel_t *)&update->data);
        //}
        led.pixel_ary[0].val = update->data.val;
        led_rgb_to_strip_rgb(&led, strip);
        ESP_ERROR_CHECK(strip->refresh(strip, 100));
      break;
      case op_ON:
        // turn lights on
        // retrieve and set pixels
        // as per last off command
        update->data.val = pixel_rgb.val; // (needs conversion);
        active = op_ON;
        // adjust local params as req'd
        // implement the color changes
        //for(int pixel=0; pixel<NUMBER_LEDS; pixel++)
        //{
        //  led_pixel_insert(&led, pixel, (pixel_t *)&update->data);
        //}
        led.pixel_ary[0].val = update->data.val;
        led_rgb_to_strip_rgb(&led, strip);
        ESP_ERROR_CHECK(strip->refresh(strip, 100));
      break;
      case op_ADJUST:
        // color_adj is the changes sent over from the remote
        // first save the update data as we will
        // be changing that value very soon
        color_adj = update->data.val;
        uint8_t *color = (uint8_t *)&color_adj;
        // modify rgb
        // get the pixel rgb from the led structure
        // into the update param
        led_rgb_get(&led, 0, (pixel_t *)&update->data);
        uint8_t *adj = (uint8_t *)&update->data.val;
        //tr_data_t *adj = &update->data;
        // blue green red 1 2 3
        // color = data.val
        for(int i=1; i<=3; i++)
        {
          //printf("color[%d]: %d\n", i, color[i]);
          switch(color[i])
          {
            case PLUS:
              adj[i]++;
            break;
            case MINUS:
              adj[i]--;
            break;
            default:
            break;
          }
        }
        // adjustments are made
        //
        // implement the color changes
        //for(int pixel=0; pixel<NUMBER_LEDS; pixel++)
        //{
        //  led_pixel_insert(&led, pixel, (pixel_t *)&update->data);
        //}
        led.pixel_ary[0].val = update->data.val;
        led_rgb_to_strip_rgb(&led, strip);
        ESP_ERROR_CHECK(strip->refresh(strip, 100));
        // adjust local params as req'd
        // send back updated params
      break;
      case op_STATUS:
      default:
        // send back params
        led_rgb_get(&led, 0, (pixel_t *)&update->data);
        update->data.cmd = active;
      break;
    }
    // send back active+rgb
    update->data.cmd = active;
    memcpy(update->mac_addr, tr_remote_mac, ESP_NOW_ETH_ALEN);
    tr_espnow_send_task(update);
  }// while
  tr_espnow_deinit();
}
//
//
//
void app_main(void)
{
  tr_espnow_data_t tr_comm;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK( nvs_flash_erase() );
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK( ret );

  //
  // set up and start the led portion of the program
  //
  // led is a global
  led.pixels = NUMBER_LEDS;
  led.pixel_ary = pixel_ary;
  //
  // note :
  // rgb_fill[] => { r, g, b }
  //
  uint8_t rgb_fill[] = {0x00, 0x00, 0x00};
 
  for(int pixel=0; pixel<NUMBER_LEDS; pixel++)
  {
    led_rgb_insert(&led, pixel, rgb_fill);
  }
  
  rmt_config_t config = RMT_DEFAULT_CONFIG_TX(35, RMT_TX_CHANNEL);
  // set counter clock to 40MHz
  config.clk_div = 2;

  ESP_ERROR_CHECK(rmt_config(&config));
  ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
  // install ws2812 driver
  led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(NUMBER_LEDS, (led_strip_dev_t)config.channel);
  strip = led_strip_new_rmt_ws2812(&strip_config);
  if (!strip) {
      ESP_LOGE(TAG, "install WS2812 driver failed");
  }
  // Clear LED strip (turn off all LEDs)
  //ESP_ERROR_CHECK(strip->clear(strip, 100));
  uint32_t free_running_counter = 0;
  //
  // 
  led_rgb_to_strip_rgb(&led, strip);
  ESP_ERROR_CHECK(strip->refresh(strip, 100));

    /* Start the server for the first time */
  tr_wifi_init();
  tr_espnow_init();
  xTaskCreate(tr_server_task, "kitchen_light_server", 4096, (void*)&tr_comm, 5, NULL);
  
}
