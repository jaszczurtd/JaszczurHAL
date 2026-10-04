/* STM32G474 hardware UART test on USART2 (hal_uart PORT_2, PA2/PA3), which
 * the NUCLEO-G474RE routes to the ST-LINK virtual COM port. After reset the
 * debug console prints a boot line at 115200 for three seconds (reset
 * reason, serial number, UID); then the application takes USART2 at
 * 3 Mbaud 8N1 and answers line commands from the host verifier:
 *
 *   S         status: rate, format, serial number, UID, error counters
 *   E<n>      echo the next n bytes (bulk read + all-or-nothing write)
 *   T<n>      send n pattern bytes with hal_uart_try_write_ex()
 *   W<n>      send n pattern bytes with blocking hal_uart_write_ex()
 *   O<ms>     stop reading for ms, then return what the ring kept
 *   F<fmt>    switch the frame format (8N1, 8E1, 7E1, ...); without a
 *             command for FORMAT_REVERT_MS the port returns to 8N1, so a
 *             format the host side cannot produce does not lock it out
 *   B<baud>   switch the rate
 *   L         print through the debug console while the port is owned
 *   C         give USART2 back to the console for a while, then take it
 *   R         software reset (hal_system_reset)
 *   M<ms>     mask interrupts for ms while the host sends, then return what
 *             the ring kept (late DMA interrupts)
 *   Q<baud>,<fmt>[,<n>]  USART1 self-test in single-wire mode on PC4: the
 *             port receives the n bytes it sends (default 4096), no wire
 *   Z         overflow the main stack into the stack guard with the port
 *             open (bare-metal build)
 *   P<n>      FreeRTOS build: this task sends n pattern bytes with blocking
 *             writes while app_task1 reads n bytes from the host and probes
 *             the writer lock with zero-length non-blocking writes
 *
 * The built-in LED toggles on every command and blinks while the boot line
 * repeats. On the CAN-FD HAT the relay drivers are held low (also with
 * UART_FIXTURE_HAT_RELAYS on another board profile). */

#include <hal/core/hal_app.h>
#include <hal/core/hal_array.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_uart.h>
#include <hal/system/hal_sync.h>
#include <hal/system/hal_system.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixture-only register access: the cycle counter for waits with interrupts
 * masked, and single-wire mode for the USART1 self-test. */
#include "hal/impl/stm32g474/port/stm32g474_regs.h"

#define PIN(port, n) ((uint8_t)((port) * 16u + (n)))
#define UART_TX PIN(0u, 2u) /* PA2 */
#define UART_RX PIN(0u, 3u) /* PA3 */

#define APP_BAUD 3000000u
#define BOOT_WINDOW_MS 3000u
#define BOOT_LINE_MS 250u
#define CONSOLE_WINDOW_MS 1500u
#define CONSOLE_LINE_MS 200u
#define LINE_MAX 48u
#define CHUNK 200u
#define WRITE_CHUNK 1000u
#define RATE_TIMEOUT_MS 10000u
#define FORMAT_REVERT_MS 5000u
#define MASK_SETTLE_MS 5u

#define LOOP_TX PIN(2u, 4u) /* PC4, USART1 TX: the single wire */
#define LOOP_RX PIN(2u, 5u) /* PC5, USART1 RX: unused in single-wire mode */
#define LOOP_BYTES 4096u
#define LOOP_RX_OFFSET 512u
#define USART_CR3_HDSEL (1u << 3)

#if defined(HAL_ENABLE_FREERTOS) && defined(HAL_ENABLE_APP_TASK1)
#define FIXTURE_DUPLEX 1
#endif

#if defined(HAL_BOARD_PROFILE_STM32G474_NUCLEO_CANHAT) ||                      \
    defined(UART_FIXTURE_HAT_RELAYS)
#define FIXTURE_HAT_RELAYS 1
/* Relay drivers K1-K4 have no gate pull-down: drive them off. */
static const uint8_t kRelayPins[] = {PIN(2u, 3u), PIN(2u, 0u), PIN(0u, 10u),
                                     PIN(1u, 3u)};
