#ifndef ESP_NOW_H
#define ESP_NOW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"

typedef enum { ESP_NOW_SEND_SUCCESS = 0, ESP_NOW_SEND_FAIL } esp_now_send_status_t;

typedef struct { int unused; } wifi_tx_info_t;

typedef struct {
    uint8_t *src_addr;
    uint8_t *des_addr;
} esp_now_recv_info_t;

typedef struct {
    uint8_t peer_addr[6];
    uint8_t channel;
    int ifidx;
    bool encrypt;
} esp_now_peer_info_t;

typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *, const uint8_t *, int);
typedef void (*esp_now_send_cb_t)(const wifi_tx_info_t *, esp_now_send_status_t);

// the fake radio in test_espnow.c answers all of these
esp_err_t esp_now_init(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len);
bool esp_now_is_peer_exist(const uint8_t *mac);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *p);

#define ESP_ERROR_CHECK(x) do { esp_err_t e_ = (x); (void)e_; } while (0)

#endif
