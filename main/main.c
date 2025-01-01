#include <stdio.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define TAG "BLE_ACCESSORY"  // Tag used for ESP_LOG

// Define the service UUID (written in reverse order due to endianness)
static uint8_t service_uuid_pink[16] = {
    0xE3, 0xAB, 0xB8, 0x27, 0x91, 0x38, 0xAB, 0xA2,
    0xCA, 0x47, 0x9B, 0xC4, 0x2E, 0x08, 0x6A, 0xE5
};

// UUID for the dice roll characteristic (0xFF3F)
#define CHAR_UUID_DICE_ROLL 0xFF3F

// Function prototypes
void start_advertising(void);
void update_dice_value(uint8_t new_value);
void dice_roll_task(void* pvParameter);
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t* param);

// GATT profile structure to hold various attributes
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;           // GATT callback function
    uint16_t gatts_if;                 // GATT interface handle
    uint16_t conn_id;                  // Connection ID
    esp_bd_addr_t remote_bda;          // Remote Bluetooth device address
    uint16_t service_handle;           // Service handle
    esp_gatt_srvc_id_t service_id;     // Service ID
    uint16_t char_handle;              // Characteristic handle
    uint16_t cccd_handle;              // Client Characteristic Configuration Descriptor handle
};

// Instantiate the GATT profile
static struct gatts_profile_inst gl_profile = {
    .gatts_cb = gatts_event_handler,
    .gatts_if = ESP_GATT_IF_NONE,      // Interface not yet assigned
    .conn_id = 0xFFFF,                 // Invalid connection ID initially
};

// Global variables
esp_ble_adv_params_t adv_params;       // Advertising parameters
static uint8_t dice_value = 1;         // Current dice value
static bool notifications_enabled = false;  // Flag to check if notifications are enabled
static esp_ble_adv_data_t adv_data;         // Advertising data
static esp_ble_adv_data_t scan_rsp_data;    // Scan response data

