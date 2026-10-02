/*
 * hg_record.h  --  HellzGate C5 observation wire record
 *
 *   Project : HellzGate C5
 *   Owner   : Sean Clossey   (work product / IP: Sean Clossey)
 *   Status  : LOCKED         (record format is frozen; do not change silently)
 *   Version : HG_PROTOCOL_VERSION below
 *   Date    : 2026-07-08
 *
 *   Packed ESP-NOW observation record; layout and CRC are unchanged.
 */

#ifndef HG_RECORD_H
#define HG_RECORD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump on any change to the record layout or CRC parameters. */
// Record layout version; the ESP-NOW envelope has its own version.
#define HG_PROTOCOL_VERSION   0x0102u

/* ---- enums (values are part of the locked wire format) ------------------ */

enum hg_band {
    HG_BAND_2G4 = 0,   /* 2.4 GHz */
    HG_BAND_5G  = 1    /* 5 GHz   */
};

enum hg_type {
    HG_TYPE_AP     = 0,
    HG_TYPE_BLE    = 1,
    HG_TYPE_CLIENT = 2
};

/* flags bitfield */
#define HG_FLAG_BLE_RANDOM      (1u << 0)   /* BLE random address        */
#define HG_FLAG_BLE_RESOLVABLE  (1u << 1)   /* BLE resolvable private    */

/* ---- the record --------------------------------------------------------- */
/*
 * 46 bytes, packed, little-endian. Byte offsets are fixed and authoritative.
 *
 *   off  size  field
 *   ---  ----  --------------------------------------------------------------
 *    0     6   bssid[6]   MAC / BLE device address, stored as captured
 *    6     1   rssi       int8, dBm
 *    7     1   channel
 *    8     1   band       enum hg_band
 *    9     1   type       enum hg_type
 *   10     1   node_id    0..HG_MAX_NODES-1 (source scanner node)
 *   11     1   flags      HG_FLAG_*
 *   12    33   ssid[33]   NUL-padded; up to 32 bytes of SSID + slack
 *   45     1   crc8       CRC-8, poly 0x07, init 0x00, over bytes 0..44
 *   ---  ----
 *   total 46
 */
typedef struct __attribute__((packed)) {
    uint8_t bssid[6];   /*  0 */
    int8_t  rssi;       /*  6 */
    uint8_t channel;    /*  7 */
    uint8_t band;       /*  8 */
    uint8_t type;       /*  9 */
    uint8_t node_id;    /* 10 */
    uint8_t flags;      /* 11 */
    char    ssid[33];   /* 12 */
    uint8_t crc8;       /* 45 */
} hg_record_t;

/* Fail the build if padding ever creeps in. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(hg_record_t) == 46, "hg_record_t must be exactly 46 bytes");
_Static_assert(offsetof(hg_record_t, crc8) == 45, "crc8 must sit at byte 45");
#endif

/* ---- CRC-8 (poly 0x07, init 0x00, no reflect, no final xor) ------------- */

static inline uint8_t hg_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                : (uint8_t)(crc << 1);
    }
    return crc;
}

/* Compute the CRC over the 45 payload bytes (0..44). */
static inline uint8_t hg_record_crc(const hg_record_t *r)
{
    return hg_crc8((const uint8_t *)r, offsetof(hg_record_t, crc8));
}

/* Stamp the CRC field in place. */
static inline void hg_record_seal(hg_record_t *r)
{
    r->crc8 = hg_record_crc(r);
}

/* Verify a received record. Returns non-zero if the CRC matches. */
static inline int hg_record_valid(const hg_record_t *r)
{
    return r->crc8 == hg_record_crc(r);
}

#ifdef __cplusplus
}
#endif

#endif /* HG_RECORD_H */
