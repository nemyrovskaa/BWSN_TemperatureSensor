/*
 * white_list.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_WHITE_LIST_H_
#define MAIN_WHITE_LIST_H_


#include <stdio.h>
#include <unistd.h>
#include "host/ble_hs.h"
#include "esp_check_err.h"


// struct that describes device in white list
typedef struct
{
    ble_addr_t device_addr; // device mac addr
    bool addr_is_empty;     // flag indicating whether the addr field is empty or not
} device_data_t;


esp_err_t init_white_list();
esp_err_t deinit_white_list();
uint8_t get_white_list_len();
esp_err_t push_to_white_list(ble_addr_t addr);
esp_err_t remove_from_white_list_by_addr(const ble_addr_t* addr);
bool white_list_contains_addr(const ble_addr_t* addr);
bool white_list_is_empty();
esp_err_t get_addr_white_list(ble_addr_t **device_addr);
bool addrs_are_equal(const ble_addr_t* addr1, const ble_addr_t* addr2);


bool wl_is_initialised = false;     // flag to indicate whether white list has been inited
const uint8_t white_list_size = 1;  // size of the white list
uint8_t white_list_len = 0;         // number of entries in the white list


// white list, stored in RTC memory to persist across sleep cycles
RTC_DATA_ATTR device_data_t white_list[] = {
        {.device_addr = {},
        .addr_is_empty = true
        }
};


// inits the white list
esp_err_t init_white_list()
{
    if (wl_is_initialised)  // check if already initialised
        return ESP_FAIL;

    white_list_len = get_white_list_len();  // count the non-empty entries (after sleep)
    wl_is_initialised = true;               // mark as initialised
    return ESP_OK;
}


// deinits the white list
esp_err_t deinit_white_list()
{
    if (!wl_is_initialised) // check if white list was not initialised
        return ESP_FAIL;

    white_list_len = 0;         // reset the list length
    wl_is_initialised = false;  // mark as not initialised
    return ESP_OK;
}


// calculates the number of non-empty entries in the white list
uint8_t get_white_list_len()
{
    uint8_t cnt = 0;
    for(uint8_t i = 0; i < white_list_size; i++)
        if (!white_list[i].addr_is_empty)
            cnt++;

    return cnt;
}

// adds a device to the white list by uuid and addr
esp_err_t push_to_white_list(ble_addr_t addr)
{
    if (!wl_is_initialised) // check if already initialised
        return ESP_FAIL;

    if (white_list_len == white_list_size)   // check if the list is full
        return ESP_FAIL;

    for (uint8_t i = 0; i < white_list_size; i++)
        if (white_list[i].addr_is_empty)
        {
            white_list[i].device_addr = addr;       // assign the addr
            white_list[i].addr_is_empty = false;    // mark as not empty
            white_list_len++;   // increment the list length
            return ESP_OK;
        }

    return ESP_FAIL;    // no matching entry found
}


// removes a device from the white list by addr
esp_err_t remove_from_white_list_by_addr(const ble_addr_t* addr)
{
    if (!wl_is_initialised)     // check if already initialised
        return ESP_FAIL;

    if (white_list_len == 0)    // check if the list is empty
        return ESP_FAIL;

    for (uint8_t i = 0; i < white_list_size; i++)
        if (!white_list[i].addr_is_empty && addrs_are_equal(&white_list[i].device_addr, addr))
        {
            white_list[i].addr_is_empty = true; // mark as empty entity with corresp. addr
            white_list_len--;   // decrement the list length
            return ESP_OK;
        }

    return ESP_FAIL;
}


// checks if the white list contains a specific mac address
bool white_list_contains_addr(const ble_addr_t* addr)
{
    if (!wl_is_initialised)     // check if already initialised
        return ESP_FAIL;

    if (white_list_len == 0)    // check if the list is empty
        return false;

    for (uint8_t i = 0; i < white_list_size; i++)
        if (!white_list[i].addr_is_empty && addrs_are_equal(&white_list[i].device_addr, addr))
            return true;    // addr found

    return false;    // addr not found
}


// checks if the white list is empty
bool white_list_is_empty()
{
    return white_list_len == 0;
}


// retrieves all addrs in the white list (not structures)
esp_err_t get_white_list_addr(ble_addr_t *addr)
{
    if (!wl_is_initialised)     // check if already initialised
        return ESP_FAIL;

    if (white_list_len == 0)    // check if the list is empty
        return ESP_FAIL;

    *addr = white_list[0].device_addr;

    return ESP_OK;
}


// compares two mac addrs for equality
bool addrs_are_equal(const ble_addr_t* addr1, const ble_addr_t* addr2)
{
    if (addr1->type == addr2->type)
        if (memcmp(addr1->val, addr2->val, 6) == 0)
            return true;
    return false;
}


#endif /* MAIN_WHITE_LIST_H_ */
