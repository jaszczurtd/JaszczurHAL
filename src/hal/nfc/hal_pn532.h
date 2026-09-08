#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_PN532

/**
 * @file hal_pn532.h
 * @brief C facade for PN532 NFC/RFID readers over HAL SPI, I2C, or UART.
 *
 * The application initializes SPI/I2C before creating those transports. A
 * UART transport may either create its UART during hal_pn532_begin() or wrap
 * a caller-owned UART handle. Reader runtime operations are serialized. A
 * successful hal_pn532_begin() is required before every other reader
 * operation.
 */

#include "hal/core/hal_status.h"

#ifdef HAL_ENABLE_UART
#include "hal/serial/hal_uart.h"
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of simultaneously allocated PN532 transports.
 * @note Override with a compile definition in the inclusive range 1..255.
 */
#ifndef HAL_PN532_MAX_TRANSPORTS
#define HAL_PN532_MAX_TRANSPORTS 4u
#endif

/**
 * @brief Maximum number of simultaneously allocated PN532 readers.
 * @note Override with a compile definition in the inclusive range 1..255.
 */
#ifndef HAL_PN532_MAX_READERS
#define HAL_PN532_MAX_READERS 4u
#endif

#if HAL_PN532_MAX_TRANSPORTS < 1u || HAL_PN532_MAX_TRANSPORTS > 255u
#error "HAL_PN532_MAX_TRANSPORTS must be in range 1..255"
#endif

#if HAL_PN532_MAX_READERS < 1u || HAL_PN532_MAX_READERS > 255u
#error "HAL_PN532_MAX_READERS must be in range 1..255"
#endif

/** @brief Sentinel for an optional, unconnected reset pin. */
#define HAL_PN532_PIN_NONE UINT8_MAX

/** @brief Default 7-bit PN532 I2C address. */
#define HAL_PN532_I2C_DEFAULT_ADDRESS 0x24u

/** @brief PN532 SPI clock used by the legacy transport. */
#define HAL_PN532_SPI_DEFAULT_CLOCK_HZ UINT32_C(1000000)

/** @brief Default command timeout in milliseconds. */
#define HAL_PN532_DEFAULT_TIMEOUT_MS UINT16_C(1000)

/** @brief Maximum UID length returned by passive-target discovery. */
#define HAL_PN532_UID_MAX_SIZE 7u

/** @brief MIFARE Classic key size in bytes. */
#define HAL_PN532_MIFARE_KEY_SIZE 6u

/** @brief MIFARE Classic block size in bytes. */
#define HAL_PN532_MIFARE_BLOCK_SIZE 16u

/** @brief MIFARE Ultralight/NTAG page size in bytes. */
#define HAL_PN532_PAGE_SIZE 4u

/** @brief Largest raw PN532 frame accepted by the shared driver. */
#define HAL_PN532_PACKET_BUFFER_SIZE 64u

/**
 * @brief Opaque PN532 transport handle.
 * @details Create it with one transport constructor, attach at most one
 *          reader, then release it with hal_pn532_transport_destroy(). NULL,
 *          stale, and foreign handles are invalid.
 */
typedef struct hal_pn532_transport_impl_s *hal_pn532_transport_t;

/**
 * @brief Opaque PN532 reader handle.
 * @details Create it with hal_pn532_create() and release it with
 *          hal_pn532_destroy(). NULL, stale, and foreign handles are invalid.
 */
typedef struct hal_pn532_impl_s *hal_pn532_t;

/** @brief SPI transport configuration. */
typedef struct {
  uint8_t chip_select_pin; /**< Required active-low chip-select pin. */
  uint8_t reset_pin;       /**< Reset pin or HAL_PN532_PIN_NONE. */
  uint8_t spi_bus;         /**< HAL SPI bus index: 0 or 1. */
} hal_pn532_spi_config_t;

/** @brief I2C transport configuration. */
typedef struct {
  uint8_t reset_pin; /**< Reset pin or HAL_PN532_PIN_NONE. */
  uint8_t address;   /**< 7-bit I2C address. */
  uint8_t i2c_bus;   /**< HAL I2C bus index: 0 or 1. */
} hal_pn532_i2c_config_t;

