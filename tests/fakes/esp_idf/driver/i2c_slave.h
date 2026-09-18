#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct fake_i2c_slave_dev *i2c_slave_dev_handle_t;
typedef int i2c_port_num_t;
typedef enum { I2C_CLK_SRC_DEFAULT = 0 } i2c_clock_source_t;
typedef enum { I2C_ADDR_BIT_LEN_7 = 0 } i2c_addr_bit_len_t;

typedef struct {
  i2c_port_num_t i2c_port;
  gpio_num_t sda_io_num;
  gpio_num_t scl_io_num;
  i2c_clock_source_t clk_source;
  uint32_t send_buf_depth;
  uint32_t receive_buf_depth;
  uint16_t slave_addr;
  i2c_addr_bit_len_t addr_bit_len;
  struct {
    uint32_t enable_internal_pullup : 1;
  } flags;
} i2c_slave_config_t;

typedef struct {
  uint8_t *buffer;
  uint32_t length;
} i2c_slave_rx_done_event_data_t;

typedef struct {
  int unused;
} i2c_slave_request_event_data_t;

typedef bool (*i2c_slave_received_callback_t)(
    i2c_slave_dev_handle_t, const i2c_slave_rx_done_event_data_t *, void *);
typedef bool (*i2c_slave_request_callback_t)(
    i2c_slave_dev_handle_t, const i2c_slave_request_event_data_t *, void *);

typedef struct {
  i2c_slave_request_callback_t on_request;
  i2c_slave_received_callback_t on_receive;
} i2c_slave_event_callbacks_t;

esp_err_t i2c_new_slave_device(const i2c_slave_config_t *config,
                               i2c_slave_dev_handle_t *ret_handle);
esp_err_t i2c_del_slave_device(i2c_slave_dev_handle_t handle);
esp_err_t
i2c_slave_register_event_callbacks(i2c_slave_dev_handle_t handle,
                                   const i2c_slave_event_callbacks_t *cbs,
                                   void *user_data);
esp_err_t i2c_slave_write(i2c_slave_dev_handle_t handle, const uint8_t *data,
                          uint32_t len, uint32_t *write_len, int timeout_ms);
esp_err_t i2c_slave_reset_tx_fifo(i2c_slave_dev_handle_t handle);
