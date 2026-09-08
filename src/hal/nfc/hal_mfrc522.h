#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_MFRC522

/**
 * @file hal_mfrc522.h
 * @brief C facade for the MFRC522 RFID reader over HAL SPI or I2C.
 *
 * The application initializes the selected bus before creating a transport.
 * Reader operations are serialized by the underlying driver. A successful
 * hal_mfrc522_begin() is required before every other reader operation.
 * Transport and reader creation/destruction are single-owner lifecycle
 * operations.
 */

#include "hal/core/hal_status.h"
#include "hal/spi/hal_spi_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of simultaneously allocated MFRC522 transports.
 * @note Override with a compile definition in the inclusive range 1..255.
 */
#ifndef HAL_MFRC522_MAX_TRANSPORTS
#define HAL_MFRC522_MAX_TRANSPORTS 4u
#endif

/**
 * @brief Maximum number of simultaneously allocated MFRC522 readers.
 * @note Override with a compile definition in the inclusive range 1..255.
 */
#ifndef HAL_MFRC522_MAX_READERS
#define HAL_MFRC522_MAX_READERS 4u
#endif

#if HAL_MFRC522_MAX_TRANSPORTS < 1u || HAL_MFRC522_MAX_TRANSPORTS > 255u
#error "HAL_MFRC522_MAX_TRANSPORTS must be in range 1..255"
#endif

#if HAL_MFRC522_MAX_READERS < 1u || HAL_MFRC522_MAX_READERS > 255u
#error "HAL_MFRC522_MAX_READERS must be in range 1..255"
#endif

/** @brief Sentinel for an optional, unconnected reset/power-down pin. */
#define HAL_MFRC522_PIN_NONE UINT8_MAX

/** @brief Default 7-bit I2C address used by MFRC522 modules. */
#define HAL_MFRC522_I2C_DEFAULT_ADDRESS 0x28u

/** @brief Maximum ISO/IEC 14443A UID length supported by the driver. */
#define HAL_MFRC522_UID_MAX_SIZE 10u

/** @brief MIFARE Classic Crypto1 key length in bytes. */
#define HAL_MFRC522_MIFARE_KEY_SIZE 6u

/** @brief MIFARE Classic data block length in bytes. */
#define HAL_MFRC522_MIFARE_BLOCK_SIZE 16u

/** @brief MIFARE Ultralight page length in bytes. */
#define HAL_MFRC522_ULTRALIGHT_PAGE_SIZE 4u

/** @brief Encoded MIFARE Classic sector access field length in bytes. */
#define HAL_MFRC522_ACCESS_BITS_SIZE 3u

/**
 * @brief Opaque MFRC522 bus transport handle.
 * @details Create it with one transport constructor, attach at most one
 *          reader, then release it with hal_mfrc522_transport_destroy(). NULL,
 *          stale, and foreign handles are invalid.
 */
typedef struct hal_mfrc522_transport_impl_s *hal_mfrc522_transport_t;

/**
 * @brief Opaque MFRC522 reader handle.
 * @details Create it with hal_mfrc522_create() and release it with
 *          hal_mfrc522_destroy(). NULL, stale, and foreign handles are invalid.
 */
typedef struct hal_mfrc522_impl_s *hal_mfrc522_t;

/** @brief SPI transport configuration. */
typedef struct {
  uint8_t chip_select_pin; /**< Required active-low chip-select pin. */
  uint8_t reset_pin;       /**< Reset/power-down pin or HAL_MFRC522_PIN_NONE. */
  uint8_t spi_bus;         /**< HAL SPI bus index: 0 or 1. */
  hal_spi_settings_t settings; /**< SPI clock, bit order, and mode. */
} hal_mfrc522_spi_config_t;

/** @brief I2C transport configuration. */
typedef struct {
  uint8_t reset_pin; /**< Reset/power-down pin or HAL_MFRC522_PIN_NONE. */
  uint8_t address;   /**< 7-bit I2C address. */
  uint8_t i2c_bus;   /**< HAL I2C bus index: 0 or 1. */
} hal_mfrc522_i2c_config_t;