#ifdef HAL_ENABLE_UART
/** @brief Configuration for a PN532 transport that owns a HAL UART. */
typedef struct {
  hal_uart_port_t port; /**< UART peripheral. */
  uint8_t rx_pin;       /**< UART receive pin. */
  uint8_t tx_pin;       /**< UART transmit pin. */
  uint8_t reset_pin;    /**< Reset pin or HAL_PN532_PIN_NONE. */
} hal_pn532_uart_config_t;
#endif

/** @brief Passive-target modulation supported by the current driver. */
typedef enum {
  HAL_PN532_MODULATION_ISO14443A = 0x00, /**< 106 kbps ISO/IEC 14443A. */
} hal_pn532_modulation_t;

/** @brief MIFARE Classic authentication key selector. */
typedef enum {
  HAL_PN532_KEY_A = 0, /**< Authenticate with sector Key A. */
  HAL_PN532_KEY_B = 1, /**< Authenticate with sector Key B. */
} hal_pn532_key_type_t;

/** @brief UID returned by passive-target discovery. */
typedef struct {
  uint8_t bytes[HAL_PN532_UID_MAX_SIZE]; /**< UID bytes in wire order. */
  uint8_t size; /**< Valid byte count, from 0 to HAL_PN532_UID_MAX_SIZE. */
} hal_pn532_uid_t;

/**
 * @brief Build the default SPI transport descriptor.
 * @param chip_select_pin Active-low chip-select GPIO. The value is copied as
 *        supplied and validated by hal_pn532_transport_create_spi().
 * @return Configuration for bus 0 with no reset pin.
 */
hal_pn532_spi_config_t hal_pn532_spi_default_config(uint8_t chip_select_pin);

/**
 * @brief Build the default I2C transport descriptor.
 * @return Configuration for bus 0, HAL_PN532_I2C_DEFAULT_ADDRESS, and no reset
 *         pin.
 */
hal_pn532_i2c_config_t hal_pn532_i2c_default_config(void);

#ifdef HAL_ENABLE_UART
/**
 * @brief Build a UART transport descriptor.
 * @param port HAL_UART_PORT_1 or HAL_UART_PORT_2.
 * @param rx_pin Required UART receive GPIO.
 * @param tx_pin Required UART transmit GPIO.
 * @return Configuration using the supplied UART wiring and no reset pin.
 * @note Values are validated by hal_pn532_transport_create_uart().
 */
hal_pn532_uart_config_t hal_pn532_uart_default_config(hal_uart_port_t port,
                                                      uint8_t rx_pin,
                                                      uint8_t tx_pin);
#endif

/**
 * @brief Allocate a PN532 SPI transport.
 * @param config Transport configuration. Must not be NULL. Chip-select and
 *        reset must use different GPIOs when reset is connected.
 * @param out_transport Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid configuration, or HAL_ENOMEM on pool
 *         exhaustion.
 */
hal_status_t
hal_pn532_transport_create_spi(const hal_pn532_spi_config_t *config,
                               hal_pn532_transport_t *out_transport);

#ifdef HAL_ENABLE_I2C
/**
 * @brief Allocate a PN532 I2C transport.
 * @param config Transport configuration. Must not be NULL.
 * @param out_transport Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid configuration, or HAL_ENOMEM on pool
 *         exhaustion.
 */
hal_status_t
hal_pn532_transport_create_i2c(const hal_pn532_i2c_config_t *config,
                               hal_pn532_transport_t *out_transport);
#endif

#ifdef HAL_ENABLE_UART
/**
 * @brief Allocate a PN532 UART transport that creates and owns its UART.
 * @param config Transport configuration. Must not be NULL. RX, TX, and a
 *        connected reset signal must use distinct GPIOs.
 * @param out_transport Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid configuration, or HAL_ENOMEM on pool
 *         exhaustion.
 */
hal_status_t
hal_pn532_transport_create_uart(const hal_pn532_uart_config_t *config,
                                hal_pn532_transport_t *out_transport);

/**
 * @brief Allocate a PN532 transport over a caller-owned UART handle.
 * @param uart Existing HAL UART; it is not destroyed with the transport and
 *        must remain valid for the transport's entire lifetime.
 * @param reset_pin Reset GPIO or HAL_PN532_PIN_NONE.
 * @param out_transport Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or HAL_ENOMEM on pool
 *         exhaustion.
 * @note Do not reconfigure or destroy @p uart while the transport exists.
 */
hal_status_t
hal_pn532_transport_create_uart_handle(hal_uart_t uart, uint8_t reset_pin,
                                       hal_pn532_transport_t *out_transport);
