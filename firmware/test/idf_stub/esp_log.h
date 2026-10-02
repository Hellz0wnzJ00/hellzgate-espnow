#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdio.h>

// quiet unless HG_TEST_VERBOSE is set, the test output is the checks
extern int hg_test_verbose;

#define HG_LOG(l, tag, fmt, ...) \
    do { if (hg_test_verbose) printf("%s (%s) " fmt "\n", l, tag, ##__VA_ARGS__); } while (0)

#define ESP_LOGE(tag, fmt, ...) HG_LOG("E", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) HG_LOG("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) HG_LOG("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) HG_LOG("D", tag, fmt, ##__VA_ARGS__)

#define ESP_LOG_NONE 0
static inline void esp_log_level_set(const char *tag, int level) { (void)tag; (void)level; }

#endif
