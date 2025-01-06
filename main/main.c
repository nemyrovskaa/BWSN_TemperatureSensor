/*
 * Temperature Sensor
 * 2024
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"

#include "sdkconfig.h"

#include "esp_check_err.h"
#include "led.h"
#include "button.h"
#include "i2c_driver.h"
#include "white_list.h"
#include "app_packet.h"

#define DEBUGGING   // enables ESP_CHECK macro (see more esp_check_err.h)
#define GPIO_LED    GPIO_NUM_8
#define GPIO_SDA    GPIO_NUM_6
#define GPIO_SCL    GPIO_NUM_7
#define GPIO_BUTTON GPIO_NUM_3

#define MAX30205_I2C_ADDR 0x90
#define MAX30205_TEMP_REG_PTR 0x00
#define MAX30205_CNFG_REG_PTR 0x01

#define RSSI_ACCEPTABLE_LVL     -50         // acceptable rssi level for connection
#define DEEP_SLEEP_CYCLE_TIME   5 * 1000000 // in us (5 s)

#define MAC_STR_SIZE 3 * 6

// enumeration of possible modes for this device
// these modes determine the current state or functionality of the device
// UNSPECIFIED_MODE  - default or undefined mode
// REGISTRATION_MODE - device is in the process of registering other devices (sensors)
// DELETION_MODE     - device is in the process of deleting other devices (sensors)
typedef enum {
    UNSPECIFIED_MODE = 0,
    REGISTRATION_MODE = 1,
    DELETION_MODE = 2

} g_device_mode_t;


g_device_mode_t g_device_mode = UNSPECIFIED_MODE;  // current mode, UNSPECIFIED_MODE by default
uint8_t g_ble_addr_type;        // addr type, set automatically in ble_hs_id_infer_auto()
const char* s_tag_temp = "TEMP";// tag used in ESP_CHECK


// button process callbacks (see more button.h)
void on_short_button_press();
void on_medium_button_press();
void on_long_button_press();

void init_ble();
void ble_app_on_sync(void);
void host_task();
static int ble_gap_event(struct ble_gap_event *event, void *arg);
float convert_temp_data_to_float(uint8_t temp_msb, uint8_t temp_lsb);
static int read_temp(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
void get_mac_str(uint8_t* addr, char (*mac_str)[MAC_STR_SIZE]);


void app_main(void)
{
    // inits led (see more led.h)
    led_init(GPIO_LED);

    // set up button cnfg and init button (see more button.h)
    button_cnfg_t button_cnfg = {
            .gpio_num = GPIO_BUTTON,
            .short_button_press_period_ms = 1000,
            .medium_button_press_period_ms = 5000,
            .long_button_press_period_ms = 10000,
            .on_short_button_press_cb = on_short_button_press,
            .on_medium_button_press_cb = on_medium_button_press,
            .on_long_button_press_cb  = on_long_button_press
    };
    button_init(button_cnfg);

    // init i2c (see more i2c_driver.h)
    i2c_port_t i2c_port = I2C_NUM_0;
    esp_i2c_init(i2c_port, GPIO_SDA, GPIO_SCL);

    // set configuration register of temperature sensor
    // MAX30205 to shut it down
    uint8_t cnfg_reg = 0b00000001;
    esp_i2c_set_cnfg_reg(I2C_NUM_0, MAX30205_I2C_ADDR, MAX30205_CNFG_REG_PTR, &cnfg_reg);

    //init white list (see more white_list.h)
    init_white_list();

    // init NVS
    ESP_CHECK(nvs_flash_init(), s_tag_temp);

    // init BLE
    init_ble();

    // get wakeup cause and do corresponding actions
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    switch (wakeup_cause)
    {
        case ESP_SLEEP_WAKEUP_GPIO:
        {
            // wakeup from gpio means that device was asleep and user
            // pressed on a button. next actions could be: registration,
            // deletion or just wakeup (needed for debug now)
            force_interupt();
            ESP_LOGI(s_tag_temp, "Waking up from GPIO.");

            break;
        }
        case ESP_SLEEP_WAKEUP_TIMER:
        {
            // wakeup from timer means that device is periodically sends data
            ESP_LOGI(s_tag_temp, "Waking up from timer.");
            led_turn_on(); // turn on to show that device is awaken

            // set configuration register of temperature sensor
            // MAX30205 to one shot read and shutdown
            uint8_t cnfg_reg = 0b10000001;
            esp_i2c_set_cnfg_reg(I2C_NUM_0, MAX30205_I2C_ADDR, MAX30205_CNFG_REG_PTR, &cnfg_reg);

            // read temperature data from temp sensor MAX30205
            ESP_LOGI(s_tag_temp, "Start data read from MAX30205.");
            const uint8_t data_buff_len = 2;
            uint8_t data_buff[data_buff_len];
            esp_i2c_read(I2C_NUM_0, MAX30205_I2C_ADDR, MAX30205_TEMP_REG_PTR, data_buff, data_buff_len);
            ESP_LOGI(s_tag_temp, "temp = %.8f", convert_temp_data_to_float(data_buff[0], data_buff[1]));

            // form advertising packet
            const char *device_name;
            device_name = ble_svc_gap_device_name();

            struct ble_hs_adv_fields adv_fields;
            memset(&adv_fields, 0, sizeof(adv_fields));
            adv_fields.name = (uint8_t*)device_name;    // set device name
            adv_fields.name_len = strlen(device_name);  // set device name length
            adv_fields.name_is_complete = 1;            // indicate the name is complete (no
                                                        // cut down due to adv package size limit)
            adv_fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;// classic bluetooth is unsupported
            adv_fields.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(0x1809)}; // 1809 uuid - temperature
            adv_fields.num_uuids16 = 1;                 // one UUID is used
            adv_fields.uuids16_is_complete = 1;         // indicate the UUID list is complete
                                                        // (no cut down due to adv package size limit)

            // form application packet (see app_packet.h) with DATA_HEADER
            // and set it as manufacturer's data
            uint8_t packet_buff[data_buff_len + HEADER_SIZE];
            form_packet(packet_buff, DATA_HEADER, data_buff, data_buff_len);
            adv_fields.mfg_data = packet_buff;
            adv_fields.mfg_data_len = data_buff_len + HEADER_SIZE;

            // set and check advertising packet fields
            ESP_CHECK(ble_gap_adv_set_fields(&adv_fields), s_tag_temp);

            // set advertising parameters
            struct ble_gap_adv_params adv_params;
            memset(&adv_params, 0, sizeof(adv_params));
            adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   // undirected advertising
            adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;   // non-discoverable (connect only in
                                                            // deletion/registr. mode, not while sending data)
            adv_params.itvl_min = 0x10;                     // min advertising interval
            adv_params.itvl_max = 0x20;                     // max advertising interval
            adv_params.channel_map = BLE_GAP_ADV_DFLT_CHANNEL_MAP; // default channel map
            adv_params.high_duty_cycle = 0;                 // low transmission frequency (for saving power)

            ESP_LOGI(s_tag_temp, "Sending data.......");

            // get white list with addrs to set in ble_gap_adv_start for adv
            ble_addr_t wl_addr;
            get_white_list_addr(&wl_addr);

            // start advertising for 1 s
            int32_t adv_duration_ms = 1*1000;
            ESP_CHECK(ble_gap_adv_start(g_ble_addr_type, &wl_addr, adv_duration_ms, &adv_params, ble_gap_event, NULL), s_tag_temp);

            break;
        }
        default:
        {
            // if we woke up from another cause, that means something
            // went wrong, so go back to sleep
            ESP_LOGI(s_tag_temp, "Waking up from other cause.");
            ESP_LOGI(s_tag_temp, "Go to sleep.");
            esp_deep_sleep_start();
            break;
        }
    }
}


// inits nimble, gap & gatt services
void init_ble()
{
    nimble_port_init();
    ble_svc_gap_device_name_set("Nemivika-Temp");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // TODO add device information svc with battery info chr
    // configure gatt characteristics
    const struct ble_gatt_chr_def gatt_chr_temp = {
            .uuid = BLE_UUID16_DECLARE(0x2A1C), // Temperature Measurement
            .flags = BLE_GATT_CHR_F_READ,
            .access_cb = read_temp};

    // configure gatt services
    const struct ble_gatt_svc_def gatt_svc_cnfg = {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = BLE_UUID16_DECLARE(0x1809), // Health Thermometer Service
            .characteristics = (struct ble_gatt_chr_def[]){gatt_chr_temp, {0}}};


    // set configuration
    struct ble_gatt_svc_def gatt_svc_cnfgs[] = {gatt_svc_cnfg, {0}};
    ble_gatts_count_cfg(gatt_svc_cnfgs);
    ble_gatts_add_svcs(gatt_svc_cnfgs);

    // set the callback function to be executed when the ble stack is synchronised
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // init FreeRTOS task for nimble
    nimble_port_freertos_init(host_task);
}


void ble_app_on_sync(void)
{
    // infer and set the ble addr type
    ble_hs_id_infer_auto(0, &g_ble_addr_type);
}


// main nimble host task, handles the ble stack processing
void host_task()
{
    nimble_port_run();              // start nimble processing loop
    nimble_port_freertos_deinit();  // deinit FreeRTOS
}


// gap event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_ADV_COMPLETE:
        {
            // if advertisement was comlete, we may:
            // - signal that data sending was completed
            // - TODO do something if there weren't any device
            //   to connect with for registration or deletion
            if (g_device_mode == REGISTRATION_MODE || g_device_mode == DELETION_MODE)
                break;

            ESP_LOGI(s_tag_temp, "Sending data is completed!");
            ESP_LOGI(s_tag_temp, "Go to sleep...");

            led_turn_off(); // turn led off, because data was send and go to sleep

            // if white list is not empty, then we have registered
            // devices to get data from => enable timer wakeup.
            // if not, we will just go to deepsleep until gpio wakeup
            if (!white_list_is_empty())
                ESP_CHECK(esp_sleep_enable_timer_wakeup(DEEP_SLEEP_CYCLE_TIME), s_tag_temp);

            esp_deep_sleep_start();

            break;
        }
        case BLE_GAP_EVENT_CONNECT:
        {
            // if device is connected, we may:
            // - register new device (analysis module-gateway)
            // - delete registered device (analysis module-gateway)
            struct ble_gap_conn_desc conn_desc;
            ble_gap_conn_find(event->connect.conn_handle, &conn_desc);

            // check status, if everything okay, then
            if (event->connect.status == 0)
            {
                ESP_LOGI(s_tag_temp, "CONNECTION established!");

                char our_mac[MAC_STR_SIZE];
                char peer_mac[MAC_STR_SIZE];
                get_mac_str(conn_desc.our_id_addr.val, &our_mac);
                get_mac_str(conn_desc.peer_id_addr.val, &peer_mac);
                ESP_LOGI(s_tag_temp, "This device id addr:\t%s", our_mac);
                ESP_LOGI(s_tag_temp, "Connected device id addr:\t%s", peer_mac);

                // stop advertising
                ble_gap_adv_stop();

                // if this device is in registration mode:
                //     - add to white list
                //     - try to disconnect
                //     - TODO read time chr from am-dateway and set it
                // if this device is in deletion mode:
                //     - delete from white list
                //     - try to disconnect
                if (g_device_mode == REGISTRATION_MODE)
                {
                    // читаємо час
                    //ble_gattc_disc_all_svcs(event->connect.conn_handle, get_time_attr_hndl, NULL);
                    /*int rc = ble_gattc_read(event->connect.conn_handle, 0x01, gatt_read, NULL);
                    ESP_LOGI("GATT Client", "rc=%d", rc);
                    if (rc != 0) {
                        ESP_LOGE("GATT Client", "Error initiating read; rc=%d", rc);
                    }*/

                    // add to white list
                    push_to_white_list(conn_desc.peer_id_addr);
                    // start fast blink, meaning that registration was successful
                    led_start_blink(100, 100);
                    ESP_LOGI(s_tag_temp, "Registration is completed.");
                }
                else if (g_device_mode == DELETION_MODE)
                {
                    // delete from white list
                    bool deleted = remove_from_white_list_by_addr(&conn_desc.peer_id_addr) == ESP_OK ? true : false;
                    if (deleted)
                    {
                        // start slow blink, meaning that deletion was successful
                        led_start_blink(700, 700);
                        ESP_LOGI(s_tag_temp, "Deletion is completed.");
                    }
                    else
                    {
                        ESP_LOGI(s_tag_temp, "Deletion failed.");
                    }
                }
                // try to disconnect
                ESP_LOGI(s_tag_temp, "Try to disconnect...");
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            else
            {
                ESP_LOGI("CONN", "CONNECTION is NOT established!");
            }

            // print info about white list
            ESP_LOGI(s_tag_temp, "White List: len = %u", white_list_len);
            for(int i = 0; i < white_list_len; i++)
            {
                char wl_mac[MAC_STR_SIZE];
                get_mac_str(white_list[i].device_addr.val, &wl_mac);
                ESP_LOGI(s_tag_temp, "WL[%d] = {%s}", i, wl_mac);
            }
            break;
        }
        case BLE_GAP_EVENT_DISCONNECT:
        {
            // print that device is disconnected
            char peer_mac[MAC_STR_SIZE];
            get_mac_str(event->disconnect.conn.peer_id_addr.val, &peer_mac);
            ESP_LOGI(s_tag_temp, "DISCONNECTED with %s! The reason - %d.", peer_mac, event->disconnect.reason);

            break;
        }
        default:
            ESP_LOGI(s_tag_temp, "Default.");
            break;
    }
    return 0;
}