// GAP callback function to handle BLE events
static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
    esp_ble_gap_cb_param_t* p = param;

    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising data set complete");
        esp_ble_gap_start_advertising(&adv_params);  // Start advertising
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan response data set complete");
        esp_ble_gap_start_advertising(&adv_params);  // Start advertising
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising started successfully");
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "Security request received");
        // Accept the security request
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Authentication complete");
        if (p->ble_security.auth_cmpl.success)
        {
            ESP_LOGI(TAG, "Authentication successful");
            esp_bd_addr_t bd_addr;
            memcpy(bd_addr, p->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "Remote BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x",
                     bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);

            // Retrieve IRK (Identity Resolving Key)
            esp_ble_bond_dev_t* bonded_devices = NULL;
            int num_bonded = esp_ble_get_bond_device_num();
            if (num_bonded > 0)
            {
                bonded_devices = malloc(sizeof(esp_ble_bond_dev_t) * num_bonded);
                esp_ble_get_bond_device_list(&num_bonded, bonded_devices);
                for (int i = 0; i < num_bonded; i++)
                {
                    if (memcmp(bonded_devices[i].bd_addr, bd_addr, sizeof(esp_bd_addr_t)) == 0)
                    {
                        ESP_LOGI(TAG, "Found bonded device, retrieving IRK");
                        esp_ble_bond_dev_t* dev = &bonded_devices[i];
                        esp_ble_bond_key_info_t* keys = &dev->bond_key;
                        ESP_LOGI(TAG, "IRK: ");
                        for (int j = 0; j < 16; j++)
                        {
                            printf("%02x ", keys->pid_key.irk[j]);
                        }
                        printf("\n");
                        break;
                    }
                }
                free(bonded_devices);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Authentication failed, status: %d", p->ble_security.auth_cmpl.fail_reason);
        }
        break;

    case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
        ESP_LOGI(TAG, "Packet length set complete");
        break;

    case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT:
        ESP_LOGI(TAG, "PHY update complete");
        break;

    default:
        ESP_LOGW(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

// GATT server event handler to manage GATT events
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t* param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT_REG_EVT, app_id %d", param->reg.app_id);
        gl_profile.gatts_if = gatts_if;

        // Set up the service ID
        gl_profile.service_id.is_primary = true;
        gl_profile.service_id.id.inst_id = 0x00;
        gl_profile.service_id.id.uuid.len = ESP_UUID_LEN_128;
        memcpy(gl_profile.service_id.id.uuid.uuid.uuid128, service_uuid_pink, 16);

        // Create the service
        esp_ble_gatts_create_service(gatts_if, &gl_profile.service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "GATT_CREATE_EVT, status %d, service_handle %d",
                 param->create.status, param->create.service_handle);
        gl_profile.service_handle = param->create.service_handle;

        // Start the service
        esp_ble_gatts_start_service(gl_profile.service_handle);

        // Add the dice roll characteristic
        esp_bt_uuid_t char_uuid;
        char_uuid.len = ESP_UUID_LEN_16;
        char_uuid.uuid.uuid16 = CHAR_UUID_DICE_ROLL;

        esp_gatt_char_prop_t char_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

        // Initialize the dice value as a string
        char dice_init_str[4];
        snprintf(dice_init_str, sizeof(dice_init_str), "%d", dice_value);

        esp_attr_value_t char_val;
        char_val.attr_max_len = 4; // Maximum length: up to 3-digit number + null terminator
        char_val.attr_len = strlen(dice_init_str);
        char_val.attr_value = (uint8_t *)dice_init_str;

        // Add the characteristic to the service
        esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile.service_handle, &char_uuid,
                               ESP_GATT_PERM_READ,
                               char_property,
                               &char_val, NULL);

        if (add_char_ret)
        {
            ESP_LOGE(TAG, "Failed to add characteristic: %s", esp_err_to_name(add_char_ret));
        }
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(TAG, "GATT_ADD_CHAR_EVT, attr_handle %d, service_handle %d",
                 param->add_char.attr_handle, param->add_char.service_handle);
        gl_profile.char_handle = param->add_char.attr_handle;

        // Add the Client Characteristic Configuration Descriptor (CCCD)
        esp_bt_uuid_t descr_uuid;
        descr_uuid.len = ESP_UUID_LEN_16;
        descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

        esp_attr_value_t cccd_val = {
            .attr_max_len = sizeof(uint16_t),
            .attr_len = sizeof(uint16_t),
            .attr_value = (uint8_t*)&(uint16_t){0x0000},
        };

        esp_attr_control_t control;
        control.auto_rsp = ESP_GATT_RSP_BY_APP;

        // Add the descriptor to the service
        esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_profile.service_handle, &descr_uuid,
                                                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                               &cccd_val, &control);
        if (add_descr_ret)
        {
            ESP_LOGE(TAG, "Failed to add descriptor: %s", esp_err_to_name(add_descr_ret));
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile.cccd_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(TAG, "Descriptor added, handle = %d", gl_profile.cccd_handle);
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "GATT_CONNECT_EVT, conn_id %d", param->connect.conn_id);
        gl_profile.conn_id = param->connect.conn_id;

        // Store the remote Bluetooth device address
        memcpy(gl_profile.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));

        // Initiate security (pairing) immediately upon connection
        esp_err_t ret = esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate encryption: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Encryption initiated successfully");
        }
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "GATT_DISCONNECT_EVT, conn_id %d", param->disconnect.conn_id);
        gl_profile.conn_id = 0xFFFF;      // Reset connection ID
        notifications_enabled = false;    // Disable notifications
        esp_ble_gap_start_advertising(&adv_params);  // Restart advertising
        break;

    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG, "GATT_READ_EVT, handle %d", param->read.handle);

        // Respond with the current dice value
        char dice_str[4];
        snprintf(dice_str, sizeof(dice_str), "%d", dice_value);

        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = strlen(dice_str);
        memcpy(rsp.attr_value.value, dice_str, rsp.attr_value.len);

        esp_err_t _ret = esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                                    param->read.trans_id, ESP_GATT_OK, &rsp);
        if (_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send read response: %s", esp_err_to_name(_ret));
        }
        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "GATT_WRITE_EVT, handle = %d, value len = %d",
                 param->write.handle, param->write.len);

        if (!param->write.is_prep)
        {
            // Handle write operations
            if (param->write.handle == gl_profile.cccd_handle && param->write.len == 2)
            {
                // Update notification status based on CCCD value
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                if (descr_value == 0x0001)
                {
                    // Notifications enabled
                    notifications_enabled = true;
                    ESP_LOGI(TAG, "Notifications enabled");
                }
                else if (descr_value == 0x0000)
                {
                    // Notifications disabled
                    notifications_enabled = false;
                    ESP_LOGI(TAG, "Notifications disabled");
                }
                else
                {
                    ESP_LOGE(TAG, "Unknown CCCD value");
                }
            }
        }
        // Send write response
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                    param->write.trans_id, ESP_GATT_OK, NULL);
        break;

    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(TAG, "GATT_CONF_EVT, status %d", param->conf.status);
        break;

    default:
        ESP_LOGW(TAG, "Unhandled GATT event: %d", event);
        break;
    }
}

