#include "../../hal_config.h"
#ifndef HAL_DISABLE_GPS

#include "../../hal_gps.h"
#include "../../hal_swserial.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "drivers/TinyGPSPlus/src/TinyGPS++.h"

static TinyGPSPlus s_gps;
static hal_swserial_t s_serial = NULL;
static hal_mutex_t s_gps_mutex = NULL;

static void gps_ensure_mutex(void) {
    if (!s_gps_mutex) {
        hal_critical_section_enter();
        if (!s_gps_mutex) {
            s_gps_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

/* ── Auto-detect state ──────────────────────────────────────────────── */
#define GPS_AUTODETECT_CHARS 500
static uint8_t  s_rx_pin;
static uint8_t  s_tx_pin;
static uint32_t s_baud;
static uint16_t s_config;
static bool     s_autodetect_done = false;

static void gps_reinit_serial(uint16_t config) {
    if (s_serial) {
        hal_swserial_destroy(s_serial);
        s_serial = NULL;
    }
    s_config = config;
    s_serial = hal_swserial_create(s_rx_pin, s_tx_pin);
    if (!s_serial) {
        hal_derr_limited("gps", "reinit failed: swserial create returned NULL");
        return;
    }
    hal_swserial_begin(s_serial, s_baud, s_config);
}

void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config) {
    if (s_serial) return;

    s_rx_pin = rx_pin;
    s_tx_pin = tx_pin;
    s_baud   = baud;
    s_config = config;

    s_serial = hal_swserial_create(rx_pin, tx_pin);
    if (!s_serial) {
        hal_derr_limited("gps", "init failed: swserial create returned NULL");
        return;
    }

    hal_swserial_begin(s_serial, baud, config);
}

void hal_gps_update(void) {
    if (!s_serial) {
        hal_derr_limited("gps", "update failed: swserial not initialized");
        return;
    }

    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);

    while (hal_swserial_available(s_serial) > 0) {
        s_gps.encode((char)hal_swserial_read(s_serial));
    }

    /* Auto-detect: if after GPS_AUTODETECT_CHARS bytes every sentence
       failed checksum, toggle between 8N1 and 7N1 and retry once. */
    if (!s_autodetect_done &&
        s_gps.charsProcessed() >= GPS_AUTODETECT_CHARS) {
        if (s_gps.passedChecksum() == 0 && s_gps.failedChecksum() > 0) {
            uint16_t alt = (s_config == HAL_UART_CFG_8N1)
                         ? HAL_UART_CFG_7N1 : HAL_UART_CFG_8N1;
            hal_deb("gps: 0 passed / %lu failed with 0x%04X, switching to 0x%04X",
                     (unsigned long)s_gps.failedChecksum(),
                     (unsigned)s_config, (unsigned)alt);
            s_gps = TinyGPSPlus();
            gps_reinit_serial(alt);
        }
        s_autodetect_done = true;
    }

    hal_mutex_unlock(s_gps_mutex);
}

void hal_gps_encode(char c) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    s_gps.encode(c);
    hal_mutex_unlock(s_gps_mutex);
}

uint32_t hal_gps_chars_processed(void)   { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); uint32_t v = s_gps.charsProcessed();   hal_mutex_unlock(s_gps_mutex); return v; }
uint32_t hal_gps_passed_checksum(void)   { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); uint32_t v = s_gps.passedChecksum();   hal_mutex_unlock(s_gps_mutex); return v; }
uint32_t hal_gps_failed_checksum(void)   { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); uint32_t v = s_gps.failedChecksum();   hal_mutex_unlock(s_gps_mutex); return v; }
uint32_t hal_gps_sentences_with_fix(void){ gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); uint32_t v = s_gps.sentencesWithFix(); hal_mutex_unlock(s_gps_mutex); return v; }
int      hal_gps_serial_available(void)  { return s_serial ? hal_swserial_available(s_serial) : -1; }

bool hal_gps_location_is_valid(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    bool v = s_gps.location.isValid();
    hal_mutex_unlock(s_gps_mutex);
    return v;
}

bool hal_gps_location_is_updated(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    bool v = s_gps.location.isUpdated();
    hal_mutex_unlock(s_gps_mutex);
    return v;
}

uint32_t hal_gps_location_age(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    uint32_t v = s_gps.location.age();
    hal_mutex_unlock(s_gps_mutex);
    return v;
}

double hal_gps_latitude(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    double v = s_gps.location.lat();
    hal_mutex_unlock(s_gps_mutex);
    return v;
}

double hal_gps_longitude(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    double v = s_gps.location.lng();
    hal_mutex_unlock(s_gps_mutex);
    return v;
}

double hal_gps_speed_kmph(void) {
    gps_ensure_mutex();
    hal_mutex_lock(s_gps_mutex);
    double v = s_gps.speed.kmph();
    hal_mutex_unlock(s_gps_mutex);
    return v;
}

int hal_gps_date_year(void)   { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); int v = s_gps.date.year();   hal_mutex_unlock(s_gps_mutex); return v; }
int hal_gps_date_month(void)  { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); int v = s_gps.date.month();  hal_mutex_unlock(s_gps_mutex); return v; }
int hal_gps_date_day(void)    { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); int v = s_gps.date.day();    hal_mutex_unlock(s_gps_mutex); return v; }
int hal_gps_time_hour(void)   { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); int v = s_gps.time.hour();   hal_mutex_unlock(s_gps_mutex); return v; }
int hal_gps_time_minute(void) { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); int v = s_gps.time.minute(); hal_mutex_unlock(s_gps_mutex); return v; }
int hal_gps_time_second(void) { gps_ensure_mutex(); hal_mutex_lock(s_gps_mutex); int v = s_gps.time.second(); hal_mutex_unlock(s_gps_mutex); return v; }


#endif /* HAL_DISABLE_GPS */
