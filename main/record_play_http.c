#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "wav_encoder.h"
#include "wav_decoder.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"
#include "periph_button.h"
#include "periph_touch.h"
#include "periph_adc_button.h"
#include "esp_timer.h"

#include "cJSON.h"

#include "filter_resample.h"
#include "esp_http_client.h"
#include "input_key_service.h"
#include "audio_idf_version.h"
#include <stdbool.h>


#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_mac.h"


#include "mp3_decoder.h"
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "HTTP_AUDIO_EXAMPLE";
static esp_periph_set_handle_t set;

#define RECORD_RATE         48000
#define RECORD_CHANNEL      2
#define RECORD_BITS         16

#define RECIEVED_FILE_RATE       44100
#define RECIEVED_FILE_CHANNEL    1
#define RECIEVED_FILE_BITS       16

#define PLAYBACK_RATE       48000
#define PLAYBACK_CHANNEL    2
#define PLAYBACK_BITS       16

static EventGroupHandle_t EXIT_FLAG;
#define DEMO_EXIT_BIT (BIT0)

esp_err_t _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[32];
    static int total_write = 0;

    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        // set header
        ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST, lenght=%d", msg->buffer_len);
        esp_http_client_set_method(http, HTTP_METHOD_POST);
        char dat[10] = {0};
        snprintf(dat, sizeof(dat), "%d", RECORD_RATE);
        esp_http_client_set_header(http, "x-audio-sample-rates", dat);
        memset(dat, 0, sizeof(dat));
        snprintf(dat, sizeof(dat), "%d", RECORD_BITS);
        esp_http_client_set_header(http, "x-audio-bits", dat);
        memset(dat, 0, sizeof(dat));
        snprintf(dat, sizeof(dat), "%d", RECORD_CHANNEL);
        esp_http_client_set_header(http, "x-audio-channel", dat);
        total_write = 0;
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_ON_REQUEST) {
        // write data
        int wlen = sprintf(len_buf, "%x\r\n", msg->buffer_len);
        if (esp_http_client_write(http, len_buf, wlen) <= 0) {
            return ESP_FAIL;
        }
        if (esp_http_client_write(http, msg->buffer, msg->buffer_len) <= 0) {
            return ESP_FAIL;
        }
        if (esp_http_client_write(http, "\r\n", 2) <= 0) {
            return ESP_FAIL;
        }
        total_write += msg->buffer_len;
        printf("\033[A\33[2K\rTotal bytes written: %d\n", total_write);
        return msg->buffer_len;
    }
 
    if (msg->event_id == HTTP_STREAM_POST_REQUEST) {
        ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
        if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }
 
    if (msg->event_id == HTTP_STREAM_FINISH_REQUEST) {
        ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST");
        char *buf = calloc(1, 64);
        assert(buf);
        int read_len = esp_http_client_read(http, buf, 64);
        if (read_len <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        buf[read_len] = 0;
        ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)buf);
        free(buf);
        return ESP_OK;
    }
    return ESP_OK;
}

static char response_buffer[8];  // Buffer for storing "1" or "0"

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (evt->data_len < sizeof(response_buffer) - 1) {
            memcpy(response_buffer, evt->data, evt->data_len);
            response_buffer[evt->data_len] = '\0';  // Null-terminate
        }
    }
    return ESP_OK;
}
int64_t start_time;

bool wait_for_new_response() {
    const int max_attempts = 30;  
    const int delay_ms = 500;     
    start_time = esp_timer_get_time();
    for (int i = 0; i < max_attempts; i++) {
        esp_http_client_config_t config = {
            .url = "http://192.168.16.25:3000/status",
            .event_handler = _http_event_handler,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);
        esp_http_client_cleanup(client);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Attempt %d: Server Response: %s Latency: %lld ms", i + 1, response_buffer, ((esp_timer_get_time() - start_time) / 1000));

            if (strcmp(response_buffer, "1") == 0) {
                ESP_LOGI(TAG, "Received '1' - Exiting loop!");
                return true;
            }
        } else {
            ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // Wait for 500ms before next attempt
    }

    ESP_LOGI(TAG, "Timeout reached, no '1' received.");
    return false;
}

