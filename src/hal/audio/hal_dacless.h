#pragma once

/**
 * @file hal_dacless.h
 * @brief C interface for the DACless PWM-audio driver.
 *
 * The C interface uses opaque handles backed by a static instance pool. C++
 * applications that include this header retain access to the legacy
 * DAClessAudio class.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_DACLESS)

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Largest supported sample block, in 16-bit samples.
 * @note A build-time override must be in the range 1..32767.
 */
#ifndef DACLESS_MAX_BLOCK_SIZE
#define DACLESS_MAX_BLOCK_SIZE 512u
#endif

/**
 * @brief Largest number of ADC inputs sampled with the audio stream.
 * @note A build-time override must be in the range 4..255.
 */
#ifndef DACLESS_MAX_ADC_INPUTS
#define DACLESS_MAX_ADC_INPUTS 4u
#endif

/** @brief Largest ADC scan supported by the DMA backends. */
#define DACLESS_MAX_DMA_ADC_INPUTS 4u

/**
 * @brief Maximum number of simultaneous DACless instances.
 * @note A build-time override must be in the range 1..127.
 */
#ifndef DACLESS_MAX_INSTANCES
#define DACLESS_MAX_INSTANCES 4u
#endif

/**
 * @brief Maximum polling samples generated during one catch-up pass.
 * @note A build-time override must be at least 1.
 */
#ifndef DACLESS_MAX_POLLING_CATCHUP_SAMPLES
#define DACLESS_MAX_POLLING_CATCHUP_SAMPLES 64u
#endif

#if DACLESS_MAX_BLOCK_SIZE < 1u || DACLESS_MAX_BLOCK_SIZE > 32767u
#error "DACLESS_MAX_BLOCK_SIZE must be in the range 1..32767"
#endif

#if DACLESS_MAX_ADC_INPUTS < 4u || DACLESS_MAX_ADC_INPUTS > 255u
#error "DACLESS_MAX_ADC_INPUTS must be in the range 4..255"
#endif

#if DACLESS_MAX_INSTANCES < 1u || DACLESS_MAX_INSTANCES > 127u
#error "DACLESS_MAX_INSTANCES must be in the range 1..127"
#endif

#if DACLESS_MAX_POLLING_CATCHUP_SAMPLES < 1u
#error "DACLESS_MAX_POLLING_CATCHUP_SAMPLES must be at least 1"
#endif

/** @brief Default PWM output pin. */
#ifndef DACLESS_DEFAULT_PWM_PIN
#define DACLESS_DEFAULT_PWM_PIN 6u
#endif

#if HAL_TARGET_IS_ESP32_FAMILY
/** @brief Default first ADC input pin on ESP32; ADC1 starts at GPIO1. */
#define DACLESS_DEFAULT_ADC0_PIN 1u
/** @brief Default second ADC input pin on ESP32. */
#define DACLESS_DEFAULT_ADC1_PIN 2u
/** @brief Default third ADC input pin on ESP32. */
#define DACLESS_DEFAULT_ADC2_PIN 3u
/** @brief Default fourth ADC input pin on ESP32. */
#define DACLESS_DEFAULT_ADC3_PIN 4u
#elif HAL_TARGET_IS_STM32G474
/** @brief Default first ADC input pin on STM32G474. */
#define DACLESS_DEFAULT_ADC0_PIN 0u
/** @brief Default second ADC input pin on STM32G474. */
#define DACLESS_DEFAULT_ADC1_PIN 1u
/** @brief Default third ADC input pin on STM32G474. */
#define DACLESS_DEFAULT_ADC2_PIN 2u
/** @brief Default fourth ADC input pin on STM32G474. */
#define DACLESS_DEFAULT_ADC3_PIN 3u
#else
/** @brief Default first ADC input pin on RP and mock targets. */
#define DACLESS_DEFAULT_ADC0_PIN 26u
/** @brief Default second ADC input pin on RP and mock targets. */
#define DACLESS_DEFAULT_ADC1_PIN 27u
/** @brief Default third ADC input pin on RP and mock targets. */
#define DACLESS_DEFAULT_ADC2_PIN 28u
/** @brief Default fourth ADC input pin on RP and mock targets. */
#define DACLESS_DEFAULT_ADC3_PIN 29u
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque DACless implementation type. */
typedef struct hal_dacless_impl_s hal_dacless_impl_t;

/** @brief Handle to one DACless audio instance. */
typedef hal_dacless_impl_t *hal_dacless_t;

/**
 * @brief Generate one unsigned PWM sample.
 * @param context Application context supplied when the callback is installed.
 * @return Sample value. Values above the configured PWM range are clamped.
 * @note In DMA mode this callback runs in interrupt context; in polling mode
 *       it runs synchronously inside @ref hal_dacless_service. It must not
 *       block or call DACless functions other than @ref hal_dacless_get_adc.
 */
typedef uint16_t (*hal_dacless_sample_callback_t)(void *context);