#endif

typedef struct {
  const char *name;
  uint16_t config;
} frame_format_t;

static const frame_format_t kFormats[] = {
    {"8N1", HAL_UART_CFG_8N1}, {"8N2", HAL_UART_CFG_8N2},
    {"8E1", HAL_UART_CFG_8E1}, {"8O1", HAL_UART_CFG_8O1},
    {"8E2", HAL_UART_CFG_8E2}, {"7N1", HAL_UART_CFG_7N1},
    {"7E1", HAL_UART_CFG_7E1}, {"7O1", HAL_UART_CFG_7O1},
    {"6E1", HAL_UART_CFG_6E1}, {"6O1", HAL_UART_CFG_6O1},
    {"5N1", HAL_UART_CFG_5N1}, {"6N1", HAL_UART_CFG_6N1},
};

static hal_uart_t s_uart;
static uint32_t s_baud = APP_BAUD;
static const frame_format_t *s_format = &kFormats[0];
static bool s_booting = true;
static uint32_t s_boot_started;
static uint32_t s_boot_line;
static bool s_led;
static uint32_t s_last_command;
static char s_line[LINE_MAX];
static size_t s_line_len;
static uint8_t s_buffer[WRITE_CHUNK];
static char s_text[160];

static void led_toggle(void) {
  s_led = !s_led;
  hal_gpio_write(HAL_LED_BUILTIN, s_led);
}

static uint8_t pattern(uint32_t index) {
  return (uint8_t)(index * 31u + (index >> 8));
}

static void identity(char *serial, size_t serial_size, size_t *serial_len,
                     char *uid, size_t uid_size) {
  uint8_t raw[HAL_DEVICE_SERIAL_MAX_BYTES];
  *serial_len = 0u;
  if (hal_get_device_serial_ex(raw, sizeof(raw), serial_len) != HAL_OK ||
      hal_get_device_serial_hex_ex(serial, serial_size) != HAL_OK) {
    (void)snprintf(serial, serial_size, "none");
  }
  if (hal_get_device_uid_hex_ex(uid, uid_size) != HAL_OK) {
    (void)snprintf(uid, uid_size, "none");
  }
}

/* One line to the host over the application port. */
static void reply(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int len = vsnprintf(s_text, sizeof(s_text) - 2u, format, args);
  va_end(args);
  if (len < 0) {
    return;
  }
  if ((size_t)len > sizeof(s_text) - 3u) {
    len = (int)(sizeof(s_text) - 3u);
  }
  s_text[len++] = '\r';
  s_text[len++] = '\n';
  (void)hal_uart_write_ex(s_uart, (const uint8_t *)s_text, (size_t)len, NULL);
}

static bool start_port(uint32_t baud, const frame_format_t *format) {
  if (s_uart == NULL) {
    s_uart = hal_uart_create(HAL_UART_PORT_2, UART_RX, UART_TX);
    if (s_uart == NULL) {
      return false;
    }
  }
  if (hal_uart_begin(s_uart, baud, format->config) != HAL_OK) {
    return false;
  }
  s_baud = baud;
  s_format = format;
  s_line_len = 0u;
  return true;
}

static void cmd_status(void) {
  char serial[HAL_DEVICE_SERIAL_HEX_BUF_SIZE];
  char uid[HAL_DEVICE_UID_HEX_BUF_SIZE];
  size_t serial_len = 0u;
  identity(serial, sizeof(serial), &serial_len, uid, sizeof(uid));
  hal_uart_error_counters_t counters = {0};
  (void)hal_uart_get_error_counters_ex(s_uart, &counters);
  size_t tx_free = 0u;
  (void)hal_uart_tx_free_ex(s_uart, &tx_free);
  reply("STAT baud=%lu fmt=%s serial=%s len=%u uid=%s ore=%lu fe=%lu pe=%lu "
        "ovf=%lu txfree=%u rtos=%d clock=%lu",
        (unsigned long)s_baud, s_format->name, serial, (unsigned)serial_len,
        uid, (unsigned long)counters.rx_overrun,
        (unsigned long)counters.rx_framing, (unsigned long)counters.rx_parity,
        (unsigned long)counters.rx_buffer_overflow, (unsigned)tx_free,
#if FIXTURE_DUPLEX
        1,
#else
        0,
#endif
        (unsigned long)JH_G474_CORE_CLOCK_HZ);
}

