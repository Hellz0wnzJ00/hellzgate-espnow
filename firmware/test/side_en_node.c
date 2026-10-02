// the real espnow code built as a scanner, so the test can reach its statics

#include "../main/espnow.c"

esp_err_t n_init(void) { return espnow_init(); }
esp_err_t n_stage(const hg_record_t *r) { return espnow_stage(r); }
void n_service(void) { espnow_service(); }
int n_enrolled(void) { return have_master; }
uint32_t n_ring_used(void) { return ring_used; }
uint32_t n_dropped(void) { return ring_dropped; }
int n_retry_pending(void) { return retry_len > 0; }

uint32_t n_test_faked(void)
{
#if CONFIG_HG_TEST_FAKE_LOST_ACK_EVERY > 0
    return test_faked;
#else
    return 0;
#endif
}

// a scanner reboot, everything in ram gone and a new boot number
void n_reboot(void)
{
    tx_seq = 0;
    have_master = 0;
    tx_miss = 0;
    last_beat_us = last_hello_us = 0;
    ring_head = ring_tail = ring_used = ring_dropped = 0;
    retry_len = 0;
    boot_id = esp_random() | 1;
}
