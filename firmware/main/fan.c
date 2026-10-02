// fan switch
// the load switch does the work, this only drives its enable pin

#include "esp_log.h"
#include "sdkconfig.h"

#include "fan.h"

static const char *tag = "fan";

#ifdef CONFIG_HG_FAN

#include "driver/gpio.h"

static int state;

void fan_init(void)
{
    gpio_reset_pin(CONFIG_HG_FAN_GPIO);
    gpio_set_direction(CONFIG_HG_FAN_GPIO, GPIO_MODE_OUTPUT);

    fan_set(CONFIG_HG_FAN_ON_AT_BOOT);

    ESP_LOGI(tag, "load switch enable on gpio%d, %s at boot",
             CONFIG_HG_FAN_GPIO, state ? "on" : "off");
}

void fan_set(int on)
{
    state = on ? 1 : 0;
    gpio_set_level(CONFIG_HG_FAN_GPIO, state);
}

int fan_on(void)
{
    return state;
}

#else

void fan_init(void)
{
    ESP_LOGI(tag, "not built in");
}

void fan_set(int on) { (void)on; }
int fan_on(void)     { return 0; }

#endif