static uint32_t rate_timeout_ms(uint32_t bytes, uint32_t baud) {
  return RATE_TIMEOUT_MS + (uint32_t)(((uint64_t)bytes * 20000u) / baud);
}

/* Bridge-style echo: bulk reads, all-or-nothing writes, never waits. */
static void cmd_echo(uint32_t count) {
  reply("READY echo %lu", (unsigned long)count);
  const uint32_t started = hal_millis();
  const uint32_t timeout = rate_timeout_ms(count, s_baud);
  uint32_t received = 0u;
  uint32_t sent = 0u;
  uint32_t again = 0u;
  size_t held = 0u;
  while (sent < count && !hal_millis_deadline_expired(started, timeout)) {
    if (held == 0u && received < count) {
      const uint32_t want = count - received < CHUNK ? count - received : CHUNK;
      size_t got = 0u;
      if (hal_uart_read_bytes_ex(s_uart, s_buffer, want, &got) == HAL_OK) {
        held = got;
        received += (uint32_t)got;
      }
    }
    if (held > 0u) {
      const hal_status_t status = hal_uart_try_write_ex(s_uart, s_buffer, held);
      if (status == HAL_OK) {
        sent += (uint32_t)held;
        held = 0u;
      } else if (status == HAL_EAGAIN) {
        ++again;
      } else {
        break;
      }
    }
  }
  reply("OK echo n=%lu sent=%lu again=%lu ms=%lu", (unsigned long)count,
        (unsigned long)sent, (unsigned long)again,
        (unsigned long)(hal_millis() - started));
}

static void fill(uint32_t first, size_t len) {
  for (size_t i = 0u; i < len; i++) {
    s_buffer[i] = pattern(first + (uint32_t)i);
  }
}

/* Blocking writes of count pattern bytes in chunks larger than the queue. */
static uint32_t send_blocking(uint32_t count) {
  const uint32_t started = hal_millis();
  uint32_t sent = 0u;
  while (sent < count && !hal_millis_deadline_expired(
                             started, rate_timeout_ms(count, s_baud))) {
    const size_t len = count - sent < WRITE_CHUNK ? count - sent : WRITE_CHUNK;
    fill(sent, len);
    size_t written = 0u;
    const hal_status_t status =
        hal_uart_write_ex(s_uart, s_buffer, len, &written);
    sent += (uint32_t)written;
    if (status != HAL_OK) {
      break;
    }
  }
  return sent;
}

static void cmd_send(uint32_t count, bool blocking) {
  reply("READY tx %lu", (unsigned long)count);
  const uint32_t started = hal_millis();
  const uint32_t timeout = rate_timeout_ms(count, s_baud);
  uint32_t sent = 0u;
  uint32_t again = 0u;
  if (blocking) {
    sent = send_blocking(count);
  }
  while (!blocking && sent < count &&
         !hal_millis_deadline_expired(started, timeout)) {
    const size_t len = count - sent < CHUNK ? count - sent : CHUNK;
    fill(sent, len);
    const hal_status_t status = hal_uart_try_write_ex(s_uart, s_buffer, len);
    if (status == HAL_OK) {
      sent += (uint32_t)len;
    } else if (status == HAL_EAGAIN) {
      ++again;
    } else {
      break;
    }
  }
  const uint32_t elapsed = hal_millis() - started;
  (void)hal_uart_flush(s_uart);
  reply("OK tx n=%lu sent=%lu again=%lu ms=%lu", (unsigned long)count,
        (unsigned long)sent, (unsigned long)again, (unsigned long)elapsed);
}

