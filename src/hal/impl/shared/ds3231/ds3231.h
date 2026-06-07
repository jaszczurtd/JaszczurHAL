/*
 * ds3231.h
 *
 * Portable DS3231 real-time clock driver built on JaszczurHAL I2C
 * primitives. This implementation follows the register map and control flow
 * of the original driver by Eric Ayars and later contributors, but removes
 * all direct Arduino dependencies.
 */

#pragma once

#include <stdint.h>
#include <time.h>

typedef uint8_t byte;

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__cplusplus)
}
#endif

#ifdef __cplusplus

#include <stdbool.h>

class DateTime {
public:
    DateTime(uint32_t t = 0u);
    DateTime(uint16_t year, uint8_t month, uint8_t day,
             uint8_t hour = 0u, uint8_t min = 0u, uint8_t sec = 0u);
    DateTime(const char *date, const char *time);

    uint16_t year() const { return (uint16_t)(2000u + yOff); }
    uint8_t month() const { return m; }
    uint8_t day() const { return d; }
    uint8_t hour() const { return hh; }
    uint8_t minute() const { return mm; }
    uint8_t second() const { return ss; }
    uint8_t dayOfTheWeek() const;

    long secondstime() const;
    uint32_t unixtime(void) const;

protected:
    uint8_t yOff, m, d, hh, mm, ss;
};

bool isleapYear(const uint16_t y);

class DS3231 {
public:
    DS3231(uint8_t i2c_bus = 0u, uint8_t i2c_addr = 0x68u);

    byte getSecond();
    byte getMinute();
    byte getHour(bool &h12, bool &PM_time);
    byte getDoW();
    byte getDate();
    byte getMonth(bool &Century);
    byte getYear();

    void setEpoch(time_t epoch = 0, bool flag_localtime = false);
    void adjust(const DateTime &dt);
    void setSecond(byte Second);
    void setMinute(byte Minute);
    void setHour(byte Hour);
    void setDoW(byte DoW);
    void setDate(byte Date);
    void setMonth(byte Month);
    void setYear(byte Year);
    void setClockMode(bool h12);

    float getTemperature();

    void getA1Time(byte &A1Day, byte &A1Hour, byte &A1Minute, byte &A1Second,
                   byte &AlarmBits, bool &A1Dy, bool &A1h12, bool &A1PM);
    void getA2Time(byte &A2Day, byte &A2Hour, byte &A2Minute,
                   byte &AlarmBits, bool &A2Dy, bool &A2h12, bool &A2PM);
    void getA1Time(byte &A1Day, byte &A1Hour, byte &A1Minute, byte &A1Second,
                   byte &AlarmBits, bool &A1Dy, bool &A1h12, bool &A1PM, bool clearAlarmBits);
    void getA2Time(byte &A1Day, byte &A1Hour, byte &A1Minute,
                   byte &AlarmBits, bool &A1Dy, bool &A1h12, bool &A1PM, bool clearAlarmBits);
    void setA1Time(byte A1Day, byte A1Hour, byte A1Minute, byte A1Second,
                   byte AlarmBits, bool A1Dy, bool A1h12, bool A1PM);
    void setA2Time(byte A2Day, byte A2Hour, byte A2Minute,
                   byte AlarmBits, bool A2Dy, bool A2h12, bool A2PM);
    void setAlarm1Simple(byte hour, byte minute);
    void setAlarm2Simple(byte hour, byte minute);
    void turnOnAlarm(byte Alarm);
    void turnOffAlarm(byte Alarm);
    bool checkAlarmEnabled(byte Alarm);
    bool checkIfAlarm(byte Alarm);
    bool checkIfAlarm(byte Alarm, bool clearflag);

    void enableOscillator(bool TF, bool battery, byte frequency);
    void enable32kHz(bool TF);
    bool oscillatorCheck();

private:
    friend class RTClib;

    uint8_t _i2c_bus;
    uint8_t _i2c_addr;

    static uint8_t dowToDS3231(uint8_t d) { return d == 0u ? 7u : d; }
    byte decToBcd(byte val);
    byte bcdToDec(byte val);

    bool readBytes(byte reg, byte *data, uint8_t count);
    bool writeBytes(byte reg, const byte *data, uint8_t count);
    byte readControlByte(bool which);
    void writeControlByte(byte control, bool which);
};

class RTClib {
public:
    static DateTime now(DS3231 &rtc);
};

#endif /* __cplusplus */