esp_err_t _http_stream_event_handle_read(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    static int total_bytes_received = 0;

    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        ESP_LOGI(TAG, "[ - ] Preparing to receive audio stream...");

        // Wait until newResponse flag is set to 1
        if (!wait_for_new_response()) {
            ESP_LOGE(TAG, "Failed to detect new response. Aborting...");
            return ESP_FAIL; 
        }

        ESP_LOGI(TAG, "[✔] Ready to receive audio stream!");
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_ON_REQUEST) {
        if (msg->buffer_len > 0) {
            // Process received audio chunk
            total_bytes_received += msg->buffer_len;
            printf("\033[A\33[2K\rTotal bytes received: %d\n", total_bytes_received);
        }
        return msg->buffer_len;
    }

    if (msg->event_id == HTTP_STREAM_POST_REQUEST) {
        ESP_LOGI(TAG, "[ - ] Finished receiving audio stream.");
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_REQUEST) {
        ESP_LOGI(TAG, "[ - ] Audio stream completed.");
        return ESP_OK;
    }

    return ESP_OK;
}


static audio_element_handle_t create_i2s_stream(int sample_rates, int bits, int channels, audio_stream_type_t type) {
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(CODEC_ADC_I2S_PORT, sample_rates, bits, type);
    i2s_cfg.out_rb_size = 64 * 1024; // Increase buffer size to handle network connectivity issues
    audio_element_handle_t i2s_stream = i2s_stream_init(&i2s_cfg);
    mem_assert(i2s_stream);
    audio_element_set_music_info(i2s_stream, sample_rates, channels, bits);
    return i2s_stream;
}

static audio_element_handle_t create_http_stream(audio_stream_type_t type) {
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.type = type;
    if(http_cfg.type==AUDIO_STREAM_WRITER)  http_cfg.event_handle = _http_stream_event_handle;
    else if(http_cfg.type==AUDIO_STREAM_READER)  http_cfg.event_handle = _http_stream_event_handle_read;
    http_cfg.user_data = NULL;
    http_cfg.enable_playlist_parser = false;  // Ensure it's handling a single stream
    http_cfg.task_stack = 8192;               // Increase stack size for reliability
    http_cfg.out_rb_size = 32 * 1024;

    audio_element_handle_t http_stream = http_stream_init(&http_cfg);
    mem_assert(http_stream);
    return http_stream;
}


static audio_element_handle_t create_filter(int source_rate, int source_channel, int dest_rate, int dest_channel, int mode)
{
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = source_rate;
    rsp_cfg.src_ch = source_channel;
    rsp_cfg.dest_rate = dest_rate;
    rsp_cfg.dest_ch = dest_channel;
    rsp_cfg.mode = mode;
    rsp_cfg.complexity = 0;
    return rsp_filter_init(&rsp_cfg);
}

static audio_element_handle_t create_wav_encoder() {
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    return wav_encoder_init(&wav_cfg);
}

static audio_element_handle_t create_mp3_decoder() {
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    return mp3_decoder_init(&mp3_cfg);
}