/** @brief UID and SAK returned after card selection. */
typedef struct {
  uint8_t bytes[HAL_MFRC522_UID_MAX_SIZE]; /**< UID bytes in wire order. */
  uint8_t size; /**< Valid byte count, from 0 to HAL_MFRC522_UID_MAX_SIZE. */
  uint8_t sak;  /**< Select acknowledge byte returned by the card. */
} hal_mfrc522_uid_t;

/** @brief MIFARE Classic Crypto1 key. */
typedef struct {
  uint8_t bytes[HAL_MFRC522_MIFARE_KEY_SIZE]; /**< Six key bytes. */
} hal_mfrc522_mifare_key_t;

/** @brief Key selector used for MIFARE Classic authentication. */
typedef enum {
  HAL_MFRC522_KEY_A = 0, /**< Authenticate with sector Key A. */
  HAL_MFRC522_KEY_B = 1, /**< Authenticate with sector Key B. */
} hal_mfrc522_key_type_t;

/** @brief Card families derived from a card SAK value. */
typedef enum {
  HAL_MFRC522_CARD_UNKNOWN = 0,           /**< Unrecognized card family. */
  HAL_MFRC522_CARD_ISO_14443_4,           /**< ISO/IEC 14443-4 card. */
  HAL_MFRC522_CARD_ISO_18092,             /**< ISO/IEC 18092 NFC target. */
  HAL_MFRC522_CARD_MIFARE_MINI,           /**< MIFARE Mini. */
  HAL_MFRC522_CARD_MIFARE_1K,             /**< MIFARE Classic 1K. */
  HAL_MFRC522_CARD_MIFARE_4K,             /**< MIFARE Classic 4K. */
  HAL_MFRC522_CARD_MIFARE_ULTRALIGHT,     /**< MIFARE Ultralight family. */
  HAL_MFRC522_CARD_MIFARE_PLUS,           /**< MIFARE Plus. */
  HAL_MFRC522_CARD_MIFARE_DESFIRE,        /**< MIFARE DESFire. */
  HAL_MFRC522_CARD_TNP3XXX,               /**< Innovision/Jewel family. */
  HAL_MFRC522_CARD_UID_INCOMPLETE = 0xff, /**< More UID levels are required. */
} hal_mfrc522_card_type_t;

/**
 * @brief Build a mode-0, MSB-first SPI descriptor on bus 0.
 * @param chip_select_pin Active-low chip-select GPIO. The value is copied as
 *        supplied and is validated by hal_mfrc522_transport_create_spi().
 * @return Configuration using the default HAL SPI clock and no reset pin.
 */
hal_mfrc522_spi_config_t
hal_mfrc522_spi_default_config(uint8_t chip_select_pin);

/**
 * @brief Build the default I2C descriptor.
 * @return Configuration for bus 0, HAL_MFRC522_I2C_DEFAULT_ADDRESS, and no
 *         reset pin.
 */
hal_mfrc522_i2c_config_t hal_mfrc522_i2c_default_config(void);

/**
 * @brief Allocate an MFRC522 SPI transport.
 * @param config Transport configuration. Must not be NULL. Chip-select and
 *        reset must use different GPIOs when reset is connected.
 * @param out_transport Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid configuration, or HAL_ENOMEM when the
 *         static pool is exhausted.
 */
hal_status_t
hal_mfrc522_transport_create_spi(const hal_mfrc522_spi_config_t *config,
                                 hal_mfrc522_transport_t *out_transport);

#ifdef HAL_ENABLE_I2C
/**
 * @brief Allocate an MFRC522 I2C transport.
 * @param config Transport configuration. Must not be NULL.
 * @param out_transport Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid configuration, or HAL_ENOMEM when the
 *         static pool is exhausted.
 */
hal_status_t
hal_mfrc522_transport_create_i2c(const hal_mfrc522_i2c_config_t *config,
                                 hal_mfrc522_transport_t *out_transport);
#endif

/**
 * @brief Release a transport.
 * @param transport Handle returned by a transport create function. NULL and
 *        stale handles are invalid.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or HAL_EBUSY while a
 *         reader still uses the transport.
 */
hal_status_t hal_mfrc522_transport_destroy(hal_mfrc522_transport_t transport);

