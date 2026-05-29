#include "hal_simcom_a76xx.h"

#ifdef HAL_ENABLE_A7670

#include "hal_sync.h"
#include "hal_system.h"
#include "hal_serial.h"
#include "hal_gpio.h"
#include "hal_uart.h"
#include "hal_modem_at.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Forward decl from hal_time / hal_system. */
extern uint32_t hal_millis(void);
extern void     hal_delay_ms(uint32_t ms);

#ifndef HAL_SIMCOM_A76XX_MAX_INSTANCES
#define HAL_SIMCOM_A76XX_MAX_INSTANCES 1
#endif

#ifndef HAL_SIMCOM_A76XX_TOPIC_MAX
#define HAL_SIMCOM_A76XX_TOPIC_MAX 128
#endif

#ifndef HAL_SIMCOM_A76XX_PAYLOAD_MAX
#define HAL_SIMCOM_A76XX_PAYLOAD_MAX 512
#endif

/* ── Internal state ──────────────────────────────────────────────────── */

struct hal_simcom_a76xx_impl_s {
    hal_simcom_a76xx_config_t cfg;
    hal_modem_at_t            at;

    bool mqtt_connected[2];
    int  mqtt_active_client;

    /* MQTT-RX URC reassembly. The CMQTTRX* family delivers a single
       message as a four-URC sequence:
         +CMQTTRXSTART: <idx>,<topic_len>,<payload_len>
         <topic>
         +CMQTTRXTOPIC: <idx>,<topic_len>
         <topic_string>                 (variant-dependent)
         +CMQTTRXPAYLOAD: <idx>,<payload_len>
         <payload_bytes>
         +CMQTTRXEND: <idx>
       The exact line ordering varies between firmware revisions; we
       handle the common case by capturing the next non-URC line after
       RXTOPIC as the topic, and the next non-URC line after RXPAYLOAD
       as the payload. */
    struct {
        bool     in_progress;
        int      client_index;
        size_t   topic_len_announced;
        size_t   payload_len_announced;
        char     topic[HAL_SIMCOM_A76XX_TOPIC_MAX];
        size_t   topic_len;
        uint8_t  payload[HAL_SIMCOM_A76XX_PAYLOAD_MAX];
        size_t   payload_len;
        bool     expect_topic_line;
        bool     expect_payload_line;
        bool     complete;
    } rx;

    hal_simcom_a76xx_mqtt_message_cb_t msg_cb;
    void *msg_cb_user;
    int   dispatched_in_poll;

    bool in_use;
};

static hal_simcom_a76xx_impl_t s_pool[HAL_SIMCOM_A76XX_MAX_INSTANCES];

/* ── Helpers ─────────────────────────────────────────────────────────── */

static hal_simcom_a76xx_result_t map_at(hal_modem_at_result_t r) {
    switch (r) {
        case HAL_MODEM_AT_OK:          return HAL_SIMCOM_A76XX_OK;
        case HAL_MODEM_AT_ERROR:       return HAL_SIMCOM_A76XX_ERROR;
        case HAL_MODEM_AT_TIMEOUT:     return HAL_SIMCOM_A76XX_TIMEOUT;
        case HAL_MODEM_AT_NO_PROMPT:   return HAL_SIMCOM_A76XX_ERROR;
        case HAL_MODEM_AT_INVALID_ARG: return HAL_SIMCOM_A76XX_INVALID_ARG;
        case HAL_MODEM_AT_BUSY:        return HAL_SIMCOM_A76XX_NOT_READY;
        default:                       return HAL_SIMCOM_A76XX_ERROR;
    }
}

/* ── MQTT-RX URC handlers ────────────────────────────────────────────── */

static void rx_reset(hal_simcom_a76xx_impl_t *h) {
    h->rx.in_progress = false;
    h->rx.client_index = -1;
    h->rx.topic_len_announced = 0;
    h->rx.payload_len_announced = 0;
    h->rx.topic_len = 0;
    h->rx.payload_len = 0;
    h->rx.topic[0] = '\0';
    h->rx.expect_topic_line = false;
    h->rx.expect_payload_line = false;
    h->rx.complete = false;
}

static void on_urc_rxstart(const char *line, void *user) {
    hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
    /* +CMQTTRXSTART: <idx>,<topic_len>,<payload_len> */
    int idx = 0, tl = 0, pl = 0;
    if (sscanf(line, "+CMQTTRXSTART: %d,%d,%d", &idx, &tl, &pl) >= 1) {
        rx_reset(h);
        h->rx.in_progress = true;
        h->rx.client_index = idx;
        h->rx.topic_len_announced = (tl > 0) ? (size_t)tl : 0u;
        h->rx.payload_len_announced = (pl > 0) ? (size_t)pl : 0u;
    }
}