static hal_uart_error_counters_t counters_now(void) {
  hal_uart_error_counters_t counters = {0};
  (void)hal_uart_get_error_counters_ex(s_uart, &counters);
  return counters;
}

/* Return what the ring kept, the bytes counted as lost and the overrun
 * events (a late interrupt that may have hidden a lap). */
static void report_kept(const char *what,
                        const hal_uart_error_counters_t *before) {
  const int waiting = hal_uart_available(s_uart);
  size_t got = 0u;
  (void)hal_uart_read_bytes_ex(s_uart, s_buffer, sizeof(s_buffer), &got);
  const hal_uart_error_counters_t after = counters_now();
  reply("DATA %u waiting=%d", (unsigned)got, waiting);
  (void)hal_uart_write_ex(s_uart, s_buffer, got, NULL);
  reply("OK %s ovf=%lu ore=%lu", what,
        (unsigned long)(after.rx_buffer_overflow - before->rx_buffer_overflow),
        (unsigned long)(after.rx_overrun - before->rx_overrun));
}

/* Leave the ring unread while the host sends more than it holds. */
static void cmd_hold(uint32_t ms) {
  const hal_uart_error_counters_t before = counters_now();
  reply("READY hold %lu", (unsigned long)ms);
  (void)hal_uart_flush(s_uart);
  hal_delay_ms(ms);
  report_kept("hold", &before);
}

/* A wait that needs no interrupt: the DWT cycle counter. */
static void wait_cycles_ms(uint32_t ms) {
  const uint32_t cycles = ms * (JH_G474_CORE_CLOCK_HZ / 1000u);
  const uint32_t started = DWT_CYCCNT;
  while ((uint32_t)(DWT_CYCCNT - started) < cycles) {
  }
}

/* Mask interrupts while the host sends: the receive DMA runs on, its
 * half/full interrupts wait until the mask lifts. */
static void cmd_mask(uint32_t ms) {
  const hal_uart_error_counters_t before = counters_now();
  reply("READY mask %lu", (unsigned long)ms);
  (void)hal_uart_flush(s_uart);
  hal_critical_section_enter();
  wait_cycles_ms(ms);
  hal_critical_section_exit();
  /* Bytes still on their way when a short mask ends land before the read. */
  hal_delay_ms(MASK_SETTLE_MS);
  report_kept("mask", &before);
}

static const frame_format_t *find_format(const char *name) {
  for (size_t i = 0u; i < COUNTOF(kFormats); i++) {
    if (strcmp(kFormats[i].name, name) == 0) {
      return &kFormats[i];
    }
  }
  return NULL;
}

static void cmd_format(const char *name) {
  const frame_format_t *format = find_format(name);
  if (format == NULL) {
    reply("ERR fmt %s unknown", name);
    return;
  }
  reply("OK fmt %s", name);
  (void)hal_uart_flush(s_uart);
  /* A format the USART cannot carry leaves the port running unchanged. */
  const hal_status_t status = hal_uart_begin(s_uart, s_baud, format->config);
  if (status == HAL_OK) {
    s_format = format;
  } else {
    reply("ERR fmt %s status=%d", name, (int)status);
  }
  s_line_len = 0u;
}

static void cmd_baud(uint32_t baud) {
  reply("OK baud %lu", (unsigned long)baud);
  (void)hal_uart_flush(s_uart);
  /* A rate out of reach leaves the port running unchanged. */
  const hal_status_t status = hal_uart_begin(s_uart, baud, s_format->config);
  if (status == HAL_OK) {
    s_baud = baud;
  } else {
    reply("ERR baud %lu status=%d", (unsigned long)baud, (int)status);
  }
  s_line_len = 0u;
}

static void cmd_leak(void) {
  hal_deb("JHUARTLEAK console");
  (void)printf("JHUARTLEAK printf\n");
  (void)fflush(stdout);
  reply("OK leak");
}