/**
 * @brief Fill one output block.
 * @param context Application context supplied when the callback is installed.
 * @param buffer Writable output buffer owned by the driver. Must not be kept
 *        after the callback returns.
 * @param sample_count Number of 16-bit samples available in @p buffer.
 * @note In DMA mode this callback runs in interrupt context; in polling mode
 *       it runs synchronously inside @ref hal_dacless_service. It must not
 *       block or call DACless functions other than @ref hal_dacless_get_adc.
 */
typedef void (*hal_dacless_block_callback_t)(void *context, uint16_t *buffer,
                                             uint16_t sample_count);

/** @brief DACless instance configuration. */
typedef struct {
  uint8_t pwm_pin;         /**< PWM output pin. */
  uint16_t pwm_bits;       /**< PWM resolution from 1 to 16 bits. */
  uint16_t block_size;     /**< Samples per buffer, from 1 to
                                DACLESS_MAX_BLOCK_SIZE. */
  uint8_t adc_input_count; /**< Number of entries used in @ref adc_pins. */
  bool use_dma; /**< true for DMA double buffering, false for polling. */
  uint8_t adc_pins[DACLESS_MAX_ADC_INPUTS]; /**< ADC input pins. */
} hal_dacless_config_t;

/** @brief Snapshot of the current DACless lifecycle and output state. */
typedef struct {
  bool started;    /**< Most recent begin operation completed successfully. */
  bool muted;      /**< Output is currently muted. */
  bool running;    /**< Output is started and not muted. */
  bool dma_active; /**< Output uses the DMA backend. */
} hal_dacless_state_t;

/**
 * @brief Return the default DACless configuration.
 * @return Configuration using 12-bit DMA output, 128-sample blocks and four
 *         default ADC pins.
 */
hal_dacless_config_t hal_dacless_default_config(void);

/**
 * @brief Fill a DACless configuration with defaults.
 * @param config Destination configuration. Must not be NULL.
 * @return HAL_OK on success or HAL_EINVAL when @p config is NULL.
 */
hal_status_t hal_dacless_config_init(hal_dacless_config_t *config);

/**
 * @brief Construct a DACless instance in the static pool.
 *
 * Values outside the supported PWM, block and ADC-count storage ranges are
 * clamped in the same way as the legacy C++ class. DMA mode accepts at most
 * DACLESS_MAX_DMA_ADC_INPUTS ADC inputs. The hardware output remains stopped
 * until @ref hal_dacless_begin is called. Creation and destruction are
 * single-owner operations and must not race other operations on the handle.
 * On RP targets, create, start, control and destroy every DMA instance from
 * one owner core. The DMA interrupt is installed on that core. A second DMA
 * instance that needs the shared ADC, or a PWM slice already in use, is
 * rejected with HAL_EBUSY.
 *
 * @param config Configuration to copy, or NULL to use defaults.
 * @param out_audio Destination for the new handle. Set to NULL on failure.
 * @return HAL_OK on success, HAL_EINVAL when @p out_audio is NULL, a used
 *         PWM/ADC pin is unsupported by the selected target, one pin is used
 *         for both PWM and ADC, or DMA mode asks for too many ADC inputs;
 *         HAL_ENOMEM when the shared C/C++ instance registry, static C pool,
 *         or its synchronization primitive is unavailable.
 */
hal_status_t hal_dacless_create(const hal_dacless_config_t *config,
                                hal_dacless_t *out_audio);

/**
 * @brief Start or restart PWM audio output.
 * @param audio Valid DACless handle.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle,
 *         HAL_EUNSUPPORTED when the selected DMA backend is unavailable,
 *         HAL_EBUSY when a required hardware resource is already active,
 *         HAL_ENOMEM when a backend pool is exhausted, or HAL_EIO when the
 *         PWM/DMA backend cannot be started.
 */
hal_status_t hal_dacless_begin(hal_dacless_t audio);

/**
 * @brief Release a DACless instance back to the static pool.
 *
 * Do not call this function from an audio callback or concurrently with an
 * audio callback. A NULL, stale or already destroyed handle is rejected.
 *
 * @param audio Valid DACless handle.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle, HAL_ENOMEM
 *         when synchronization is unavailable, or HAL_EBUSY when an RP DMA
 *         instance is destroyed from a core other than its owner.
 */
hal_status_t hal_dacless_destroy(hal_dacless_t audio);

/**
 * @brief Advance polling-mode output and refill completed buffers.
 *
 * DMA instances also accept this call; it completes without additional work.
 *
 * @param audio Valid, started DACless handle.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle, or HAL_ESTATE
 *         when output has not been started.
 */
hal_status_t hal_dacless_service(hal_dacless_t audio);

/**
 * @brief Mute output and drive the configured PWM midpoint.
 * @param audio Valid, started DACless handle.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle, HAL_ESTATE
 *         when output has not been started, HAL_ENOMEM when synchronization
 *         cannot be initialized, or HAL_EIO from the DMA backend.
 */
hal_status_t hal_dacless_mute(hal_dacless_t audio);

/**
 * @brief Resume output after muting.
 * @param audio Valid, started DACless handle.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle, HAL_ESTATE
 *         when output has not been started, HAL_EBUSY when ADC or PWM
 *         resources are in use, HAL_ENOMEM when synchronization cannot be
 *         initialized, or HAL_EIO from the DMA backend.
 */