static void on_urc_rxtopic(const char *line, void *user) {
    hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
    if (!h->rx.in_progress) return;
    /* +CMQTTRXTOPIC: <idx>,<topic_len>  — the actual topic is on the
       next non-URC line. Mark that we expect it. */
    int idx = 0, tl = 0;
    if (sscanf(line, "+CMQTTRXTOPIC: %d,%d", &idx, &tl) >= 1) {
        h->rx.topic_len_announced = (tl > 0) ? (size_t)tl : h->rx.topic_len_announced;
        h->rx.expect_topic_line = true;
    }
}

static void on_urc_rxpayload(const char *line, void *user) {
    hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
    if (!h->rx.in_progress) return;
    int idx = 0, pl = 0;
    if (sscanf(line, "+CMQTTRXPAYLOAD: %d,%d", &idx, &pl) >= 1) {
        h->rx.payload_len_announced = (pl > 0) ? (size_t)pl : h->rx.payload_len_announced;
        h->rx.expect_payload_line = true;
    }
}

static void on_urc_rxend(const char *line, void *user) {
    (void)line;
    hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
    if (!h->rx.in_progress) return;
    h->rx.complete = true;
}

/* Wildcard handler installed at "" — every URC line reaches us; we
   use it to capture the topic and payload text that follow the
   RXTOPIC / RXPAYLOAD announcements. Lines starting with '+' are
   skipped here (they go through the dedicated handlers). */
static void on_urc_any(const char *line, void *user) {
    hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
    if (!h->rx.in_progress) return;
    if (line[0] == '+' || line[0] == '\0') return;

    if (h->rx.expect_topic_line) {
        size_t n = strlen(line);
        if (n >= sizeof(h->rx.topic)) n = sizeof(h->rx.topic) - 1u;
        memcpy(h->rx.topic, line, n);
        h->rx.topic[n] = '\0';
        h->rx.topic_len = n;
        h->rx.expect_topic_line = false;
        return;
    }
    if (h->rx.expect_payload_line) {
        size_t n = strlen(line);
        if (n > sizeof(h->rx.payload)) n = sizeof(h->rx.payload);
        memcpy(h->rx.payload, line, n);
        h->rx.payload_len = n;
        h->rx.expect_payload_line = false;
        return;
    }
}

static void on_urc_disconn(const char *line, void *user) {
    (void)line;
    hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
    /* +CMQTTCONNLOST: <client_index>,<cause>  — best effort: clear both
       client flags so the application notices and reconnects. */
    int idx = 0, cause = 0;
    if (sscanf(line, "+CMQTTCONNLOST: %d,%d", &idx, &cause) >= 1 &&
        idx >= 0 && idx < 2) {
        h->mqtt_connected[idx] = false;
    } else {
        h->mqtt_connected[0] = false;
        h->mqtt_connected[1] = false;
    }
}