/* The console gets USART2 back for a while, then the port takes it again. */
static void cmd_console(void) {
  reply("OK console");
  (void)hal_uart_flush(s_uart);
  hal_uart_destroy(s_uart);
  s_uart = NULL;
  const uint32_t started = hal_millis();
  uint32_t line = 0u;
  while (!hal_millis_deadline_expired(started, CONSOLE_WINDOW_MS)) {
    hal_deb("JHUARTCONSOLE n=%lu", (unsigned long)line++);
    led_toggle();
    hal_delay_ms(CONSOLE_LINE_MS);
  }
  if (!start_port(s_baud, s_format)) {
    hal_deb("JHUARTCONSOLE restart failed");
  }
}

static void cmd_reset(void) {
  reply("OK reset");
  (void)hal_uart_flush(s_uart);
  (void)hal_system_reset();
}

/* Data bits of a format name such as "7E1". */
static uint8_t data_mask_of(const frame_format_t *format) {
  return (uint8_t)((1u << (uint32_t)(format->name[0] - '0')) - 1u);
}

/* USART1 (PORT_1) through the HAL with one register change made here: in
 * single-wire mode TX and RX meet inside the USART, so it receives its own
 * bytes. The pull-up keeps the released line idle between frames. */
static void cmd_loop1(const char *args) {
  char *end = NULL;
  const uint32_t baud = (uint32_t)strtoul(args, &end, 10);
  char name[4] = "8N1";
  uint32_t count = LOOP_BYTES;
  if (end != NULL && *end == ',') {
    (void)strncpy(name, end + 1, sizeof(name) - 1u);
    const char *more = strchr(end + 1, ',');
    if (more != NULL) {
      count = (uint32_t)strtoul(more + 1, NULL, 10);
    }
  }
  const frame_format_t *format = find_format(name);
  if (format == NULL || count == 0u) {
    reply("ERR loop1 arguments");
    return;
  }
  hal_uart_t loop = hal_uart_create(HAL_UART_PORT_1, LOOP_RX, LOOP_TX);
  if (loop == NULL) {
    reply("ERR loop1 create");
    return;
  }
  const hal_status_t begun = hal_uart_begin(loop, baud, format->config);
  if (begun != HAL_OK) {
    hal_uart_destroy(loop);
    reply("ERR loop1 begin status=%d", (int)begun);
    return;
  }
  USART_CR1(USART1_BASE) &= ~USART_CR1_UE;
  USART_CR3(USART1_BASE) |= USART_CR3_HDSEL;
  USART_CR1(USART1_BASE) |= USART_CR1_UE;
  GPIO_PUPDR(2u) = (GPIO_PUPDR(2u) & ~(0x3u << 8u)) | (GPIO_PUPD_UP << 8u);
  hal_delay_ms(2u);
  size_t got = 0u;
  uint8_t *rx = &s_buffer[LOOP_RX_OFFSET];
  while (hal_uart_read_bytes_ex(loop, rx, CHUNK, &got) == HAL_OK) {
  }
  /* Switching the line over may leave a framing error: count from here. */
  hal_uart_error_counters_t before = {0};
  (void)hal_uart_get_error_counters_ex(loop, &before);

  const uint8_t mask = data_mask_of(format);
  const uint32_t started = hal_millis();
  uint32_t sent = 0u;
  uint32_t received = 0u;
  uint32_t bad = 0u;
  while (received < count &&
         !hal_millis_deadline_expired(started, rate_timeout_ms(count, baud))) {
    if (sent < count) {
      const size_t len = count - sent < CHUNK ? count - sent : CHUNK;
      fill(sent, len);
      if (hal_uart_try_write_ex(loop, s_buffer, len) == HAL_OK) {
        sent += (uint32_t)len;
      }
    }
    if (hal_uart_read_bytes_ex(loop, rx, CHUNK, &got) == HAL_OK) {
      for (size_t i = 0u; i < got; i++) {
        if (rx[i] != (uint8_t)(pattern(received + (uint32_t)i) & mask)) {
          ++bad;
        }
      }
      received += (uint32_t)got;
    }
  }
  const uint32_t elapsed = hal_millis() - started;
  hal_uart_error_counters_t after = {0};
  (void)hal_uart_get_error_counters_ex(loop, &after);
  hal_uart_destroy(loop);
  reply("OK loop1 baud=%lu fmt=%s sent=%lu got=%lu bad=%lu ovf=%lu ore=%lu "
        "fe=%lu pe=%lu ms=%lu",
        (unsigned long)baud, format->name, (unsigned long)sent,
        (unsigned long)received, (unsigned long)bad,
        (unsigned long)(after.rx_buffer_overflow - before.rx_buffer_overflow),
        (unsigned long)(after.rx_overrun - before.rx_overrun),
        (unsigned long)(after.rx_framing - before.rx_framing),
        (unsigned long)(after.rx_parity - before.rx_parity),
        (unsigned long)elapsed);
}