#endif

/**
 * @brief Release a PN532 transport.
 * @param transport Handle returned by a transport create function. NULL and
 *        stale handles are invalid.
 * @return HAL_OK, HAL_EINVAL for a stale/invalid handle, or HAL_EBUSY while a
 *         reader remains attached.
 */
hal_status_t hal_pn532_transport_destroy(hal_pn532_transport_t transport);

/**
 * @brief Allocate a PN532 reader attached to one transport.
 * @param transport Valid, unattached transport handle.
 * @param out_reader Receives a reader handle and is cleared on error.
 * @return HAL_OK, HAL_EINVAL for bad arguments, HAL_EBUSY when attached, or
 *         HAL_ENOMEM on pool exhaustion.
 */
hal_status_t hal_pn532_create(hal_pn532_transport_t transport,
                              hal_pn532_t *out_reader);

/**
 * @brief Release a reader and detach its transport.
 * @param reader Handle returned by hal_pn532_create(). NULL and stale handles
 *        are invalid.
 * @return HAL_OK or HAL_EINVAL for an invalid handle.
 */
hal_status_t hal_pn532_destroy(hal_pn532_t reader);

/**
 * @brief Initialize the selected transport.
 * @param reader Valid reader handle; must not be NULL. This is the only
 *        reader operation permitted before initialization and may be retried.
 *        Any failed attempt leaves the reader uninitialized.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, HAL_ENOMEM when an owned
 *         UART cannot be created, or a propagated transport status.
 */
hal_status_t hal_pn532_begin(hal_pn532_t reader);

/**
 * @brief Wake a PN532 that is in a low-power state.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or a propagated transport
 *         status.
 */
hal_status_t hal_pn532_wakeup(hal_pn532_t reader);

/**
 * @brief Configure the Secure Access Module for normal reader mode.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, HAL_ETIMEOUT, HAL_EPROTO,
 *         or a propagated transport status.
 */
hal_status_t hal_pn532_sam_configure(hal_pn532_t reader);

/**
 * @brief Read the IC/version/revision/support response.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param out_version Receives the four-byte big-endian response as a host
 *        integer; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_ETIMEOUT, HAL_EPROTO,
 *         or a propagated transport status.
 */
hal_status_t hal_pn532_get_firmware_version(hal_pn532_t reader,
                                            uint32_t *out_version);

/**
 * @brief Send a raw PN532 command frame and verify its ACK.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param command Command bytes without PN532 framing.
 * @param command_size Number of command bytes; must be in the inclusive range
 *        1..56. @p command must not be NULL.
 * @param timeout_ms ACK timeout in milliseconds.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_ETIMEOUT, HAL_EPROTO,
 *         or a propagated transport status.
 */
hal_status_t hal_pn532_send_command_check_ack(hal_pn532_t reader,
                                              const uint8_t *command,
                                              size_t command_size,
                                              uint16_t timeout_ms);

/**
 * @brief Discover one passive target and return its UID.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param modulation HAL_PN532_MODULATION_ISO14443A; other values are rejected.
 * @param timeout_ms Discovery timeout in milliseconds.
 * @param out_uid Receives the UID and length. Must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_ENOENT when no card
 *         responds, HAL_ETIMEOUT, HAL_EPROTO, or a transport status.
 */
hal_status_t hal_pn532_read_passive_target(hal_pn532_t reader,
                                           hal_pn532_modulation_t modulation,
                                           uint16_t timeout_ms,
                                           hal_pn532_uid_t *out_uid);

/**
 * @brief Read the raw passive-target response.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param response Destination buffer; must not be NULL.
 * @param response_size Destination capacity in bytes; must be nonzero. At
 *        most HAL_PN532_PACKET_BUFFER_SIZE - 9 payload bytes can be returned.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_EOVERFLOW when the
 *         response does not fit, HAL_ETIMEOUT, HAL_EPROTO, or a transport
 *         status.
 */
hal_status_t hal_pn532_list_passive_target(hal_pn532_t reader,
                                           uint8_t *response,
                                           size_t response_size);

/**
 * @brief Exchange data with the currently selected target.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param request Request bytes. Must not be NULL or empty.
 * @param request_size Request length in the inclusive range 1..54.
 * @param response Destination response buffer; must not be NULL.
 * @param inout_response_size Must not be NULL. On input, response capacity;
 *        on success, received payload length. Capacities above 54 bytes are
 *        accepted, but the driver cannot return more than 54 payload bytes.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_EOVERFLOW when a
 *         buffer is too small, HAL_ETIMEOUT, HAL_EPROTO, or a transport status.
 */
