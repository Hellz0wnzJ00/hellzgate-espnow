#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "slotid.h"
#include "straps.h"
#include "transport.h"

// the master has no slot and menuconfig does not define HG_NODE_ID for it, so
// none of this is built into the master image
#ifdef CONFIG_HG_ROLE_NODE

static const char *tag = "slotid";

// namespace and key the provisioning tool writes
#define SLOT_NS  "hellzgate"
#define SLOT_KEY "node_id"

static uint8_t node_id;
static int from_nvs;

// Only a verified strap, NVS or explicitly trusted build ID permits enrollment.
static int known;

void slotid_init(void)
{
    node_id = CONFIG_HG_NODE_ID;
    from_nvs = 0;

    // a per slot build sets HG_NODE_ID on purpose and gives every slot its own
    // image, so the built in id is a real answer there. off by default because
    // one image flashed to several boards makes it the same answer on all of
    // them, creating duplicate scanner IDs
#ifdef CONFIG_HG_SLOT_TRUST_BUILT_IN
    known = 1;
#else
    known = 0;
#endif

#ifdef CONFIG_HG_SLOT_STRAPS
    // Strap builds require a valid hardware slot and do not fall back to NVS.
    uint8_t slot = straps_read_slot();
    if (slot >= 1 && slot <= HG_MAX_NODES) {
        node_id = slot - 1;
        known = 1;
        return;
    }

    known = 0;
    ESP_LOGE(tag, "strap read failed, this board will not enroll");
    ESP_LOGE(tag, "check seating. a strap build does not fall back to nvs on purpose");
    return;
#endif

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGW(tag, "nvs unavailable, %s, using built in id %u",
                 esp_err_to_name(err), node_id);
        return;
    }

    nvs_handle_t h;
    if (nvs_open(SLOT_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(tag, "no id in nvs, falling back to built in id %u, slot %u. every board flashed with this image shares it",
                 node_id, node_id + 1);
        return;
    }

    uint8_t stored = 0;
    err = nvs_get_u8(h, SLOT_KEY, &stored);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(tag, "no id in nvs, falling back to built in id %u, slot %u. every board flashed with this image shares it",
                 node_id, node_id + 1);
        return;
    }

    // a provisioned id outside the cluster would have every frame rejected by
    // the master, so refuse it here where it is visible rather than there
    if (stored >= HG_MAX_NODES) {
        ESP_LOGE(tag, "nvs id %u is outside the cluster of %d, using built in id %u",
                 stored, HG_MAX_NODES, node_id);
        return;
    }

    node_id = stored;
    from_nvs = 1;
    known = 1;
    ESP_LOGI(tag, "id %u from nvs, slot %u",
             node_id, node_id + 1);
}

uint8_t slotid_get(void)
{
    return node_id;
}

int slotid_from_nvs(void)
{
    return from_nvs;
}

int slotid_known(void)
{
    return known;
}

#endif