#if defined(HAL_ENABLE_STACK_GUARD) && !defined(HAL_ENABLE_FREERTOS)
static volatile uint32_t s_recursion_limit = UINT32_MAX;

static uint32_t descend(uint32_t depth) {
  volatile uint8_t frame[256];
  frame[0] = (uint8_t)depth;
  if (depth >= s_recursion_limit) {
    return frame[0];
  }
  return descend(depth + 1u) + frame[0];
}
#endif

/* The stack guard catches the overflow; its reset path must not write to
 * USART2 while the application owns it. */
static void cmd_stack_overflow(void) {
#if defined(HAL_ENABLE_STACK_GUARD) && !defined(HAL_ENABLE_FREERTOS)
  reply("OK overflow");
  (void)hal_uart_flush(s_uart);
  (void)descend(0u);
  reply("ERR overflow returned");
#else
  reply("ERR overflow unavailable");
#endif
}

#if FIXTURE_DUPLEX
/* Shared with app_task1 for the duration of one P command. */
static struct {
  volatile bool active;
  volatile bool done;
  volatile uint32_t want;
  volatile uint32_t got;
  volatile uint32_t sum;
  volatile uint32_t probes;
  volatile uint32_t refused;
  volatile uint32_t probe_max_cycles;
} s_duplex;
static uint8_t s_task1_buffer[CHUNK];

void app_task1(void) {
  if (!s_duplex.active) {
    hal_delay_ms(1u);
    return;
  }
  size_t got = 0u;
  const uint32_t want = s_duplex.want - s_duplex.got;
  if (want > 0u &&
      hal_uart_read_bytes_ex(s_uart, s_task1_buffer,
                             want < CHUNK ? want : CHUNK, &got) == HAL_OK) {
    uint32_t sum = s_duplex.sum;
    for (size_t i = 0u; i < got; i++) {
      sum = sum * 31u + s_task1_buffer[i];
    }
    s_duplex.sum = sum;
    s_duplex.got += (uint32_t)got;
  }
  /* A zero-length non-blocking write only takes the writer lock. */
  const uint32_t started = DWT_CYCCNT;
  const hal_status_t probe = hal_uart_try_write_ex(s_uart, s_task1_buffer, 0u);
  const uint32_t cycles = DWT_CYCCNT - started;
  ++s_duplex.probes;
  if (probe == HAL_EAGAIN) {
    ++s_duplex.refused;
  }
  if (cycles > s_duplex.probe_max_cycles) {
    s_duplex.probe_max_cycles = cycles;
  }
  if (s_duplex.got >= s_duplex.want) {
    s_duplex.done = true;
    s_duplex.active = false;
  }
}
#endif

static void cmd_duplex(uint32_t count) {
#if FIXTURE_DUPLEX
  memset((void *)&s_duplex, 0, sizeof(s_duplex));
  s_duplex.want = count;
  reply("READY duplex %lu", (unsigned long)count);
  s_duplex.active = true;
  const uint32_t sent = send_blocking(count);
  const uint32_t started = hal_millis();
  while (!s_duplex.done && !hal_millis_deadline_expired(
                               started, rate_timeout_ms(count, s_baud))) {
    hal_delay_ms(1u);
  }
  s_duplex.active = false;
  (void)hal_uart_flush(s_uart);
  reply("OK duplex sent=%lu got=%lu sum=%08lx probes=%lu refused=%lu "
        "probe_max_us=%lu ovf=%lu",
        (unsigned long)sent, (unsigned long)s_duplex.got,
        (unsigned long)s_duplex.sum, (unsigned long)s_duplex.probes,
        (unsigned long)s_duplex.refused,
        (unsigned long)(s_duplex.probe_max_cycles /
                        (JH_G474_CORE_CLOCK_HZ / 1000000u)),
        (unsigned long)counters_now().rx_buffer_overflow);
#else
  (void)count;
  reply("ERR duplex unavailable");
#endif
}