static void install_mqtt_urcs(hal_simcom_a76xx_impl_t *h) {
    hal_modem_at_urc_register(h->at, "+CMQTTRXSTART:",   on_urc_rxstart,   h);
    hal_modem_at_urc_register(h->at, "+CMQTTRXTOPIC:",   on_urc_rxtopic,   h);
    hal_modem_at_urc_register(h->at, "+CMQTTRXPAYLOAD:", on_urc_rxpayload, h);
    hal_modem_at_urc_register(h->at, "+CMQTTRXEND:",     on_urc_rxend,     h);
    hal_modem_at_urc_register(h->at, "+CMQTTCONNLOST:",  on_urc_disconn,   h);
    /* Raw line observer captures the bare topic/payload lines that the
       SimCom CMQTTRX family emits between the announcement URCs. */
    hal_modem_at_set_line_observer(h->at, on_urc_any, h);
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

hal_simcom_a76xx_t hal_simcom_a76xx_create(const hal_simcom_a76xx_config_t *cfg) {
    if (!cfg || !cfg->uart || !cfg->rx_buf || cfg->rx_buf_size < 256u) {
        return NULL;
    }

    hal_critical_section_enter();
    int slot = -1;
    for (int i = 0; i < HAL_SIMCOM_A76XX_MAX_INSTANCES; i++) {
        if (!s_pool[i].in_use) { slot = i; s_pool[i].in_use = true; break; }
    }
    hal_critical_section_exit();
    if (slot < 0) return NULL;

    hal_simcom_a76xx_impl_t *h = &s_pool[slot];
    memset(h, 0, sizeof(*h));
    h->in_use = true;
    h->cfg = *cfg;
    h->mqtt_active_client = -1;
    rx_reset(h);

    hal_modem_at_config_t atc;
    memset(&atc, 0, sizeof(atc));
    atc.uart = cfg->uart;
    atc.rx_buf = cfg->rx_buf;
    atc.rx_buf_size = cfg->rx_buf_size;
    atc.default_timeout_ms = cfg->default_at_timeout_ms ? cfg->default_at_timeout_ms : 2000u;
    atc.quiet_window_ms = 200u;

    h->at = hal_modem_at_create(&atc);
    if (!h->at) {
        hal_critical_section_enter();
        h->in_use = false;
        hal_critical_section_exit();
        return NULL;
    }

    install_mqtt_urcs(h);
    return h;
}

void hal_simcom_a76xx_destroy(hal_simcom_a76xx_t h) {
    if (!h || !h->in_use) return;
    if (h->at) {
        hal_modem_at_destroy(h->at);
        h->at = NULL;
    }
    hal_critical_section_enter();
    h->in_use = false;
    hal_critical_section_exit();
}

/* ── Power ───────────────────────────────────────────────────────────── */

hal_simcom_a76xx_result_t hal_simcom_a76xx_power_toggle(hal_simcom_a76xx_t h, uint32_t pulse_ms) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (h->cfg.pwr_pin < 0) return HAL_SIMCOM_A76XX_OK;
    hal_gpio_set_mode((uint8_t)h->cfg.pwr_pin, HAL_GPIO_OUTPUT);
    hal_gpio_write((uint8_t)h->cfg.pwr_pin, false);
    hal_modem_at_sleep_ms(h->at, pulse_ms ? pulse_ms : 1500u);
    hal_gpio_write((uint8_t)h->cfg.pwr_pin, true);
    return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_hard_reset(hal_simcom_a76xx_t h) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (h->cfg.pwr_pin < 0) return HAL_SIMCOM_A76XX_OK;
    (void)hal_simcom_a76xx_power_toggle(h, 1500u);
    hal_modem_at_sleep_ms(h->at, 5000u);
    (void)hal_simcom_a76xx_power_toggle(h, 1500u);
    hal_modem_at_sleep_ms(h->at, 5000u);
    return HAL_SIMCOM_A76XX_OK;
}

/* ── Boot wait ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t start_ms;
    uint32_t last_rx_ms;
    bool     saw_ready;
    bool     saw_done;
    bool     saw_any;
    uint32_t ready_at_ms;
} boot_state_t;

static bool boot_ready(const char *buf, size_t len, void *user) {
    boot_state_t *st = (boot_state_t *)user;
    (void)len;

    if (!st->saw_done && (strstr(buf, "PB DONE") || strstr(buf, "SMS DONE"))) {
        st->saw_done = true;
    }
    if (!st->saw_ready &&
        (strstr(buf, "*ATREADY") || strstr(buf, "+CPIN: READY"))) {
        st->saw_ready = true;
        st->ready_at_ms = hal_millis();
    }
    if (buf[0] != '\0') st->saw_any = true;
    st->last_rx_ms = hal_millis();

    if (st->saw_done) return true;
    if (st->saw_ready && (hal_millis() - st->ready_at_ms) >= 2000u) return true;
    if (st->saw_any && (hal_millis() - st->last_rx_ms) >= 3000u)    return true;
    return false;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_boot(hal_simcom_a76xx_t h,
                                                     uint32_t total_timeout_ms) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;
    boot_state_t st;
    memset(&st, 0, sizeof(st));
    st.start_ms = hal_millis();
    st.last_rx_ms = st.start_ms;
    return map_at(hal_modem_at_listen_until(h->at, boot_ready, &st, total_timeout_ms));
}

/* ── Init / SIM / Network ────────────────────────────────────────────── */