/**
 * @brief Allocate a reader attached to an existing transport.
 * @param transport Valid transport handle. One reader may own a transport.
 * @param out_reader Receives a handle on success and NULL on error.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_EBUSY when the
 *         transport is already attached, or HAL_ENOMEM on pool exhaustion.
 */
hal_status_t hal_mfrc522_create(hal_mfrc522_transport_t transport,
                                hal_mfrc522_t *out_reader);

/**
 * @brief Release a reader and detach its transport.
 * @param reader Handle returned by hal_mfrc522_create(). NULL and stale
 *        handles are invalid.
 * @return HAL_OK or HAL_EINVAL for an invalid handle.
 */
hal_status_t hal_mfrc522_destroy(hal_mfrc522_t reader);

/**
 * @brief Initialize the transport and MFRC522 chip.
 * @param reader Valid reader handle; must not be NULL. This is the only
 *        reader operation permitted before initialization and may be retried.
 *        Any failed attempt leaves the reader uninitialized.
 * @return HAL_OK when a supported chip version responds, HAL_ENOENT for
 *         version 0x00/0xff, HAL_EINVAL for an invalid handle, HAL_ENOMEM when
 *         internal synchronization cannot be initialized, or a transport
 *         status.
 */
hal_status_t hal_mfrc522_begin(hal_mfrc522_t reader);

/**
 * @brief Read the raw MFRC522 version register.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_version Output byte. Must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a transport status.
 */
hal_status_t hal_mfrc522_get_version(hal_mfrc522_t reader,
                                     uint8_t *out_version);

/**
 * @brief Enable the RF antenna driver.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for a NULL, stale, or foreign handle, or a
 *         transport status.
 */
hal_status_t hal_mfrc522_antenna_on(hal_mfrc522_t reader);

/**
 * @brief Disable the RF antenna driver.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for a NULL, stale, or foreign handle, or a
 *         transport status.
 */
hal_status_t hal_mfrc522_antenna_off(hal_mfrc522_t reader);

/**
 * @brief Read the receiver gain mask.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_gain Receives RFCfgReg bits 6..4; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a transport status.
 */
hal_status_t hal_mfrc522_get_antenna_gain(hal_mfrc522_t reader,
                                          uint8_t *out_gain);

/**
 * @brief Set the receiver gain mask.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param gain RFCfgReg gain mask. Only bits 6..4 are applied.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or a transport status.
 */
hal_status_t hal_mfrc522_set_antenna_gain(hal_mfrc522_t reader, uint8_t gain);

/**
 * @brief Run the MFRC522 digital self-test.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_passed Receives whether the chip output matched its firmware
 *        reference pattern; must not be NULL.
 * @return HAL_OK when the test completed, including a failed test result,
 *         HAL_EINVAL for invalid arguments, or a transport status.
 */
hal_status_t hal_mfrc522_perform_self_test(hal_mfrc522_t reader,
                                           bool *out_passed);

/**
 * @brief Enter MFRC522 soft power-down.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or a transport status.
 */
hal_status_t hal_mfrc522_power_down(hal_mfrc522_t reader);

/**
 * @brief Leave MFRC522 soft power-down.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or a transport status.
 */
hal_status_t hal_mfrc522_power_up(hal_mfrc522_t reader);

/**
 * @brief Check whether a new card responds to REQA.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_present Receives true for a response or collision; must not be
 *        NULL. A timeout meaning no card produces false with HAL_OK.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or another mapped
 *         protocol/communication status.
 */
hal_status_t hal_mfrc522_is_new_card_present(hal_mfrc522_t reader,
                                             bool *out_present);

/**
 * @brief Select the current card and read its UID.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_uid Receives up to HAL_MFRC522_UID_MAX_SIZE UID bytes and SAK;
 *        must not be NULL and is cleared on protocol failure.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_EINTERNAL for an
 *         invalid UID response, or another mapped protocol/transport status.
 */
hal_status_t hal_mfrc522_read_uid(hal_mfrc522_t reader,
                                  hal_mfrc522_uid_t *out_uid);