// Function to update the dice value and send notifications if enabled
void update_dice_value(uint8_t new_value)
{
    esp_err_t ret;

    dice_value = new_value;  // Update the global dice value

    if (notifications_enabled && gl_profile.conn_id != 0xFFFF) {
        // Prepare the dice value as a string
        char dice_str[4];
        snprintf(dice_str, sizeof(dice_str), "%d", dice_value);

        // Send notification to the connected client
        ret = esp_ble_gatts_send_indicate(gl_profile.gatts_if, gl_profile.conn_id,
                                          gl_profile.char_handle, strlen(dice_str),
                                          (uint8_t *)dice_str, false);
        if (ret) {
            ESP_LOGE(TAG, "Failed to send notification: %s", esp_err_to_name(ret));
        }
    }
}

// Task that simulates dice rolling at regular intervals
void dice_roll_task(void *pvParameter)
{
    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait for 5 seconds

        // Generate a new dice value between 1 and 6
        uint8_t new_dice_value = (esp_random() % 6) + 1;

        ESP_LOGI(TAG, "Rolled new dice value: %d", new_dice_value);

        // Update the dice value and notify connected clients
        update_dice_value(new_dice_value);
    }
}

// Function to initialize and start BLE advertising
void start_advertising(void)
{
    esp_err_t ret;

    // Initialize advertising parameters
    adv_params.adv_int_min = 0x20;
    adv_params.adv_int_max = 0x40;
    adv_params.adv_type = ADV_TYPE_IND;
    adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    adv_params.channel_map = ADV_CHNL_ALL;
    adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    // Prepare advertising data
    memset(&adv_data, 0, sizeof(adv_data));
    adv_data.set_scan_rsp = false;  // Not a scan response
    adv_data.include_name = true;   // Include device name in advertising data
    adv_data.include_txpower = true;  // Include TX power level
    adv_data.min_interval = 0x0006;   // Minimum advertising interval
    adv_data.max_interval = 0x0010;   // Maximum advertising interval
    adv_data.appearance = 0x00;       // Appearance
    adv_data.manufacturer_len = 0;    // No manufacturer data
    adv_data.p_manufacturer_data = NULL;
    adv_data.service_data_len = 0;    // No service data
    adv_data.p_service_data = NULL;
    adv_data.service_uuid_len = sizeof(service_uuid_pink);  // Length of service UUID
    adv_data.p_service_uuid = service_uuid_pink;            // Pointer to service UUID
    adv_data.flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);  // Advertising flags

    // Configure advertising data
    ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret)
    {
        ESP_LOGE(TAG, "Failed to configure advertising data: %s", esp_err_to_name(ret));
        return;
    }

    // Prepare scan response data (optional)
    memset(&scan_rsp_data, 0, sizeof(scan_rsp_data));
    scan_rsp_data.set_scan_rsp = true;    // This is scan response data
    scan_rsp_data.include_name = true;    // Include device name
    scan_rsp_data.include_txpower = true; // Include TX power level

    // Configure scan response data
    ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
    if (ret)
    {
        ESP_LOGE(TAG, "Failed to configure scan response data: %s", esp_err_to_name(ret));
        return;
    }

    // Advertising will start in the GAP callback after data is set
}

// Main application entry point
void app_main(void)
{
    ESP_LOGI(TAG, "Starting BLE Accessory");

    esp_err_t ret;

    // Initialize NVS (Non-Volatile Storage) for BLE stack
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated or a new NVS version is found; erase and initialize
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize the BT controller with default configuration
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(TAG, "Controller initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Enable BLE mode in the BT controller
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(TAG, "Controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize Bluedroid stack
    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(TAG, "Bluedroid initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Enable Bluedroid stack
    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GAP and GATT callbacks
    esp_ble_gap_register_callback(ble_gap_cb);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(0); // Register the application with app_id = 0

    // Set BLE security parameters
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;          // Secure Connections with bonding, no MITM
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;                       // No input/output capabilities
    uint8_t key_size = 16;                                          // Maximum encryption key size
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;  // Initiator key distribution
    uint8_t resp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;  // Responder key distribution
    uint8_t auth_option = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE; // Only accept specified authentication
    uint8_t oob_support = ESP_BLE_OOB_DISABLE;                      // Out-of-band data not supported

    // Set security parameters in the GAP
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &auth_option, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &resp_key, sizeof(uint8_t));

    ESP_LOGI(TAG, "BLE Accessory initialized. Starting advertising...");

    // Start BLE advertising
    start_advertising();

    // Create a task to simulate dice rolling every 5 seconds
    xTaskCreate(dice_roll_task, "dice_roll_task", 2048, NULL, 5, NULL);
}