hal_simcom_a76xx_result_t hal_simcom_a76xx_init(hal_simcom_a76xx_t h) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;

    bool at_ok = false;
    for (int i = 0; i < 10; i++) {
        if (hal_modem_at_send(h->at, "AT", "OK", 2000u) == HAL_MODEM_AT_OK) {
            at_ok = true;
            break;
        }
        hal_modem_at_sleep_ms(h->at, 1000u);
    }
    if (!at_ok) return HAL_SIMCOM_A76XX_TIMEOUT;

    (void)hal_modem_at_send(h->at, "ATE0", "OK", 3000u);
    /* AT+CLTS is firmware-variant dependent; ignore failures. */
    (void)hal_modem_at_send(h->at, "AT+CLTS=1", "OK", 3000u);
    (void)hal_modem_at_send(h->at, "AT+CEREG=0", "OK", 3000u);

    return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_sim_ready(hal_simcom_a76xx_t h,
                                                          uint32_t timeout_ms) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;
    uint32_t start = hal_millis();
    do {
        if (hal_modem_at_send(h->at, "AT+CPIN?", "READY", 5000u) == HAL_MODEM_AT_OK) {
            return HAL_SIMCOM_A76XX_OK;
        }
        hal_modem_at_sleep_ms(h->at, 1000u);
    } while ((hal_millis() - start) < timeout_ms);
    return HAL_SIMCOM_A76XX_TIMEOUT;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_network_registered(hal_simcom_a76xx_t h,
                                                                    uint32_t timeout_ms) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;
    uint32_t start = hal_millis();
    do {
        if (hal_modem_at_send(h->at, "AT+CREG?", "OK", 2000u) == HAL_MODEM_AT_OK) {
            const char *r = hal_modem_at_last_response(h->at);
            if (r && (strstr(r, "+CREG: 0,1") || strstr(r, "+CREG: 0,5"))) {
                return HAL_SIMCOM_A76XX_OK;
            }
        }
        hal_modem_at_sleep_ms(h->at, 2000u);
    } while ((hal_millis() - start) < timeout_ms);
    return HAL_SIMCOM_A76XX_TIMEOUT;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_attach_pdp(hal_simcom_a76xx_t h,
                                                      const hal_simcom_a76xx_apn_t *apn) {
    if (!h || !apn || !apn->apn || !*apn->apn) return HAL_SIMCOM_A76XX_INVALID_ARG;

    char cmd[160];
    int n = snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn->apn);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    hal_modem_at_result_t r = hal_modem_at_send(h->at, cmd, "OK", 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    r = hal_modem_at_send(h->at, "AT+CGACT=1,1", "OK", 10000u);
    return map_at(r);
}

/* ── Network time ────────────────────────────────────────────────────── */