hal_status_t hal_dacless_unmute(hal_dacless_t audio);

/**
 * @brief Install or clear the per-sample callback.
 *
 * A block callback takes precedence while both callback types are installed.
 * Passing NULL clears the sample callback; @p context may then be NULL.
 * Configure callbacks before the first successful @ref hal_dacless_begin.
 *
 * @param audio Valid DACless handle.
 * @param callback Callback function or NULL.
 * @param context Application context forwarded unchanged to @p callback.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle, HAL_ESTATE
 *         after output has started, or HAL_ENOMEM when synchronization cannot
 *         be initialized.
 */
hal_status_t hal_dacless_set_sample_callback(
    hal_dacless_t audio, hal_dacless_sample_callback_t callback, void *context);

/**
 * @brief Install or clear the block callback.
 *
 * Passing NULL clears the block callback; @p context may then be NULL. Sample
 * and block callbacks retain independent application contexts.
 * Configure callbacks before the first successful @ref hal_dacless_begin.
 *
 * @param audio Valid DACless handle.
 * @param callback Callback function or NULL.
 * @param context Application context forwarded unchanged to @p callback.
 * @return HAL_OK on success, HAL_EINVAL for an invalid handle, HAL_ESTATE
 *         after output has started, or HAL_ENOMEM when synchronization cannot
 *         be initialized.
 */
hal_status_t hal_dacless_set_block_callback(
    hal_dacless_t audio, hal_dacless_block_callback_t callback, void *context);

/**
 * @brief Read one captured ADC channel.
 * @param audio Valid DACless handle.
 * @param channel Zero-based channel below the configured ADC input count.
 * @param out_value Destination for the 16-bit ADC value. Must not be NULL.
 * @note This is the only DACless function that may be called from a DACless
 *       audio callback. The handle must not be destroyed concurrently.
 * @return HAL_OK on success or HAL_EINVAL for an invalid handle, channel or
 *         destination.
 */
hal_status_t hal_dacless_get_adc(hal_dacless_t audio, uint8_t channel,
                                 uint16_t *out_value);

/**
 * @brief Read the configured output sample rate.
 * @param audio Valid DACless handle.
 * @param out_hz Destination for the sample rate in hertz. Must not be NULL.
 * @return HAL_OK on success or HAL_EINVAL for an invalid handle or destination.
 */
hal_status_t hal_dacless_get_sample_rate(hal_dacless_t audio, float *out_hz);

/**
 * @brief Copy the effective, range-normalized configuration.
 * @param audio Valid DACless handle.
 * @param out_config Destination configuration. Must not be NULL.
 * @return HAL_OK on success or HAL_EINVAL for an invalid handle or destination.
 */
hal_status_t hal_dacless_get_config(hal_dacless_t audio,
                                    hal_dacless_config_t *out_config);

/**
 * @brief Read the most recently completed output-buffer pointer.
 *
 * The returned storage belongs to the driver and remains valid only until the
 * next buffer completion or destruction. NULL means that no block has
 * completed yet.
 *
 * @param audio Valid DACless handle.
 * @param out_buffer Destination for the read-only volatile pointer. Must not be
 *        NULL.
 * @return HAL_OK on success or HAL_EINVAL for an invalid handle or destination.
 */
hal_status_t
hal_dacless_get_output_buffer(hal_dacless_t audio,
                              const volatile uint16_t **out_buffer);

/**
 * @brief Read the driver-owned ADC result-buffer pointer and active length.
 *
 * The returned storage remains owned by the handle and becomes invalid when
 * the handle is destroyed. Values may change while audio is running.
 *
 * @param audio Valid DACless handle.
 * @param out_buffer Destination for the read-only volatile pointer. Must not be
 *        NULL.
 * @param out_count Optional destination for the configured channel count; may
 *        be NULL.
 * @return HAL_OK on success or HAL_EINVAL for an invalid handle or destination.
 */
hal_status_t hal_dacless_get_adc_buffer(hal_dacless_t audio,
                                        const volatile uint16_t **out_buffer,
                                        uint8_t *out_count);

/**
 * @brief Copy the current lifecycle and output state.
 * @param audio Valid DACless handle.
 * @param out_state Destination state. Must not be NULL.
 * @return HAL_OK on success or HAL_EINVAL for an invalid handle or destination.
 */
hal_status_t hal_dacless_get_state(hal_dacless_t audio,
                                   hal_dacless_state_t *out_state);

/**
 * @brief Linearly interpolate two unsigned samples.
 * @param x First sample, returned when the low eight bits of @p mu_scaled are
 *        zero.
 * @param y Second sample.
 * @param mu_scaled Fraction represented by the low eight bits, from 0 to 255.
 * @return Interpolated 16-bit sample, clamped to the unsigned range.
 */
uint16_t hal_dacless_interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled);

#ifdef __cplusplus
}
#endif

/* Keep the existing C++ class reachable through the same public include. */
#ifdef __cplusplus
#include "hal/audio/dacless/dacless.h"
#endif

#endif /* HAL_ENABLE_DACLESS */
