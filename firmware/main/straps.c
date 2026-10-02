#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "straps.h"
#include "transport.h"

static const char *tag = "straps";

// xiao d7 to d10, lowest address bit first
static const gpio_num_t addr_pins[4] = {
    GPIO_NUM_12,   // d7,  addr0
    GPIO_NUM_8,    // d8,  addr1
    GPIO_NUM_9,    // d9,  addr2
    GPIO_NUM_10,   // d10, addr3
};

static uint8_t raw_value;
static uint8_t floating_mask;

static int read_with_pull(gpio_num_t pin, int pull_up)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_up ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    // the internal pull is weak and the pin has board capacitance on it, so
    // give it a moment to settle before believing the level
    vTaskDelay(pdMS_TO_TICKS(2));
    return gpio_get_level(pin);
}

uint8_t straps_read_slot(void)
{
    uint8_t v = 0;

    floating_mask = 0;

    for (int i = 0; i < 4; i++) {
        // a pin the slot hardware ties to 3v3 or ground reads the same whichever
        // way we pull it. a pin with nothing on it follows the pull. checking
        // both ways is the only way to tell a real strap from an open pin. a
        // pull-down alone is insufficient: an unconnected GPIO12 was observed
        // reading high and producing an apparently valid slot 1
        int up = read_with_pull(addr_pins[i], 1);
        int down = read_with_pull(addr_pins[i], 0);

        if (up != down) {
            floating_mask |= 1u << i;
            continue;
        }

        if (up)
            v |= 1u << i;
    }

    raw_value = v;

    if (floating_mask != 0) {
        ESP_LOGE(tag, "address pins 0x%x are not connected, board is not seated",
                 floating_mask);
        return 0;
    }

    // four pins cover 0 to 15 and 0 is spoken for as the not seated marker, so
    // 15 is the ceiling however big the cluster is set to
    uint8_t top = HG_MAX_NODES < 15 ? HG_MAX_NODES : 15;

    if (v < 1 || v > top) {
        ESP_LOGE(tag, "slot straps read %u, outside 1 to %u", v, top);
        return 0;
    }

    ESP_LOGI(tag, "slot %u, node id %u", v, v - 1);
    return v;
}

uint8_t straps_raw(void)
{
    return raw_value;
}

uint8_t straps_floating(void)
{
    return floating_mask;
}