// pressing on button during 1 - 5 s causes entering or exiting
// registration mode
void on_medium_button_press()
{
    // if device isn't in registration or deletion mode now,
    // then it could enter registration mode
    if (g_device_mode != REGISTRATION_MODE && g_device_mode != DELETION_MODE)
    {
        // set registration mode and turn led on
        g_device_mode = REGISTRATION_MODE;
        led_turn_on();

        ESP_LOGI(s_tag_temp, "Entering register mode.");
        ESP_LOGI(s_tag_temp, "Broadcast advertising.......");

        // for registration, device starts advertising
        // form advertising packet
        const char *device_name;
        device_name = ble_svc_gap_device_name();

        struct ble_hs_adv_fields adv_fields;
        memset(&adv_fields, 0, sizeof(adv_fields));
        adv_fields.name = (uint8_t*)device_name;    // set device name
        adv_fields.name_len = strlen(device_name);  // set device name length
        adv_fields.name_is_complete = 1;            // indicate the name is complete
        adv_fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;// classic bluetooth is unsupported
        adv_fields.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(0x1809)}; // 1809 uuid - temperature
        adv_fields.num_uuids16 = 1;                 // one UUID is used
        adv_fields.uuids16_is_complete = 1;         // indicate the UUID list is complete

        // form application packet (see app_packet.h) with REG_HEADER
        // and set it as manufacturer's data
        uint8_t packet_buff[HEADER_SIZE];
        uint8_t* data_buff = NULL;
        form_packet(packet_buff, REG_HEADER, data_buff, 0);
        adv_fields.mfg_data = packet_buff;
        adv_fields.mfg_data_len = HEADER_SIZE;

        // set and check advertising packet fields
        ESP_CHECK(ble_gap_adv_set_fields(&adv_fields), s_tag_temp);

        // set advertising parameters
        struct ble_gap_adv_params adv_params;
        memset(&adv_params, 0, sizeof(adv_params));
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   // undirected adv, because we want to register new device
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;   // discoverable (connect in deletion/registr. mode)
        adv_params.itvl_min = 0x0010;
        adv_params.itvl_max = 0x0020;
        adv_params.channel_map = BLE_GAP_ADV_DFLT_CHANNEL_MAP;
        adv_params.high_duty_cycle = 0;

        // set duration to forever (TODO not forever but some time) until device is found
        int32_t adv_duration_ms = BLE_HS_FOREVER;
        ESP_CHECK(ble_gap_adv_start(g_ble_addr_type, NULL, adv_duration_ms, &adv_params, ble_gap_event, NULL), s_tag_temp);
    }
    else if (g_device_mode == REGISTRATION_MODE)
    {
        // if device is in registration mode now, that means user exit this mode
        ESP_LOGI(s_tag_temp, "Quiting registration mode.");

        // if white list is not empty, then we have registered
        // devices to get data from => enable timer wakeup.
        // if not, we will just go to deepsleep until gpio wakeup
        if (!white_list_is_empty())
            ESP_CHECK(esp_sleep_enable_timer_wakeup(DEEP_SLEEP_CYCLE_TIME), s_tag_temp);

        // turn led off as signal for exiting registration mode
        led_turn_off();

        // set device into unspecified mode and go to sleep
        g_device_mode = UNSPECIFIED_MODE;
        esp_deep_sleep_start();
    }
}


