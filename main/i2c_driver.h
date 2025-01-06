/*
 * i2c_driver.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_I2C_DRIVER_H_
#define MAIN_I2C_DRIVER_H_


#include <unistd.h>
#include "esp_log.h"
#include "driver/i2c.h"

#include "esp_check_err.h"

const char* g_tag_i2c = "I2C";

void esp_i2c_init(i2c_port_t i2c_port, int gpio_sda, int gpio_scl);
void esp_i2c_set_cnfg_reg(i2c_port_t i2c_port, uint8_t addr, uint8_t reg_ptr, uint8_t* max30205_cnfg_reg);
void esp_i2c_read(i2c_port_t i2c_port, uint8_t addr, uint8_t reg_ptr, uint8_t* read_data_buff, uint8_t read_data_buff_len);


// creates an I2C configuration structure and sets its fields
void esp_i2c_init(i2c_port_t i2c_port, int gpio_sda, int gpio_scl)
{
    i2c_config_t i2c_cnfg;
    i2c_cnfg.mode = I2C_MODE_MASTER;                    // set the I2C mode to master
    i2c_cnfg.sda_io_num = gpio_sda;                     // set the GPIO pin for SDA line
    i2c_cnfg.scl_io_num = gpio_scl;                     // set the GPIO pin for SCL line
    i2c_cnfg.sda_pullup_en = GPIO_PULLUP_ENABLE;        // enable pull-up resistor for SDA line
    i2c_cnfg.scl_pullup_en = GPIO_PULLUP_ENABLE;        // enable pull-up resistor for SCL line
    i2c_cnfg.master.clk_speed = 100000;                 // set the I2C clock speed to 100 kHz
    i2c_cnfg.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;   // use standard clock source

    // configure the I2C parameters for the specified I2C port & install the I2C driver
    ESP_CHECK(i2c_param_config(i2c_port, &i2c_cnfg), g_tag_i2c);
    ESP_CHECK(i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0), g_tag_i2c);
}


// sends a sequence of I2C commands to write to a configuration register
void esp_i2c_set_cnfg_reg(i2c_port_t i2c_port, uint8_t addr, uint8_t reg_ptr, uint8_t* max30205_cnfg_reg)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    ESP_CHECK(i2c_master_start(cmd), g_tag_i2c);
    ESP_CHECK(i2c_master_write_byte(cmd, addr | I2C_MASTER_WRITE, I2C_MASTER_ACK), g_tag_i2c);
    ESP_CHECK(i2c_master_write_byte(cmd, reg_ptr, I2C_MASTER_ACK), g_tag_i2c);

    ESP_CHECK(i2c_master_write_byte(cmd, *max30205_cnfg_reg, I2C_MASTER_ACK), g_tag_i2c);   // write byte to cnfg_reg

    ESP_CHECK(i2c_master_stop(cmd), g_tag_i2c);
    ESP_CHECK(i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_PERIOD_MS), g_tag_i2c);
    i2c_cmd_link_delete(cmd);
}


// reads data from a MAX30205 data register
void esp_i2c_read(i2c_port_t i2c_port, uint8_t addr, uint8_t reg_ptr, uint8_t* read_data_buff, uint8_t read_data_buff_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    ESP_CHECK(i2c_master_start(cmd), g_tag_i2c);
    ESP_CHECK(i2c_master_write_byte(cmd, addr | I2C_MASTER_WRITE, I2C_MASTER_ACK), g_tag_i2c);
    ESP_CHECK(i2c_master_write_byte(cmd, reg_ptr, I2C_MASTER_ACK), g_tag_i2c);

    ESP_CHECK(i2c_master_start(cmd), g_tag_i2c);
    ESP_CHECK(i2c_master_write_byte(cmd, addr | I2C_MASTER_READ, I2C_MASTER_ACK), g_tag_i2c);

    uint8_t* temp_msb = read_data_buff;
    uint8_t* temp_lsb = read_data_buff+1;
    ESP_CHECK(i2c_master_read_byte(cmd, temp_msb, I2C_MASTER_ACK), g_tag_i2c);  // read first byte of data
    ESP_CHECK(i2c_master_read_byte(cmd, temp_lsb, I2C_MASTER_NACK), g_tag_i2c); // read second byte of data

    ESP_CHECK(i2c_master_stop(cmd), g_tag_i2c);
    ESP_CHECK(i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_PERIOD_MS), g_tag_i2c);
    i2c_cmd_link_delete(cmd);
}


#endif /* MAIN_I2C_DRIVER_H_ */