static void run_command(char *line) {
  led_toggle();
  s_last_command = hal_millis();
  const char op = line[0];
  const uint32_t value = (uint32_t)strtoul(&line[1], NULL, 10);
  switch (op) {
  case 'S':
    cmd_status();
    break;
  case 'E':
    cmd_echo(value);
    break;
  case 'T':
    cmd_send(value, false);
    break;
  case 'W':
    cmd_send(value, true);
    break;
  case 'O':
    cmd_hold(value);
    break;
  case 'F':
    cmd_format(&line[1]);
    break;
  case 'B':
    cmd_baud(value);
    break;
  case 'L':
    cmd_leak();
    break;
  case 'C':
    cmd_console();
    break;
  case 'R':
    cmd_reset();
    break;
  case 'M':
    cmd_mask(value);
    break;
  case 'Q':
    cmd_loop1(&line[1]);
    break;
  case 'Z':
    cmd_stack_overflow();
    break;
  case 'P':
    cmd_duplex(value);
    break;
  case '\0':
    break;
  default:
    reply("ERR unknown");
    break;
  }
}

static void poll_commands(void) {
  uint8_t byte = 0u;
  while (hal_uart_read_ex(s_uart, &byte) == HAL_OK) {
    if (byte == '\r') {
      continue;
    }
    if (byte == '\n') {
      s_line[s_line_len] = '\0';
      s_line_len = 0u;
      run_command(s_line);
      return;
    }
    if (s_line_len < LINE_MAX - 1u) {
      s_line[s_line_len++] = (char)byte;
    }
  }
}

static void boot_line(void) {
  char serial[HAL_DEVICE_SERIAL_HEX_BUF_SIZE];
  char uid[HAL_DEVICE_UID_HEX_BUF_SIZE];
  size_t serial_len = 0u;
  identity(serial, sizeof(serial), &serial_len, uid, sizeof(uid));
  hal_deb("JHUARTBOOT reset=%s serial=%s len=%u uid=%s",
          hal_reset_reason_str(hal_get_reset_reason()), serial,
          (unsigned)serial_len, uid);
}

void app_start(void) {
  hal_debug_init_default();
#if defined(HAL_ENABLE_STACK_GUARD)
  (void)hal_stack_guard_init_ex();
#endif
#if FIXTURE_HAT_RELAYS
  for (size_t i = 0u; i < COUNTOF(kRelayPins); i++) {
    hal_gpio_set_mode(kRelayPins[i], HAL_GPIO_OUTPUT_LOW);
  }
#endif
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT_LOW);
  s_boot_started = hal_millis();
  s_boot_line = s_boot_started - BOOT_LINE_MS;
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if (s_booting) {
    if (hal_elapsed_u32(now, s_boot_line, BOOT_LINE_MS)) {
      s_boot_line = now;
      boot_line();
      led_toggle();
    }
    if (hal_elapsed_u32(now, s_boot_started, BOOT_WINDOW_MS)) {
      s_booting = false;
      if (!start_port(APP_BAUD, &kFormats[0])) {
        hal_deb("JHUARTBOOT port start failed");
      }
    }
    return;
  }
  if (s_uart != NULL) {
    poll_commands();
    if (s_format != &kFormats[0] &&
        hal_elapsed_u32(hal_millis(), s_last_command, FORMAT_REVERT_MS)) {
      (void)start_port(s_baud, &kFormats[0]);
    }
  }
}