hal_status_t hal_pn532_data_exchange(hal_pn532_t reader, const uint8_t *request,
                                     size_t request_size, uint8_t *response,
                                     size_t *inout_response_size);

/**
 * @brief Authenticate a MIFARE Classic block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param uid Card UID returned by passive-target discovery; must not be NULL
 *        and size must be 4 or HAL_PN532_UID_MAX_SIZE bytes.
 * @param block Block number from 0 to 255.
 * @param key_type Select Key A or Key B.
 * @param key_data Source of HAL_PN532_MIFARE_KEY_SIZE bytes; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_ETIMEOUT, HAL_EPROTO
 *         when the card rejects authentication, or a transport status.
 */
hal_status_t hal_pn532_mifare_classic_authenticate(
    hal_pn532_t reader, const hal_pn532_uid_t *uid, uint8_t block,
    hal_pn532_key_type_t key_type,
    const uint8_t key_data[HAL_PN532_MIFARE_KEY_SIZE]);

/**
 * @brief Read one MIFARE Classic block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param data Destination for HAL_PN532_MIFARE_BLOCK_SIZE bytes; must not be
 *        NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or an authentication,
 *         timeout, protocol, overflow, or transport status.
 */
hal_status_t
hal_pn532_mifare_classic_read(hal_pn532_t reader, uint8_t block,
                              uint8_t data[HAL_PN532_MIFARE_BLOCK_SIZE]);

/**
 * @brief Write one MIFARE Classic block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param data Source of HAL_PN532_MIFARE_BLOCK_SIZE bytes; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or an authentication,
 *         timeout, protocol, overflow, or transport status.
 */
hal_status_t
hal_pn532_mifare_classic_write(hal_pn532_t reader, uint8_t block,
                               const uint8_t data[HAL_PN532_MIFARE_BLOCK_SIZE]);

/**
 * @brief Read one MIFARE Ultralight page.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param page Page number in the inclusive range 0..255.
 * @param data Destination for HAL_PN532_PAGE_SIZE bytes; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or an authentication,
 *         timeout, protocol, overflow, or transport status.
 */
hal_status_t
hal_pn532_mifare_ultralight_read(hal_pn532_t reader, uint8_t page,
                                 uint8_t data[HAL_PN532_PAGE_SIZE]);

/**
 * @brief Write one MIFARE Ultralight page.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param page Page number in the inclusive range 0..255.
 * @param data Source of HAL_PN532_PAGE_SIZE bytes; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or an authentication,
 *         timeout, protocol, overflow, or transport status.
 */
hal_status_t
hal_pn532_mifare_ultralight_write(hal_pn532_t reader, uint8_t page,
                                  const uint8_t data[HAL_PN532_PAGE_SIZE]);

/**
 * @brief Write one NTAG2xx page.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_pn532_begin(), this operation returns HAL_ESTATE.
 * @param page Page number in the inclusive range 0..255.
 * @param data Source of HAL_PN532_PAGE_SIZE bytes; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or an authentication,
 *         timeout, protocol, overflow, or transport status.
 */
hal_status_t hal_pn532_ntag2xx_write(hal_pn532_t reader, uint8_t page,
                                     const uint8_t data[HAL_PN532_PAGE_SIZE]);

/**
 * @brief Validate a raw PN532 response frame.
 * @param frame Complete response frame; must not be NULL.
 * @param frame_size Available bytes. At least five bytes are required to read
 *        the header; a valid normal response occupies LEN + 7 bytes and is at
 *        least nine bytes long.
 * @param command Command byte whose response is expected.
 * @return HAL_OK, HAL_EINVAL for NULL or fewer than five bytes, HAL_EOVERFLOW
 * for a truncated declared frame, or HAL_EPROTO for invalid framing, response
 * command, checksum, or postamble.
 */
hal_status_t hal_pn532_check_response_frame(const uint8_t *frame,
                                            size_t frame_size, uint8_t command);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_PN532 */

#ifdef __cplusplus
/* Existing class API remains available to C++ consumers, with or without the
 * facade feature flag, matching the historical direct-include behavior. */
#include "hal/nfc/pn532/pn532.h"
#endif