/**
 * @brief Send an ISO/IEC 14443A REQA command.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_atqa Two-byte ATQA destination; must not be NULL.
 * @return HAL_OK or a mapped collision/timeout/protocol/transport status.
 */
hal_status_t hal_mfrc522_request_a(hal_mfrc522_t reader, uint8_t out_atqa[2]);

/**
 * @brief Send an ISO/IEC 14443A WUPA command.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_atqa Two-byte ATQA destination; must not be NULL.
 * @return HAL_OK or a mapped collision/timeout/protocol/transport status.
 */
hal_status_t hal_mfrc522_wakeup_a(hal_mfrc522_t reader, uint8_t out_atqa[2]);

/**
 * @brief Put the selected card in HALT state.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK or a mapped timeout/protocol/transport status.
 */
hal_status_t hal_mfrc522_halt(hal_mfrc522_t reader);

/**
 * @brief Authenticate a MIFARE Classic block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param key_type Select Key A or Key B.
 * @param block Block number in the inclusive range 0..255.
 * @param key Six-byte Crypto1 key; must not be NULL.
 * @param uid UID returned by hal_mfrc522_read_uid(); must not be NULL and its
 *        size must be 4, 7, or 10 bytes.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a mapped
 *         authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_authenticate(
    hal_mfrc522_t reader, hal_mfrc522_key_type_t key_type, uint8_t block,
    const hal_mfrc522_mifare_key_t *key, const hal_mfrc522_uid_t *uid);

/**
 * @brief Stop Crypto1 on the selected card.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or a transport status.
 */
hal_status_t hal_mfrc522_mifare_stop_crypto(hal_mfrc522_t reader);

/**
 * @brief Read a MIFARE block into a caller buffer.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param buffer Destination buffer; must not be NULL.
 * @param inout_size Must not be NULL. On input, capacity in bytes in the
 *        inclusive range 1..255; on return, the size reported by the driver.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_EOVERFLOW when the
 *         buffer is too small, or another mapped communication status.
 */
hal_status_t hal_mfrc522_mifare_read(hal_mfrc522_t reader, uint8_t block,
                                     uint8_t *buffer, size_t *inout_size);

/**
 * @brief Write one MIFARE Classic block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param data Source of exactly HAL_MFRC522_MIFARE_BLOCK_SIZE bytes; must not
 *        be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a mapped
 *         authentication/communication status.
 */
hal_status_t
hal_mfrc522_mifare_write(hal_mfrc522_t reader, uint8_t block,
                         const uint8_t data[HAL_MFRC522_MIFARE_BLOCK_SIZE]);

/**
 * @brief Write one MIFARE Ultralight page.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param page Page number in the inclusive range 0..255.
 * @param data Source of exactly HAL_MFRC522_ULTRALIGHT_PAGE_SIZE bytes; must
 *        not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a mapped
 *         authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_ultralight_write(
    hal_mfrc522_t reader, uint8_t page,
    const uint8_t data[HAL_MFRC522_ULTRALIGHT_PAGE_SIZE]);

/**
 * @brief Decrement a MIFARE value block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param delta Signed 32-bit quantity passed to the card.
 * @return HAL_OK or a mapped validation/authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_decrement(hal_mfrc522_t reader, uint8_t block,
                                          int32_t delta);

/**
 * @brief Increment a MIFARE value block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param delta Signed 32-bit quantity passed to the card.
 * @return HAL_OK or a mapped validation/authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_increment(hal_mfrc522_t reader, uint8_t block,
                                          int32_t delta);

/**
 * @brief Load a value block into the card transfer buffer.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @return HAL_OK or a mapped authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_restore(hal_mfrc522_t reader, uint8_t block);

/**
 * @brief Store the card transfer buffer into a value block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @return HAL_OK or a mapped authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_transfer(hal_mfrc522_t reader, uint8_t block);

/**
 * @brief Read a signed MIFARE value block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param out_value Destination value; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a mapped
 *         authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_get_value(hal_mfrc522_t reader, uint8_t block,
                                          int32_t *out_value);

/**
 * @brief Encode and write a signed MIFARE value block.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param block Block number in the inclusive range 0..255.
 * @param value Signed 32-bit value to encode.
 * @return HAL_OK or a mapped validation/authentication/communication status.
 */
