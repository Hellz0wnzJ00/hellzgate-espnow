// ESP-NOW transport: stage records on a scanner and collect them on the master.

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

#include "esp_err.h"
#include "hg_record.h"
#include "sdkconfig.h"

#define HG_MAX_NODES CONFIG_HG_MAX_NODES

typedef enum {
    HG_NODE_UNSEEN = 0,   // never enrolled
    HG_NODE_UP,           // heartbeats arriving
    HG_NODE_DOWN          // enrolled once, then went quiet
} hg_node_state;

// Per-scanner status. Reserved compatibility counters remain zero on ESP-NOW.
typedef struct {
    hg_node_state state;
    uint8_t  mac[6];
    uint16_t proto;          // what the node claims, a mismatch is not trusted
    uint32_t heartbeats;     // frames that were only a liveness ping
    uint32_t frames;         // records frames accepted
    uint32_t frames_lost;    // observed sequence gaps, not a complete loss count
    uint32_t records;        // records received from this node
    uint32_t seq;            // last records sequence we saw
    uint32_t boot_id;        // espnow, the scanner's boot number
    uint8_t  seq_known;      // espnow, seq holds a real value for this boot
    uint32_t downs;          // times it has been marked down
    uint32_t restarts;       // times it rebooted, seen from a new boot number
    uint32_t dupes;          // retries we had already taken, ignored not counted
    uint32_t inbox_full;     // records that reached the master and had no room
    uint32_t node_overflow;  // reserved; scanner overflow is local tx_stats only
    uint32_t wrong_id;       // reserved compatibility counter
    uint32_t reframes;       // reserved compatibility counter
    uint32_t last_seen_ms;   // ms not us, a 64 bit field here tore across tasks
} hg_node_info;

// Scanner transmission counters. MAC-layer success does not prove that the
// master queued or saved a record; compare master and storage counters too.
typedef struct {
    uint32_t records;   // records released after MAC-layer send success
    uint32_t frames;    // sends accepted by the radio, including control/retries
    uint32_t refused;   // the send call itself was rejected or never reported
    uint32_t retried;   // needed a second attempt
    uint32_t lost;      // still no acknowledgement after the retry
    uint32_t dropped;   // staged but the ring overflowed before they could go
    uint32_t queued;    // staged and still waiting right now
} hg_tx_stats;

typedef struct {
    const char *name;

    // Maximum records per ESP-NOW frame.
    uint8_t max_batch;

    esp_err_t (*init)(void);

    // node side. stage a record for the master to collect
    esp_err_t (*stage)(const hg_record_t *r);

    // node side. called often, hands staged records over and keeps liveness up
    void (*service)(void);

    // master side. drain what has arrived, returns how many were written
    uint32_t (*collect)(hg_record_t *out, uint32_t max);

    // True after a scanner discovers a master.
    int (*ready)(void);

    // True when scanning should yield for ESP-NOW service.
    int (*beat_due)(void);

    // node side. how the sending has been going
    void (*tx_stats)(hg_tx_stats *s);
} hg_transport;

const hg_transport *transport_espnow(void);

// master side view of the cluster, index is node_id
const hg_node_info *transport_node(uint8_t id);

// ESP-NOW enrollment, liveness scheduling and transmission counters.
int transport_enrolled(void);
int transport_beat_due(void);
void transport_tx_stats(hg_tx_stats *s);

#endif
