// main.c

#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define TAG "BLE_DICE"

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t ble_addr_type;
static uint8_t dice_value = 1; // Initial dice value

static ble_uuid128_t dice_service_uuid128 = BLE_UUID128_INIT(0xE3, 0xAB, 0xB8, 0x27, 0x91, 0x38, 0xAB, 0xA2,
                                                             0xCA, 0x47, 0x9B, 0xC4, 0x2E, 0x08, 0x6A, 0xE5);
static ble_uuid16_t dice_char_uuid16 = BLE_UUID16_INIT(0xFF3F);
static uint16_t dice_char_handle;

static void ble_app_advertise(void);

static int gatt_svr_chr_access_dice(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    const char *dice_value_str;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            // Handle read request
            dice_value_str = (dice_value == 0) ? "1" : (char[2]){dice_value + '0', '\0'};
            rc = os_mbuf_append(ctxt->om, dice_value_str, strlen(dice_value_str));
            if (rc != 0) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            ESP_LOGI(TAG, "Read request; dice value: %s", dice_value_str);
            return 0;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            // Not writable; return error
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
        {
                // Service: Dice Service
                .type = BLE_GATT_SVC_TYPE_PRIMARY,
                .uuid = &dice_service_uuid128.u,
                .characteristics = (struct ble_gatt_chr_def[]) {
                        {
                                // Characteristic: Dice Roll Result
                                .uuid = &dice_char_uuid16.u,
                                .access_cb = gatt_svr_chr_access_dice,
                                .val_handle = &dice_char_handle,
                                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                        },
                        {
                                0, // No more characteristics in this service
                        },
                },
        },
        {
                0, // No more services
        },
};

static void ble_app_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &ble_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type");
        return;
    }

    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(ble_addr_type, addr_val, NULL);
    ESP_LOGI(TAG, "Device Address: %02X:%02X:%02X:%02X:%02X:%02X",
             addr_val[5], addr_val[4], addr_val[3],
             addr_val[2], addr_val[1], addr_val[0]);

    // Begin advertising
    ble_app_advertise();
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connection established");
                conn_handle = event->connect.conn_handle;

                // Initiate security
                int rc = ble_gap_security_initiate(conn_handle);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Failed to initiate security; rc=%d", rc);
                } else {
                    ESP_LOGI(TAG, "Security initiated");
                }
            } else {
                ESP_LOGI(TAG, "Connection failed; status=%d", event->connect.status);
                ble_app_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_app_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Advertising complete");
            ble_app_advertise();
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status == 0) {
                ESP_LOGI(TAG, "Encryption change event; encrypted");

                // Retrieve IRK and complete enrollment
                struct ble_store_key_sec key_sec;
                struct ble_store_value_sec value_sec;

                memset(&key_sec, 0, sizeof(key_sec));
                memset(&value_sec, 0, sizeof(value_sec));

                // Get the peer's address
                struct ble_gap_conn_desc desc;
                int rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Failed to get connection descriptor; rc=%d", rc);
                    break;
                }

                // Set up the key to search for the peer's security information
                key_sec.peer_addr = desc.peer_id_addr;
                key_sec.idx = 0; // First match

                // Load the peer's security information
                rc = ble_store_read_peer_sec(&key_sec, &value_sec);
                if (rc == 0 && value_sec.irk_present) {
                    // Extract the IRK
                    char irk_str[33] = {0};
                    for (int i = 0; i < 16; i++) {
                        sprintf(&irk_str[i * 2], "%02X", value_sec.irk[15 - i]);
                    }
                    ESP_LOGI(TAG, "Retrieved IRK: %s", irk_str);
                    // Enrollment complete
                    // You can store or use the IRK as needed here
                } else {
                    ESP_LOGE(TAG, "Failed to load peer security info; rc=%d", rc);
                }
            } else {
                ESP_LOGE(TAG, "Encryption failed; status=%d", event->enc_change.status);
            }
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(TAG, "Passkey action event; action=%d", event->passkey.params.action);

            switch (event->passkey.params.action) {
                case BLE_SM_IOACT_NONE:
                    // Just Works pairing; no action needed
                    break;
                case BLE_SM_IOACT_NUMCMP:
                    // Numeric comparison; auto-confirm
                    ESP_LOGI(TAG, "Numeric Comparison: %lu", event->passkey.params.numcmp);
                    ble_sm_inject_io(event->passkey.conn_handle, &(struct ble_sm_io){
                            .action = BLE_SM_IOACT_NUMCMP,
                            .numcmp_accept = 1,
                    });
                    break;
                default:
                    // Other actions not supported
                    ESP_LOGE(TAG, "Unsupported passkey action");
                    ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    break;
            }
            break;

        default:
            break;
    }
    return 0;
}

static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));

    // Set the service UUID to advertise
    fields.uuids128 = &dice_service_uuid128;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    // Device name
    const char *device_name = "Pink Dice";
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    // Set advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Connectable undirected advertising
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // General discoverable mode

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising started");
    }
}

static void dice_roll_timer_cb(void *arg)
{
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        // Generate a random dice value between 1 and 6
        dice_value = (esp_random() % 6) + 1;
        char dice_value_str[2];
        sprintf(dice_value_str, "%d", dice_value);

        // Notify the characteristic
        struct os_mbuf *om = ble_hs_mbuf_from_flat(dice_value_str, strlen(dice_value_str));
        ble_gattc_notify_custom(conn_handle, dice_char_handle, om);

        ESP_LOGI(TAG, "Notified dice value: %s", dice_value_str);
    }
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run(); // This function will return only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "Initializing NVS done");

    // Initialize the NimBLE stack
    nimble_port_init();

    ESP_LOGI(TAG, "NimBLE Port Initialized");

    // Set security parameters
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO; // Just Works pairing
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Register the GATT server
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Add our custom service
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    // Set the device name
    ble_svc_gap_device_name_set("Pink Dice");

    // Set the callback for host synchronization
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // Start the BLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Host Task Started");

    // Create a periodic timer to simulate dice rolls every 5 seconds
    const esp_timer_create_args_t dice_timer_args = {
            .callback = &dice_roll_timer_cb,
            .name = "dice_roll_timer"
    };
    esp_timer_handle_t dice_timer;
    ESP_ERROR_CHECK(esp_timer_create(&dice_timer_args, &dice_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(dice_timer, 5000000)); // 5 seconds
}
