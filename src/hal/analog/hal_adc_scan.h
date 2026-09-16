#pragma once

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file hal_adc_scan.h
 * @brief Continuous hardware-paced ADC scan into a caller-owned double block.
 *
 * Enable HAL_ENABLE_ADC_SCAN. The converter samples the listed pins one after
 * another at a fixed hardware-timed period and DMA fills the two halves of
 * the buffer alternately, so every sample has a known position in time and
 * the CPU only reads finished blocks. While a scan runs it owns the
 * converter: the DACless audio path and the scan exclude each other with
 * HAL_EBUSY, hal_adc_read() of a scanned pin returns the newest scanned
 * sample instead of converting, and hal_adc_read() of a pin the scan does
 * not carry returns -1 because no conversion can be made for it. Start/stop
 * belong to one owner core; take/latest may be called from any task but not
 * from an ISR.
 */

/** @brief Most pins one scan can carry. */
#define HAL_ADC_SCAN_MAX_PINS 8u
/** @brief Pin value selecting the internal temperature sensor. */
#define HAL_ADC_SCAN_PIN_TEMPERATURE 0xFFu
/** @brief Shortest conversion period accepted by the facade, in nanoseconds;
 * backends may need more (RP: 2000 ns, ESP32-S3: 12000 ns). */
#define HAL_ADC_SCAN_MIN_CONVERSION_NS 1000u
/** @brief Longest conversion period accepted, in nanoseconds. */
#define HAL_ADC_SCAN_MAX_CONVERSION_NS 1000000000u
/** @brief Smallest block, in frames. */
#define HAL_ADC_SCAN_MIN_BLOCK_FRAMES 2u
/** @brief Largest block, in samples (frames times pins). */
#define HAL_ADC_SCAN_MAX_BLOCK_SAMPLES 32768u

/**
 * @brief Block-completion hook, called when a block is complete.
 * @param user Opaque pointer from the configuration.
 * @return Any value worth pairing with the block, e.g. a PWM counter.
 * @note On RP and STM32G474 this runs in the DMA interrupt on the core that
 * started the scan: a few loads at most, no HAL calls, no locks. On ESP32
 * it runs in the task that collects the block.
 */
typedef uint32_t (*hal_adc_scan_marker_fn)(void *user);

/** @brief Scan configuration, copied by the driver at start. */
typedef struct {
  uint8_t pins[HAL_ADC_SCAN_MAX_PINS]; /**< Distinct ADC-capable HAL pins, or
                                          HAL_ADC_SCAN_PIN_TEMPERATURE. */
  uint8_t pin_count;                   /**< 1..HAL_ADC_SCAN_MAX_PINS. */
  uint32_t conversion_period_ns; /**< Time between consecutive conversions;
                                    each pin is sampled every
                                    conversion_period_ns times pin_count.
                                    Backends round to what the converter
                                    can do and report the frame period. */
  uint16_t *buffer;              /**< Caller-owned, 2 times block_frames
                                    times pin_count samples, 4-byte aligned,
                                    alive until stop. */
  uint32_t block_frames;         /**< Frames per block, at least
                                    HAL_ADC_SCAN_MIN_BLOCK_FRAMES; frames
                                    times pins at most
                                    HAL_ADC_SCAN_MAX_BLOCK_SAMPLES. */
  hal_adc_scan_marker_fn marker; /**< Optional completion hook; NULL skips. */
  void *marker_user;             /**< Passed to the hook. */
} hal_adc_scan_config_t;

/** @brief One completed block. Frame-major: the sample of the pin at frame
 * position p in frame k is samples[k * pin_count + p]; positions come from
 * hal_adc_scan_pin_position(), because the frame order is the converter's,
 * not necessarily the configured one. */
typedef struct {
  const uint16_t *samples; /**< Raw right-aligned 12-bit codes. Valid until
                              the next block completes. */
  uint32_t frames;         /**< Frames in the block. */
  uint8_t pin_count;       /**< Samples per frame. */
  uint32_t sequence;       /**< 1 for the first block after start. */
  uint32_t completed_us;   /**< hal_micros() at completion. */
  uint32_t marker;         /**< Hook result, or 0 without a hook. */
} hal_adc_scan_block_t;

/**
 * @brief Start scanning; the converter belongs to the scan until stop.
 * @param config Non-NULL configuration, copied.
 * @return HAL_OK, HAL_EINVAL (bad pins, period, buffer or block size),
 * HAL_EBUSY (already running, converter owned elsewhere, or the DMA
 * interrupt is taken), HAL_ENOMEM (no DMA channel or mutex), HAL_EHW, or
 * HAL_EUNSUPPORTED for a period or pin the backend cannot serve.
 * @note Single owner core. The first take() succeeds once a whole block has
 * completed.
 */
hal_status_t hal_adc_scan_start(const hal_adc_scan_config_t *config);

/**
 * @brief Stop scanning and release the converter, DMA and interrupt.
 * @return HAL_OK, also when not running; a backend stop error can be retried.
 * @note Same owner as start. The buffer may be reused after HAL_OK.
 */
hal_status_t hal_adc_scan_stop(void);

/** @brief Whether a scan currently owns the converter. */
bool hal_adc_scan_is_running(void);

/**
 * @brief Hand out the newest completed block not taken yet.
 * @param block Non-NULL destination; unchanged on any error.
 * @return HAL_OK, HAL_EAGAIN (no new block since the last take), HAL_ESTATE
 * (not running), HAL_EINVAL.
 * @note A block a slow consumer skipped is never handed out; the sequence
 * field shows the gap. Process or copy the block before the next completes.
 */
hal_status_t hal_adc_scan_take(hal_adc_scan_block_t *block);

/**
 * @brief Newest sample of one pin: from the block in progress when it holds
 * a complete frame, otherwise from the last completed block.
 * @param pin A configured pin or HAL_ADC_SCAN_PIN_TEMPERATURE.
 * @param raw Non-NULL destination; unchanged on any error.
 * @return HAL_OK, HAL_EAGAIN (nothing sampled yet), HAL_ENOENT (pin not
 * scanned), HAL_ESTATE (not running), HAL_EINVAL.
 */
hal_status_t hal_adc_scan_latest(uint8_t pin, uint16_t *raw);

/**
 * @brief Time between consecutive frames, i.e. between two samples of the
 * same pin, in nanoseconds, as the converter actually runs.
 * @return 0 when not running.
 */
uint32_t hal_adc_scan_frame_period_ns(void);

/**
 * @brief Position of a pin inside a frame.
 * @param pin A configured pin or HAL_ADC_SCAN_PIN_TEMPERATURE.
 * @return 0-based frame position, or UINT8_MAX when the pin is not scanned or
 * no scan runs.
 */
uint8_t hal_adc_scan_pin_position(uint8_t pin);

#ifdef __cplusplus
}
#endif