hal_status_t hal_mfrc522_mifare_set_value(hal_mfrc522_t reader, uint8_t block,
                                          int32_t value);

/**
 * @brief Authenticate an NTAG216 card.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param password Source of exactly four password bytes; must not be NULL.
 * @param out_pack Two-byte password acknowledgement destination; must not be
 *        NULL.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, HAL_EAUTH on NAK, or
 *         another mapped communication status.
 */
hal_status_t hal_mfrc522_ntag216_authenticate(hal_mfrc522_t reader,
                                              const uint8_t password[4],
                                              uint8_t out_pack[2]);

/**
 * @brief Calculate MIFARE Classic sector access bytes.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param out_access Destination of HAL_MFRC522_ACCESS_BITS_SIZE encoded bytes;
 *        must not be NULL.
 * @param g0 Access condition for data block 0, in the range 0..7.
 * @param g1 Access condition for data block 1, in the range 0..7.
 * @param g2 Access condition for data block 2, in the range 0..7.
 * @param g3 Access condition for the sector trailer, in the range 0..7.
 * @return HAL_OK or HAL_EINVAL for invalid arguments.
 */
hal_status_t hal_mfrc522_mifare_set_access_bits(
    hal_mfrc522_t reader, uint8_t out_access[HAL_MFRC522_ACCESS_BITS_SIZE],
    uint8_t g0, uint8_t g1, uint8_t g2, uint8_t g3);

/**
 * @brief Open the UID backdoor on a compatible changeable-UID card.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param log_errors Enable legacy driver diagnostics when true.
 * @return HAL_OK when opened, HAL_EIO when rejected, HAL_EINVAL for an
 *         invalid handle, or a more specific transport status.
 */
hal_status_t hal_mfrc522_mifare_open_uid_backdoor(hal_mfrc522_t reader,
                                                  bool log_errors);

/**
 * @brief Replace the UID on a compatible changeable-UID card.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param uid New UID; must not be NULL and size must be
 *        1..HAL_MFRC522_UID_MAX_SIZE. The SAK field is ignored.
 * @param log_errors Enable legacy driver diagnostics when true.
 * @return HAL_OK when written, HAL_ESTATE when no card with a 4-, 7-, or
 *         10-byte UID was previously selected, HAL_EIO when rejected,
 *         HAL_EINVAL for invalid arguments, or a more specific transport
 *         status.
 */
hal_status_t hal_mfrc522_mifare_set_uid(hal_mfrc522_t reader,
                                        const hal_mfrc522_uid_t *uid,
                                        bool log_errors);

/**
 * @brief Restore sector zero on a compatible changeable-UID card.
 * @param reader Initialized reader handle; must not be NULL. Before a
 *        successful hal_mfrc522_begin(), this operation returns HAL_ESTATE.
 * @param log_errors Enable legacy driver diagnostics when true.
 * @return HAL_OK when restored, HAL_EIO when rejected, HAL_EINVAL for an
 *         invalid handle, or a more specific transport status.
 */
hal_status_t hal_mfrc522_mifare_unbrick_uid_sector(hal_mfrc522_t reader,
                                                   bool log_errors);

/**
 * @brief Classify a card from its select acknowledge byte.
 * @param sak Raw SAK returned by card selection.
 * @return A portable card family; unknown values map to
 *         HAL_MFRC522_CARD_UNKNOWN.
 */
hal_mfrc522_card_type_t hal_mfrc522_card_type_from_sak(uint8_t sak);

/**
 * @brief Return an English name for a card family.
 * @param type Card family, including values outside the defined enum.
 * @return Static, read-only text; never NULL. Values outside the defined enum
 *         return the same text as HAL_MFRC522_CARD_UNKNOWN.
 */
const char *hal_mfrc522_card_type_name(hal_mfrc522_card_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_MFRC522 */

#ifdef __cplusplus
/* Existing class API remains available to C++ consumers, with or without the
 * facade feature flag, matching the historical direct-include behavior. */
#include "hal/nfc/mfrc522/mfrc522.h"
#endif