void record_playback_task() {
    audio_pipeline_handle_t pipeline_rec = NULL;
    audio_pipeline_handle_t pipeline_play = NULL;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();

    ESP_LOGI(TAG, "[1.1] Initialize recorder pipeline");
    pipeline_rec = audio_pipeline_init(&pipeline_cfg);
    pipeline_play = audio_pipeline_init(&pipeline_cfg);

    ESP_LOGI(TAG, "[1.2] Create audio elements for recorder pipeline");
    audio_element_handle_t i2s_reader_el = create_i2s_stream(RECORD_RATE, RECORD_BITS, RECORD_CHANNEL, AUDIO_STREAM_READER);
    audio_element_handle_t wav_encoder_el = create_wav_encoder();
    audio_element_handle_t http_writer_el = create_http_stream(AUDIO_STREAM_WRITER);

    ESP_LOGI(TAG, "[1.3] Register audio elements to recorder pipeline");
    audio_pipeline_register(pipeline_rec, i2s_reader_el, "i2s_reader");
    audio_pipeline_register(pipeline_rec, wav_encoder_el, "wav_encoder");
    audio_pipeline_register(pipeline_rec, http_writer_el, "http_writer");

    ESP_LOGI(TAG, "[2.2] Create audio elements for playback pipeline");
    audio_element_handle_t http_reader_el = create_http_stream(AUDIO_STREAM_READER);
    audio_element_handle_t wav_decoder_el = create_mp3_decoder();
    audio_element_handle_t filter_upsample_el = create_filter(RECIEVED_FILE_RATE, RECIEVED_FILE_CHANNEL, PLAYBACK_RATE, PLAYBACK_CHANNEL, RESAMPLE_DECODE_MODE);
    audio_element_handle_t i2s_writer_el = create_i2s_stream(PLAYBACK_RATE, PLAYBACK_BITS, PLAYBACK_CHANNEL, AUDIO_STREAM_WRITER);
    

    ESP_LOGI(TAG, "[2.3] Register audio elements to playback pipeline");
    audio_pipeline_register(pipeline_play, http_reader_el,      "http_reader");
    audio_pipeline_register(pipeline_play, wav_decoder_el,      "wav_decoder");
    audio_pipeline_register(pipeline_play, filter_upsample_el,  "filter_upsample");
    audio_pipeline_register(pipeline_play, i2s_writer_el,       "i2s_writer");
    

    ESP_LOGI(TAG, "[ 3 ] Set up event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    while (1) {
    audio_event_iface_msg_t msg;
    esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
        continue;
    }

    if ((msg.source_type == PERIPH_ID_TOUCH || msg.source_type == PERIPH_ID_BUTTON || msg.source_type == PERIPH_ID_ADC_BTN)) {
        if ((msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED || msg.cmd == PERIPH_ADC_BUTTON_PRESSED) && (int) msg.data == get_input_rec_id()) {
            // start_time = esp_timer_get_time(); // Start timing
            ESP_LOGE(TAG, "STOP Playback and START [Record]");

            // start_time = esp_timer_get_time(); // Start timing
            // audio_pipeline_pause(pipeline_play);
            // ESP_LOGI(TAG, "Latency for audio_pipeline_pause(pipeline_play): %lld ms", (esp_timer_get_time() - start_time) / 1000);
    
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_stop(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_stop(pipeline_play);: %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_wait_for_stop(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_wait_for_stop(pipeline_play);: %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_unlink(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_unlink(pipeline_play);: %lld ms", (esp_timer_get_time() - start_time) / 1000);

            
            ESP_LOGI(TAG, "Link audio elements to make recorder pipeline ready");
            const char *link_tag[3] = {"i2s_reader", "wav_encoder", "http_writer"};  // Use HTTP writer for streaming
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_relink(pipeline_rec, &link_tag[0], 3);
            ESP_LOGI(TAG, "Latency for audio_pipeline_link: %lld ms", (esp_timer_get_time() - start_time) / 1000);

            ESP_LOGI(TAG, "Setup HTTP server URI for recording");
            // start_time = esp_timer_get_time(); // Start timing
            audio_element_set_uri(http_writer_el, "http://192.168.16.25:3000/upload");
            // ESP_LOGI(TAG, "Latency for audio_element_set_uri (recording): %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_run(pipeline_rec);
            ESP_LOGI(TAG, "Latency for audio_pipeline_run: %lld ms", (esp_timer_get_time() - start_time) / 1000);
        } 
        else if((msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE) && (int) msg.data == get_input_rec_id()) {
            ESP_LOGI(TAG, "[REC] button Released. Closing Pipeline_rec.");
            start_time = esp_timer_get_time(); // Start timing
            audio_element_set_ringbuf_done(i2s_reader_el);
            ESP_LOGI(TAG, "Latency for audio_element_set_ringbuf_done: %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            // start_time = esp_timer_get_time(); // Start timing
            // audio_pipeline_pause(pipeline_rec);
            // ESP_LOGI(TAG, "Latency for audio_pipeline_pause(pipeline_play): %lld ms", (esp_timer_get_time() - start_time) / 1000);
    
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_stop(pipeline_rec);
            ESP_LOGI(TAG, "Latency for audio_pipeline_stop(pipeline_play);: %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_wait_for_stop_with_ticks(pipeline_rec, pdMS_TO_TICKS(500));
            ESP_LOGI(TAG, "Latency for audio_pipeline_wait_for_stop(pipeline_play);: %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_unlink(pipeline_rec);
            ESP_LOGI(TAG, "Latency for audio_pipeline_unlink(pipeline_play);: %lld ms", (esp_timer_get_time() - start_time) / 1000);

            ESP_LOGI(TAG, "Link audio elements to make playback pipeline ready");
            const char *link_tag[4] = {"http_reader", "wav_decoder", "filter_upsample", "i2s_writer"};
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_relink(pipeline_play, &link_tag[0], 4);
            ESP_LOGI(TAG, "Latency for audio_pipeline_link (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);

            ESP_LOGI(TAG, "Setup HTTP server URI for playback");
            start_time = esp_timer_get_time(); // Start timing
            audio_element_set_uri(http_reader_el, "http://192.168.16.25:3000/stream");
            ESP_LOGI(TAG, "Latency for audio_element_set_uri (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_run(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_run (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);
        }
        else if ((msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED || msg.cmd == PERIPH_ADC_BUTTON_PRESSED) && (int) msg.data == get_input_play_id()) {
            ESP_LOGI(TAG, "STOP [Record] and START Playback");
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_stop(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_stop (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_wait_for_stop(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_wait_for_stop (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            ESP_LOGI(TAG, "Link audio elements to make playback pipeline ready");
            const char *link_tag[4] = {"http_reader", "wav_decoder", "filter_upsample", "i2s_writer"};
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_link(pipeline_play, &link_tag[0], 4);
            ESP_LOGI(TAG, "Latency for audio_pipeline_link (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);

            ESP_LOGI(TAG, "Setup HTTP server URI for playback");
            start_time = esp_timer_get_time(); // Start timing
            audio_element_set_uri(http_reader_el, "http://192.168.16.25:3000/stream");
            ESP_LOGI(TAG, "Latency for audio_element_set_uri (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);
            
            start_time = esp_timer_get_time(); // Start timing
            audio_pipeline_run(pipeline_play);
            ESP_LOGI(TAG, "Latency for audio_pipeline_run (playback): %lld ms", (esp_timer_get_time() - start_time) / 1000);
        }
    }

}

    ESP_LOGI(TAG, "[ 4 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline_rec);
    audio_pipeline_wait_for_stop(pipeline_rec);
    audio_pipeline_terminate(pipeline_rec);
    audio_pipeline_stop(pipeline_play);
    audio_pipeline_wait_for_stop(pipeline_play);
    audio_pipeline_terminate(pipeline_play);

    audio_pipeline_unregister(pipeline_play, http_reader_el);
    audio_pipeline_unregister(pipeline_play, wav_decoder_el);
    audio_pipeline_unregister(pipeline_play, i2s_writer_el);
    audio_pipeline_unregister(pipeline_play, filter_upsample_el);

    audio_pipeline_unregister(pipeline_rec, i2s_reader_el);
    audio_pipeline_unregister(pipeline_rec, wav_encoder_el);
    audio_pipeline_unregister(pipeline_rec, http_writer_el);

    audio_pipeline_remove_listener(pipeline_rec);
    audio_pipeline_remove_listener(pipeline_play);

    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);
    audio_event_iface_destroy(evt);

    audio_element_deinit(http_reader_el);
    audio_element_deinit(wav_decoder_el);
    audio_element_deinit(i2s_writer_el);
    audio_element_deinit(filter_upsample_el);
    audio_element_deinit(i2s_reader_el);
    audio_element_deinit(wav_encoder_el);
    audio_element_deinit(http_writer_el);
}

static const char *WIFI_TAG = "WIFI_PROVISIONING"; // Renamed TAG for Wi-Fi provisioning
static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

static void smartconfig_example_task(void * parm);

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(WIFI_TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(WIFI_TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(WIFI_TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

#ifdef CONFIG_SET_MAC_ADDRESS_OF_TARGET_AP
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            ESP_LOGI(WIFI_TAG, "Set MAC address of target AP: "MACSTR" ", MAC2STR(evt->bssid));
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }
#endif

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));

        ESP_LOGI(WIFI_TAG, "SSID:%s", ssid);
        ESP_LOGI(WIFI_TAG, "PASSWORD:%s", password);
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(WIFI_TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    // Now try to get saved config
    wifi_config_t wifi_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));

    // Attempt to connect using stored credentials

    ESP_LOGI(WIFI_TAG, "Saved Credentials in NVS, SSID: %s  PASSWORD: %s", wifi_config.sta.ssid, wifi_config.sta.password);
    int retry_count = 0;
    while (retry_count < 10) {
        ESP_LOGI(WIFI_TAG, "Attempting to connect to WiFi, try %d", retry_count + 1);
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
        if (bits & CONNECTED_BIT) {
            ESP_LOGI(WIFI_TAG, "Successfully connected to WiFi");
            return;
        }
        retry_count++;
    }

    ESP_LOGI(WIFI_TAG, "Failed to connect after 10 attempts, starting SmartConfig");
    xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;

    ESP_LOGI("SmartConfig", "Stopping previous SmartConfig session (if any)");
    esp_smartconfig_stop();

    ESP_LOGI("SmartConfig", "Setting SmartConfig type...");
    esp_err_t err = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
    if (err != ESP_OK) {
        ESP_LOGE("SmartConfig", "Failed to set SmartConfig type: %d", err);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI("SmartConfig", "Starting SmartConfig...");
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    err = esp_smartconfig_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE("SmartConfig", "Failed to start SmartConfig: %d", err);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & CONNECTED_BIT) {
            ESP_LOGI("SmartConfig", "WiFi Connected to AP");
        }
        if (uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI("SmartConfig", "SmartConfig completed");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

// static void try_connecting_with_saved_credentials(void)
// {
//     // Initialize WiFi first
//     ESP_ERROR_CHECK(esp_netif_init());
//     s_wifi_event_group = xEventGroupCreate();
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
//     assert(sta_netif);

//     // Initialize WiFi with default config
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     // Register event handlers
//     ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

//     // Set WiFi mode to station
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

//     // Now try to get saved config
//     wifi_config_t wifi_config;
//     ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));

//     if (strlen((char *)wifi_config.sta.ssid) == 0) {
//         ESP_LOGI(TAG, "No saved Wi-Fi credentials found. Starting SmartConfig...");
//         ESP_ERROR_CHECK(esp_wifi_start());
//         xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
//         return;
//     }

//     ESP_LOGI(TAG, "Attempting to connect using saved credentials (SSID: %s)...", wifi_config.sta.ssid);
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());
//     ESP_ERROR_CHECK(esp_wifi_connect());

//     int attempts = 0;
//     while (attempts < 5) {
//         EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, 
//                                              CONNECTED_BIT, 
//                                              pdFALSE, 
//                                              pdFALSE, 
//                                              pdMS_TO_TICKS(5000));

//         if (bits & CONNECTED_BIT) {
//             ESP_LOGI(TAG, "Connected to WiFi!");
//             return;
//         } else {
//             ESP_LOGW(TAG, "WiFi connection attempt %d failed, retrying...", attempts + 1);
//             esp_wifi_connect(); // Retry connection
//         }

//         attempts++;
//     }

//     ESP_LOGE(TAG, "Failed to connect after 10 attempts. Starting SmartConfig...");
//     // Ensure WiFi is disconnected before starting SmartConfig
//     ESP_ERROR_CHECK(esp_wifi_disconnect());
//     ESP_ERROR_CHECK(esp_wifi_stop());

//     vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay before starting SmartConfig

//     xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
// }

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set(WIFI_TAG, ESP_LOG_INFO);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    // try_connecting_with_saved_credentials();
    
    initialise_wifi();
    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    set = esp_periph_set_init(&periph_cfg);
 
    // periph_wifi_cfg_t wifi_cfg = { 
    //     .wifi_config.sta.ssid = {SSID},
    //     .wifi_config.sta.password = {PASSWORD},
    // };

    // Initialize Wi-Fi peripheral
    // periph_wifi_cfg_t wifi_cfg = {
        // .wifi_config.sta.ssid = "JioPhone Next",
        // .wifi_config.sta.password = "anisharma",
        // .wifi_config.sta.ssid = "motorola edge 50 pro_3243",
        // .wifi_config.sta.password = "123456789",
        // .wifi_config.sta.ssid = "OnePlus Nord CE 2 Lite 5G",
        // .wifi_config.sta.password = "123456789",
        // .wifi_config.sta.ssid = "Rastogi_ji",
        // .wifi_config.sta.password = "asaan hai",
        // .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        // .wifi_config.sta.password = "CONFIG_WIFI_PASSWORD",
    // };

    // esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    // esp_periph_start(set, wifi_handle);
    // periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);


    // Initialize Button peripheral
    audio_board_key_init(set);

    // Setup audio codec
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    int player_volume = 100;
    audio_hal_get_volume(board_handle->audio_hal, &player_volume); 
    // Create event group for exit
    EXIT_FLAG = xEventGroupCreate();

    // Start record/playback task
    record_playback_task();
    esp_periph_set_destroy(set);
}