// pressing on button during 5 and more s causes entering or exiting
// deletion mode
void on_long_button_press()
{
    // if device isn't in registration or deletion mode now,
    // then it could enter deletion mode
    if (g_device_mode != DELETION_MODE && g_device_mode != REGISTRATION_MODE && !white_list_is_empty())
    {
        // set deletion mode and turn led on
        g_device_mode = DELETION_MODE;
        led_turn_on();

        ESP_LOGI(s_tag_temp, "Entering deletion mode.");
        ESP_LOGI(s_tag_temp, "Directed advertising.......");

        // for deletion, device starts advertising
        // form advertising packet
        const char *device_name;
        device_name = ble_svc_gap_device_name();

        struct ble_hs_adv_fields adv_fields;
        memset(&adv_fields, 0, sizeof(adv_fields));
        adv_fields.name = (uint8_t*)device_name;    // set device name
        adv_fields.name_len = strlen(device_name);  // set device name length
        adv_fields.name_is_complete = 1;            // indicate the name is complete
        adv_fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;// classic bluetooth is unsupported
        adv_fields.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(0x1809)};// 1809 uuid - temperature
        adv_fields.num_uuids16 = 1;                 // one UUID is used
        adv_fields.uuids16_is_complete = 1;         // indicate the UUID list is complete

        // form application packet (see app_packet.h) with DEL_HEADER
        // and set it as manufacturer's data
        uint8_t packet_buff[HEADER_SIZE];
        uint8_t* data_buff = NULL;
        form_packet(packet_buff, (uint8_t)DEL_HEADER, data_buff, 0);
        adv_fields.mfg_data = packet_buff;
        adv_fields.mfg_data_len = HEADER_SIZE;

        // set and check advertising packet fields
        ESP_CHECK(ble_gap_adv_set_fields(&adv_fields), s_tag_temp);

        // set advertising parameters
        struct ble_gap_adv_params adv_params;
        memset(&adv_params, 0, sizeof(adv_params));
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   // undirected advertising
        adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;   // discoverable (connect in deletion/registr. mode)
        adv_params.itvl_min = 0x10;
        adv_params.itvl_max = 0x20;
        adv_params.channel_map = BLE_GAP_ADV_DFLT_CHANNEL_MAP;
        adv_params.high_duty_cycle = 0;

        // get white list with addrs to set in ble_gap_adv_start for adv
        ble_addr_t wl_addr;
        get_white_list_addr(&wl_addr);

        // set duration to forever (TODO not forever but some time) until device is found
        int32_t adv_duration_ms = BLE_HS_FOREVER;
        ble_gap_adv_start(g_ble_addr_type, &wl_addr, adv_duration_ms, &adv_params, ble_gap_event, NULL);
    }
    else if (g_device_mode == DELETION_MODE)
    {
        // if device is in deletion mode now, that means user exit this mode
        ESP_LOGI(s_tag_temp, "Quiting deletion mode.");

        // if white list is not empty, then we have registered
        // devices to get data from => enable timer wakeup.
        // if not, we will just go to deepsleep until gpio wakeup
        if (!white_list_is_empty())
            ESP_CHECK(esp_sleep_enable_timer_wakeup(DEEP_SLEEP_CYCLE_TIME), s_tag_temp);

        // turn led off as signal for exiting deletion mode
        led_turn_off();

        // set device into unspecified mode and go to sleep
        g_device_mode = UNSPECIFIED_MODE;
        esp_deep_sleep_start();
    }
}


// no action on button press under 1 s
void on_short_button_press()
{

}


// converts raw temperature data (from two bytes) to a float value
float convert_temp_data_to_float(uint8_t temp_msb, uint8_t temp_lsb)
{
    // extract the most significant byte (MSB) and the least significant byte (LSB)
    float ret_val = (float)(temp_msb & 0b01111111);

    // add the fractional part by shifting the LSB and dividing by powers of 2
    for (int i = 0; i < 8; i++)
        ret_val += ((temp_lsb >> (7-i)) & 1) / (float)(2 << i);

    // adjust for the sign based on the MSB (negative if the MSB's sign bit is 1)
    return ret_val * ((temp_msb>>7) & 1 ? -1.0 : 1.0);
}


// read temperature chr (TODO not sure if needed)
static int read_temp(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char* str = "Hello from the server";
    os_mbuf_append(ctxt->om, str, strlen(str));
    return 0;
}


// makes string with mac addr for printing
void get_mac_str(uint8_t* addr, char(*mac_str)[MAC_STR_SIZE])
{
    for (int i = 0; i < 6; i++)
        sprintf(*mac_str+(i*3), "%02X:", addr[5-i]);

    (*mac_str)[MAC_STR_SIZE - 1] = '\0';
}