hal_simcom_a76xx_result_t hal_simcom_a76xx_get_network_time_iso8601(hal_simcom_a76xx_t h,
                                                                     char *out,
                                                                     size_t out_size) {
    if (!h || !out || out_size < 26u) return HAL_SIMCOM_A76XX_INVALID_ARG;

    hal_modem_at_result_t r = hal_modem_at_send(h->at, "AT+CCLK?", "+CCLK:", 3000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    const char *resp = hal_modem_at_last_response(h->at);
    if (!resp) return HAL_SIMCOM_A76XX_PARSE;

    const char *p = strstr(resp, "+CCLK: \"");
    if (!p) p = strstr(resp, "+CCLK:\"");
    if (!p) return HAL_SIMCOM_A76XX_PARSE;
    p = strchr(p, '"');
    if (!p) return HAL_SIMCOM_A76XX_PARSE;
    p++;

    int yy, mo, dd, hh, mm, ss;
    if (sscanf(p, "%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) != 6) {
        return HAL_SIMCOM_A76XX_PARSE;
    }

    char tz_sign = '+';
    int tz_h = 0, tz_m = 0;
    if (strlen(p) > 17u && (p[17] == '+' || p[17] == '-')) {
        tz_sign = p[17];
        int tz_quarters = atoi(p + 18);
        if (tz_quarters < 0)  tz_quarters = 0;
        if (tz_quarters > 56) tz_quarters = 56;
        int tz_total_min = tz_quarters * 15;
        tz_h = tz_total_min / 60;
        tz_m = tz_total_min % 60;
    }

    int wn = snprintf(out, out_size,
                      "20%02d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                      yy, mo, dd, hh, mm, ss, tz_sign, tz_h, tz_m);
    if (wn <= 0 || (size_t)wn >= out_size) return HAL_SIMCOM_A76XX_INVALID_ARG;
    return HAL_SIMCOM_A76XX_OK;
}

hal_modem_at_t hal_simcom_a76xx_get_at(hal_simcom_a76xx_t h) {
    return h ? h->at : NULL;
}

/* ── MQTT ────────────────────────────────────────────────────────────── */

static hal_simcom_a76xx_result_t apply_ssl(hal_simcom_a76xx_t h,
                                           const hal_simcom_a76xx_ssl_config_t *s) {
    char cmd[128];
    int n;

    n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"sslversion\",%d,%d",
                 s->ssl_context_id, s->sslversion ? s->sslversion : 4);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

    n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"authmode\",%d,%d",
                 s->ssl_context_id, s->authmode ? s->authmode : 1);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

    if (s->ca_cert_name && *s->ca_cert_name) {
        n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"cacert\",%d,\"%s\"",
                     s->ssl_context_id, s->ca_cert_name);
        if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
        (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);
    }

    n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"ignorelocaltime\",%d,%d",
                 s->ssl_context_id, s->ignore_local_time ? 1 : 0);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

    n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"enableSNI\",%d,%d",
                 s->ssl_context_id, s->enable_sni ? 1 : 0);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

    return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_connect(hal_simcom_a76xx_t h,
                                                        const hal_simcom_a76xx_mqtt_config_t *cfg) {
    if (!h || !cfg || !cfg->broker_host || !cfg->client_id) return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (cfg->client_index < 0 || cfg->client_index > 1)     return HAL_SIMCOM_A76XX_INVALID_ARG;

    char cmd[320];
    int n;
    int ci = cfg->client_index;

    /* Tear down any previous session (errors ignored). */
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTDISC=%d,10", ci);
    if (n > 0) (void)hal_modem_at_send(h->at, cmd, "OK", 3000u);
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTREL=%d", ci);
    if (n > 0) (void)hal_modem_at_send(h->at, cmd, "OK", 2000u);
    (void)hal_modem_at_send(h->at, "AT+CMQTTSTOP", "OK", 3000u);
    hal_modem_at_sleep_ms(h->at, 1000u);

    if (cfg->ssl.enabled) {
        hal_simcom_a76xx_result_t sr = apply_ssl(h, &cfg->ssl);
        if (sr != HAL_SIMCOM_A76XX_OK) return sr;
    }

    hal_modem_at_result_t r = hal_modem_at_send(h->at, "AT+CMQTTSTART",
                                                "+CMQTTSTART: 0", 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=%d,\"%s\",%d",
                 ci, cfg->client_id, cfg->ssl.enabled ? 1 : 0);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    r = hal_modem_at_send(h->at, cmd, "OK", 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    if (cfg->ssl.enabled) {
        n = snprintf(cmd, sizeof(cmd), "AT+CMQTTSSLCFG=%d,%d",
                     ci, cfg->ssl.ssl_context_id);
        if (n > 0) (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);
    }

    char expected[32];
    snprintf(expected, sizeof(expected), "+CMQTTCONNECT: %d,0", ci);

    if (cfg->username && cfg->password) {
        n = snprintf(cmd, sizeof(cmd),
                     "AT+CMQTTCONNECT=%d,\"tcp://%s:%u\",%u,%d,\"%s\",\"%s\"",
                     ci, cfg->broker_host, (unsigned)cfg->broker_port,
                     (unsigned)cfg->keepalive_s, cfg->clean_session ? 1 : 0,
                     cfg->username, cfg->password);
    } else {
        n = snprintf(cmd, sizeof(cmd),
                     "AT+CMQTTCONNECT=%d,\"tcp://%s:%u\",%u,%d",
                     ci, cfg->broker_host, (unsigned)cfg->broker_port,
                     (unsigned)cfg->keepalive_s, cfg->clean_session ? 1 : 0);
    }
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;

    r = hal_modem_at_send(h->at, cmd, expected, 15000u);
    if (r != HAL_MODEM_AT_OK) {
        h->mqtt_connected[ci] = false;
        return map_at(r);
    }

    h->mqtt_connected[ci] = true;
    h->mqtt_active_client = ci;
    return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_disconnect(hal_simcom_a76xx_t h,
                                                           int client_index) {
    if (!h || client_index < 0 || client_index > 1) return HAL_SIMCOM_A76XX_INVALID_ARG;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTDISC=%d,10", client_index);
    hal_modem_at_result_t r = hal_modem_at_send(h->at, cmd, "OK", 5000u);
    h->mqtt_connected[client_index] = false;
    return map_at(r);
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_publish(hal_simcom_a76xx_t h,
                                                        int client_index,
                                                        const char *topic,
                                                        const void *payload,
                                                        size_t payload_len,
                                                        int qos) {
    if (!h || client_index < 0 || client_index > 1) return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (!topic || (!payload && payload_len > 0))   return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (qos < 0 || qos > 2)                         return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (!h->mqtt_connected[client_index])           return HAL_SIMCOM_A76XX_NOT_READY;

    char cmd[64];
    int n;
    hal_modem_at_result_t r;

    /* Topic */
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=%d,%u",
                 client_index, (unsigned)strlen(topic));
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    r = hal_modem_at_send_with_data(h->at, cmd,
                                    (const uint8_t *)topic, strlen(topic),
                                    1000u, 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    /* Payload */
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=%d,%u",
                 client_index, (unsigned)payload_len);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    r = hal_modem_at_send_with_data(h->at, cmd,
                                    (const uint8_t *)payload, payload_len,
                                    1000u, 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    /* Pub. retained=0, timeout=60s. */
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=%d,%d,60", client_index, qos);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    char expected[32];
    snprintf(expected, sizeof(expected), "+CMQTTPUB: %d,0", client_index);
    r = hal_modem_at_send(h->at, cmd, expected, 10000u);
    return map_at(r);
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_subscribe(hal_simcom_a76xx_t h,
                                                          int client_index,
                                                          const char *topic,
                                                          int qos) {
    if (!h || client_index < 0 || client_index > 1) return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (!topic || !*topic)                          return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (qos < 0 || qos > 2)                         return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (!h->mqtt_connected[client_index])           return HAL_SIMCOM_A76XX_NOT_READY;

    char cmd[64];
    int n = snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=%d,%u,%d",
                     client_index, (unsigned)strlen(topic), qos);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    hal_modem_at_result_t r = hal_modem_at_send_with_data(h->at, cmd,
                                                          (const uint8_t *)topic,
                                                          strlen(topic),
                                                          1000u, 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTSUB=%d", client_index);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    char expected[32];
    snprintf(expected, sizeof(expected), "+CMQTTSUB: %d,0", client_index);
    r = hal_modem_at_send(h->at, cmd, expected, 10000u);
    return map_at(r);
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_unsubscribe(hal_simcom_a76xx_t h,
                                                            int client_index,
                                                            const char *topic) {
    if (!h || client_index < 0 || client_index > 1) return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (!topic || !*topic)                          return HAL_SIMCOM_A76XX_INVALID_ARG;
    if (!h->mqtt_connected[client_index])           return HAL_SIMCOM_A76XX_NOT_READY;

    char cmd[64];
    int n = snprintf(cmd, sizeof(cmd), "AT+CMQTTUNSUBTOPIC=%d,%u",
                     client_index, (unsigned)strlen(topic));
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    hal_modem_at_result_t r = hal_modem_at_send_with_data(h->at, cmd,
                                                          (const uint8_t *)topic,
                                                          strlen(topic),
                                                          1000u, 5000u);
    if (r != HAL_MODEM_AT_OK) return map_at(r);

    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTUNSUB=%d", client_index);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return HAL_SIMCOM_A76XX_INVALID_ARG;
    char expected[32];
    snprintf(expected, sizeof(expected), "+CMQTTUNSUB: %d,0", client_index);
    r = hal_modem_at_send(h->at, cmd, expected, 10000u);
    return map_at(r);
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_set_message_callback(hal_simcom_a76xx_t h,
                                                                     hal_simcom_a76xx_mqtt_message_cb_t cb,
                                                                     void *user) {
    if (!h) return HAL_SIMCOM_A76XX_INVALID_ARG;
    h->msg_cb = cb;
    h->msg_cb_user = user;
    return HAL_SIMCOM_A76XX_OK;
}

int hal_simcom_a76xx_mqtt_poll(hal_simcom_a76xx_t h) {
    if (!h) return 0;
    h->dispatched_in_poll = 0;
    (void)hal_modem_at_urc_poll(h->at);

    if (h->rx.complete) {
        if (h->msg_cb && h->rx.topic_len > 0) {
            h->msg_cb(h->rx.client_index, h->rx.topic,
                      h->rx.payload, h->rx.payload_len,
                      h->msg_cb_user);
        }
        rx_reset(h);
        h->dispatched_in_poll++;
    }

    return h->dispatched_in_poll;
}

bool hal_simcom_a76xx_mqtt_is_connected(hal_simcom_a76xx_t h, int client_index) {
    if (!h || client_index < 0 || client_index > 1) return false;
    return h->mqtt_connected[client_index];
}

#endif /* HAL_ENABLE_A7670 */
