// record tests, runs on the pc so no boards needed

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hg_record.h"

// 0xf4 is the known check value for this crc over the ascii digits
static void crc_matches_known_vector(void)
{
    const char *s = "123456789";
    assert(hg_crc8((const uint8_t *)s, 9) == 0xF4);
}

// offsets spelled out so they can be diffed against the spec table
static void layout_is_locked(void)
{
    assert(sizeof(hg_record_t) == 46);
    assert(offsetof(hg_record_t, bssid)   == 0);
    assert(offsetof(hg_record_t, rssi)    == 6);
    assert(offsetof(hg_record_t, channel) == 7);
    assert(offsetof(hg_record_t, band)    == 8);
    assert(offsetof(hg_record_t, type)    == 9);
    assert(offsetof(hg_record_t, node_id) == 10);
    assert(offsetof(hg_record_t, flags)   == 11);
    assert(offsetof(hg_record_t, ssid)    == 12);
    assert(offsetof(hg_record_t, crc8)    == 45);
}

static hg_record_t sample_ap(void)
{
    hg_record_t r;
    memset(&r, 0, sizeof r);

    uint8_t mac[6] = { 0xaa, 0xbb, 0xcc, 0x11, 0x22, 0x33 };
    memcpy(r.bssid, mac, sizeof mac);

    r.rssi    = -55;
    r.channel = 6;
    r.band    = HG_BAND_2G4;
    r.type    = HG_TYPE_AP;
    r.node_id = 1;
    strcpy(r.ssid, "test ap");

    return r;
}

static void seal_then_verify(void)
{
    hg_record_t r = sample_ap();
    hg_record_seal(&r);
    assert(hg_record_valid(&r));
}

// flip a bit through every payload byte, all of them have to fail
static void any_flipped_bit_is_caught(void)
{
    hg_record_t r = sample_ap();
    hg_record_seal(&r);

    uint8_t *raw = (uint8_t *)&r;
    for (size_t i = 0; i < 45; i++) {
        raw[i] ^= 0x01;
        assert(!hg_record_valid(&r));
        raw[i] ^= 0x01;
    }

    assert(hg_record_valid(&r));
}

// rssi is signed, easy to get wrong through a byte buffer
static void rssi_stays_negative(void)
{
    hg_record_t r = sample_ap();
    r.rssi = -90;
    hg_record_seal(&r);

    assert(r.rssi == -90);
    assert(hg_record_valid(&r));
}

// ble reuses the same 6 bytes for the device address, ssid stays empty
static void ble_record_seals_clean(void)
{
    hg_record_t r;
    memset(&r, 0, sizeof r);

    r.rssi    = -70;
    r.band    = HG_BAND_2G4;
    r.type    = HG_TYPE_BLE;
    r.node_id = 3;
    r.flags   = HG_FLAG_BLE_RANDOM;

    hg_record_seal(&r);
    assert(hg_record_valid(&r));
    assert(r.ssid[0] == '\0');
}

int main(void)
{
    crc_matches_known_vector();
    layout_is_locked();
    seal_then_verify();
    any_flipped_bit_is_caught();
    rssi_stays_negative();
    ble_record_seals_clean();

    printf("record tests ok\n");
    return 0;
}
