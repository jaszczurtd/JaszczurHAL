/*
 * ds3231.cpp
 *
 * Portable DS3231 real-time clock driver built on JaszczurHAL I2C
 * primitives. The control flow, register layout, and public behavior follow
 * the original Eric Ayars / JeeLabs / RTClib-style driver, while replacing
 * Arduino Wire access with HAL I2C calls.
 */

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_RTC) && defined(HAL_ENABLE_DS3231)

#include "ds3231.h"

#include "../../../hal_i2c.h"

#include <cstdio>
#include <cstring>

#define CLOCK_ADDRESS 0x68u
#define SECONDS_FROM_1970_TO_2000 946684800u

static const uint8_t daysInMonth[] = { 31u,28u,31u,30u,31u,30u,31u,31u,30u,31u,30u,31u };

static uint16_t date2days(uint16_t y, uint8_t m, uint8_t d) {
    if (y >= 2000u) {
        y = (uint16_t)(y - 2000u);
    }

    uint16_t days = d;
    for (uint8_t i = 1u; i < m; ++i) {
        days = (uint16_t)(days + daysInMonth[i - 1u]);
    }
    if (m > 2u && isleapYear(y)) {
        ++days;
    }
    return (uint16_t)(days + (uint16_t)(365u * y) + (uint16_t)((y + 3u) / 4u) - 1u);
}

static long time2long(uint16_t days, uint8_t h, uint8_t m, uint8_t s) {
    return ((days * 24L + h) * 60L + m) * 60L + s;
}

DateTime::DateTime(uint32_t t) {
    t -= SECONDS_FROM_1970_TO_2000;

    ss = (uint8_t)(t % 60u);
    t /= 60u;
    mm = (uint8_t)(t % 60u);
    t /= 60u;
    hh = (uint8_t)(t % 24u);
    uint16_t days = (uint16_t)(t / 24u);
    uint8_t leap = 0u;
    for (yOff = 0u; ; ++yOff) {
        leap = isleapYear((uint16_t)yOff) ? 1u : 0u;
        if (days < (uint16_t)(365u + leap)) {
            break;
        }
        days = (uint16_t)(days - (365u + leap));
    }
    for (m = 1u; ; ++m) {
        uint8_t daysPerMonth = daysInMonth[m - 1u];
        if (leap && m == 2u) {
            ++daysPerMonth;
        }
        if (days < daysPerMonth) {
            break;
        }
        days = (uint16_t)(days - daysPerMonth);
    }
    d = (uint8_t)(days + 1u);
}

DateTime::DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) {
    if (year >= 2000u) {
        year = (uint16_t)(year - 2000u);
    }
    yOff = (uint8_t)year;
    m = month;
    d = day;
    hh = hour;
    mm = min;
    ss = sec;
}

DateTime::DateTime(const char *date, const char *time) {
    static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static char buff[4] = {'0', '0', '0', '0'};
    int y = 0;
    std::sscanf(date, "%3s %hhu %d", buff, &d, &y);
    yOff = (uint8_t)(y >= 2000 ? (y - 2000) : y);
    const char *found = std::strstr(month_names, buff);
    m = (uint8_t)((found != nullptr) ? ((found - month_names) / 3 + 1) : 1);
    std::sscanf(time, "%hhu:%hhu:%hhu", &hh, &mm, &ss);
}

uint8_t DateTime::dayOfTheWeek() const {
    const uint16_t day = date2days(yOff, m, d);
    return (uint8_t)((day + 6u) % 7u);
}

uint32_t DateTime::unixtime(void) const {
    const uint16_t days = date2days(yOff, m, d);
    uint32_t t = (uint32_t)time2long(days, hh, mm, ss);
    t += SECONDS_FROM_1970_TO_2000;
    return t;
}

long DateTime::secondstime() const {
    return time2long(date2days(yOff, m, d), hh, mm, ss);
}

bool isleapYear(const uint16_t y) {
    if (y & 3u) {
        return false;
    }
    return (y % 100u) || (y % 400u == 0u);
}

DS3231::DS3231(uint8_t i2c_bus, uint8_t i2c_addr)
    : _i2c_bus(i2c_bus), _i2c_addr(i2c_addr) {}

bool DS3231::readBytes(byte reg, byte *data, uint8_t count) {
    if (!data) {
        return false;
    }

    hal_i2c_begin_transmission_bus(_i2c_bus, _i2c_addr);
    if (hal_i2c_write_bus(_i2c_bus, reg) != 1) {
        (void)hal_i2c_end_transmission_bus(_i2c_bus);
        return false;
    }
    if (hal_i2c_end_transmission_bus(_i2c_bus) != 0) {
        return false;
    }

    const int received = hal_i2c_request_from_bus(_i2c_bus, _i2c_addr, count);
    if (received < (int)count) {
        return false;
    }

    for (uint8_t i = 0u; i < count; ++i) {
        if (hal_i2c_available_bus(_i2c_bus) <= 0) {
            return false;
        }
        const int value = hal_i2c_read_bus(_i2c_bus);
        if (value < 0) {
            return false;
        }
        data[i] = (byte)value;
    }

    return true;
}

bool DS3231::writeBytes(byte reg, const byte *data, uint8_t count) {
    if (!data) {
        return false;
    }

    hal_i2c_begin_transmission_bus(_i2c_bus, _i2c_addr);
    if (hal_i2c_write_bus(_i2c_bus, reg) != 1) {
        (void)hal_i2c_end_transmission_bus(_i2c_bus);
        return false;
    }
    for (uint8_t i = 0u; i < count; ++i) {
        if (hal_i2c_write_bus(_i2c_bus, data[i]) != 1) {
            (void)hal_i2c_end_transmission_bus(_i2c_bus);
            return false;
        }
    }
    return hal_i2c_end_transmission_bus(_i2c_bus) == 0;
}

byte DS3231::decToBcd(byte val) {
    return (byte)(((val / 10u) * 16u) + (val % 10u));
}

byte DS3231::bcdToDec(byte val) {
    return (byte)(((val / 16u) * 10u) + (val % 16u));
}

DateTime RTClib::now(DS3231 &rtc) {
    byte buffer[7] = {0};
    if (!rtc.readBytes(0x00u, buffer, 7u)) {
        return DateTime(2000u, 1u, 1u, 0u, 0u, 0u);
    }

    const uint8_t ss = (uint8_t)(rtc.bcdToDec((byte)(buffer[0] & 0x7Fu)));
    const uint8_t mm = rtc.bcdToDec(buffer[1]);
    const uint8_t hh = rtc.bcdToDec(buffer[2]);
    const uint8_t d = rtc.bcdToDec(buffer[4]);
    const uint8_t m = rtc.bcdToDec(buffer[5] & 0x7Fu);
    const uint16_t y = (uint16_t)(rtc.bcdToDec(buffer[6]) + 2000u);

    return DateTime(y, m, d, hh, mm, ss);
}

byte DS3231::getSecond() {
    byte buffer = 0u;
    (void)readBytes(0x00u, &buffer, 1u);
    return bcdToDec((byte)(buffer & 0x7Fu));
}

byte DS3231::getMinute() {
    byte buffer = 0u;
    (void)readBytes(0x01u, &buffer, 1u);
    return bcdToDec(buffer);
}

byte DS3231::getHour(bool &h12, bool &PM_time) {
    byte temp_buffer = 0u;
    byte hour = 0u;
    (void)readBytes(0x02u, &temp_buffer, 1u);
    h12 = (temp_buffer & 0b01000000) != 0u;
    if (h12) {
        PM_time = (temp_buffer & 0b00100000) != 0u;
        hour = bcdToDec((byte)(temp_buffer & 0b00011111));
    } else {
        hour = bcdToDec((byte)(temp_buffer & 0b00111111));
    }
    return hour;
}

byte DS3231::getDoW() {
    byte buffer = 0u;
    (void)readBytes(0x03u, &buffer, 1u);
    return bcdToDec(buffer);
}

byte DS3231::getDate() {
    byte buffer = 0u;
    (void)readBytes(0x04u, &buffer, 1u);
    return bcdToDec(buffer);
}

byte DS3231::getMonth(bool &Century) {
    byte temp_buffer = 0u;
    (void)readBytes(0x05u, &temp_buffer, 1u);
    Century = (temp_buffer & 0b10000000) != 0u;
    return bcdToDec((byte)(temp_buffer & 0b01111111));
}

byte DS3231::getYear() {
    byte buffer = 0u;
    (void)readBytes(0x06u, &buffer, 1u);
    return bcdToDec(buffer);
}

void DS3231::setEpoch(time_t epoch, bool flag_localtime) {
#if defined(__AVR__)
    epoch -= SECONDS_FROM_1970_TO_2000;
#endif
    struct tm tmnow;
    if (flag_localtime) {
        localtime_r(&epoch, &tmnow);
    } else {
        gmtime_r(&epoch, &tmnow);
    }
    setSecond((byte)tmnow.tm_sec);
    setMinute((byte)tmnow.tm_min);
    setHour((byte)tmnow.tm_hour);
    setDoW((byte)(tmnow.tm_wday + 1U));
    setDate((byte)tmnow.tm_mday);
    setMonth((byte)(tmnow.tm_mon + 1U));
    setYear((byte)(tmnow.tm_year - 100U));
}

void DS3231::adjust(const DateTime &dt) {
    const byte data[] = {
        0x00u,
        decToBcd(dt.second()),
        decToBcd(dt.minute()),
        decToBcd(dt.hour()),
        decToBcd(dowToDS3231(dt.dayOfTheWeek())),
        decToBcd(dt.day()),
        decToBcd((byte)(dt.year() - 2000u)),
        0x00u,
    };

    hal_i2c_begin_transmission_bus(_i2c_bus, _i2c_addr);
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(data) / sizeof(data[0])); ++i) {
        if (hal_i2c_write_bus(_i2c_bus, data[i]) != 1) {
            (void)hal_i2c_end_transmission_bus(_i2c_bus);
            return;
        }
    }
    (void)hal_i2c_end_transmission_bus(_i2c_bus);
}

void DS3231::setSecond(byte Second) {
    const byte data[] = { 0x00u, decToBcd(Second) };
    (void)writeBytes(data[0], &data[1], 1u);
    const byte temp_buffer = readControlByte(1);
    writeControlByte((byte)(temp_buffer & 0b01111111), 1);
}

void DS3231::setMinute(byte Minute) {
    const byte data[] = { 0x01u, decToBcd(Minute) };
    (void)writeBytes(data[0], &data[1], 1u);
}

void DS3231::setHour(byte Hour) {
    bool h12 = false;
    byte temp_hour = 0u;

    (void)readBytes(0x02u, &temp_hour, 1u);
    h12 = (temp_hour & 0b01000000) != 0u;

    if (h12) {
        const bool am_pm = Hour > 11u;
        byte temp = Hour;
        if (temp > 11u) {
            temp = (byte)(temp - 12u);
        }
        if (temp == 0u) {
            temp = 12u;
        }
        temp_hour = (byte)(decToBcd(temp) | (am_pm ? 0b00100000 : 0u) | 0b01000000);
    } else {
        temp_hour = (byte)(decToBcd(Hour) & 0b10111111);
    }

    const byte data[] = { 0x02u, temp_hour };
    (void)writeBytes(data[0], &data[1], 1u);
}

void DS3231::setDoW(byte DoW) {
    const byte data[] = { 0x03u, decToBcd(DoW) };
    (void)writeBytes(data[0], &data[1], 1u);
}

void DS3231::setDate(byte Date) {
    const byte data[] = { 0x04u, decToBcd(Date) };
    (void)writeBytes(data[0], &data[1], 1u);
}

void DS3231::setMonth(byte Month) {
    const byte data[] = { 0x05u, decToBcd(Month) };
    (void)writeBytes(data[0], &data[1], 1u);
}

void DS3231::setYear(byte Year) {
    const byte data[] = { 0x06u, decToBcd(Year) };
    (void)writeBytes(data[0], &data[1], 1u);
}

void DS3231::setClockMode(bool h12) {
    byte temp_buffer = 0u;
    (void)readBytes(0x02u, &temp_buffer, 1u);
    if (h12) {
        temp_buffer = (byte)(temp_buffer | 0b01000000);
    } else {
        temp_buffer = (byte)(temp_buffer & 0b10111111);
    }
    const byte data[] = { 0x02u, temp_buffer };
    (void)writeBytes(data[0], &data[1], 1u);
}

float DS3231::getTemperature() {
    byte buffer[2] = {0u, 0u};
    if (!readBytes(0x11u, buffer, 2u)) {
        return -9999.0f;
    }

    const int16_t itemp = (int16_t)((buffer[0] << 8) | (buffer[1] & 0xC0u));
    return (float)itemp / 256.0f;
}

void DS3231::getA1Time(byte &A1Day, byte &A1Hour, byte &A1Minute, byte &A1Second, byte &AlarmBits, bool &A1Dy, bool &A1h12, bool &A1PM) {
    byte buffer[4] = {0u, 0u, 0u, 0u};
    (void)readBytes(0x07u, buffer, 4u);

    A1Second = bcdToDec((byte)(buffer[0] & 0b01111111));
    AlarmBits = (byte)(AlarmBits | ((buffer[0] & 0b10000000) >> 7));

    A1Minute = bcdToDec((byte)(buffer[1] & 0b01111111));
    AlarmBits = (byte)(AlarmBits | ((buffer[1] & 0b10000000) >> 6));

    A1h12 = (buffer[2] & 0b01000000) != 0u;
    AlarmBits = (byte)(AlarmBits | ((buffer[2] & 0b10000000) >> 5));
    if (A1h12) {
        A1PM = (buffer[2] & 0b00100000) != 0u;
        A1Hour = bcdToDec((byte)(buffer[2] & 0b00011111));
    } else {
        A1Hour = bcdToDec((byte)(buffer[2] & 0b00111111));
    }

    AlarmBits = (byte)(AlarmBits | ((buffer[3] & 0b10000000) >> 4));
    A1Dy = (buffer[3] & 0b01000000) != 0u;
    if (A1Dy) {
        A1Day = bcdToDec((byte)(buffer[3] & 0b00001111));
    } else {
        A1Day = bcdToDec((byte)(buffer[3] & 0b00111111));
    }
}

void DS3231::getA1Time(byte &A1Day, byte &A1Hour, byte &A1Minute, byte &A1Second, byte &AlarmBits, bool &A1Dy, bool &A1h12, bool &A1PM, bool clearAlarmBits) {
    if (clearAlarmBits) {
        AlarmBits = 0x0u;
    }
    getA1Time(A1Day, A1Hour, A1Minute, A1Second, AlarmBits, A1Dy, A1h12, A1PM);
}

void DS3231::getA2Time(byte &A2Day, byte &A2Hour, byte &A2Minute, byte &AlarmBits, bool &A2Dy, bool &A2h12, bool &A2PM) {
    byte buffer[3] = {0u, 0u, 0u};
    (void)readBytes(0x0Bu, buffer, 3u);

    A2Minute = bcdToDec((byte)(buffer[0] & 0b01111111));
    AlarmBits = (byte)(AlarmBits | ((buffer[0] & 0b10000000) >> 3));

    A2h12 = (buffer[1] & 0b01000000) != 0u;
    AlarmBits = (byte)(AlarmBits | ((buffer[1] & 0b10000000) >> 2));
    if (A2h12) {
        A2PM = (buffer[1] & 0b00100000) != 0u;
        A2Hour = bcdToDec((byte)(buffer[1] & 0b00011111));
    } else {
        A2Hour = bcdToDec((byte)(buffer[1] & 0b00111111));
    }

    AlarmBits = (byte)(AlarmBits | ((buffer[2] & 0b10000000) >> 1));
    A2Dy = (buffer[2] & 0b01000000) != 0u;
    if (A2Dy) {
        A2Day = bcdToDec((byte)(buffer[2] & 0b00001111));
    } else {
        A2Day = bcdToDec((byte)(buffer[2] & 0b00111111));
    }
}

void DS3231::getA2Time(byte &A2Day, byte &A2Hour, byte &A2Minute, byte &AlarmBits, bool &A2Dy, bool &A2h12, bool &A2PM, bool clearAlarmBits) {
    if (clearAlarmBits) {
        AlarmBits = 0x0u;
    }
    getA2Time(A2Day, A2Hour, A2Minute, AlarmBits, A2Dy, A2h12, A2PM);
}

void DS3231::setA1Time(byte A1Day, byte A1Hour, byte A1Minute, byte A1Second, byte AlarmBits, bool A1Dy, bool A1h12, bool A1PM) {
    byte temp_buffer = 0u;
    const byte data0[] = { 0x07u, (byte)(decToBcd(A1Second) | ((AlarmBits & 0b00000001u) << 7)) };
    (void)writeBytes(data0[0], &data0[1], 1u);

    const byte data1[] = { 0x08u, (byte)(decToBcd(A1Minute) | ((AlarmBits & 0b00000010u) << 6)) };
    (void)writeBytes(data1[0], &data1[1], 1u);

    if (A1h12) {
        if (A1Hour > 12u) {
            A1Hour = (byte)(A1Hour - 12u);
            A1PM = true;
        }
        if (A1PM) {
            temp_buffer = (byte)(decToBcd(A1Hour) | 0b01100000);
        } else {
            temp_buffer = (byte)(decToBcd(A1Hour) | 0b01000000);
        }
    } else {
        temp_buffer = decToBcd(A1Hour);
    }
    temp_buffer = (byte)(temp_buffer | ((AlarmBits & 0b00000100u) << 5));
    const byte data2[] = { 0x09u, temp_buffer };
    (void)writeBytes(data2[0], &data2[1], 1u);

    temp_buffer = (byte)(((AlarmBits & 0b00001000u) << 4) | decToBcd(A1Day));
    if (A1Dy) {
        temp_buffer = (byte)(temp_buffer | 0b01000000);
    }
    const byte data3[] = { 0x0Au, temp_buffer };
    (void)writeBytes(data3[0], &data3[1], 1u);
}

void DS3231::setA2Time(byte A2Day, byte A2Hour, byte A2Minute, byte AlarmBits, bool A2Dy, bool A2h12, bool A2PM) {
    byte temp_buffer = 0u;
    const byte data0[] = { 0x0Bu, (byte)(decToBcd(A2Minute) | ((AlarmBits & 0b00010000u) << 3)) };
    (void)writeBytes(data0[0], &data0[1], 1u);

    if (A2h12) {
        if (A2Hour > 12u) {
            A2Hour = (byte)(A2Hour - 12u);
            A2PM = true;
        }
        if (A2PM) {
            temp_buffer = (byte)(decToBcd(A2Hour) | 0b01100000);
        } else {
            temp_buffer = (byte)(decToBcd(A2Hour) | 0b01000000);
        }
    } else {
        temp_buffer = decToBcd(A2Hour);
    }
    temp_buffer = (byte)(temp_buffer | ((AlarmBits & 0b00100000u) << 2));
    const byte data1[] = { 0x0Cu, temp_buffer };
    (void)writeBytes(data1[0], &data1[1], 1u);

    temp_buffer = (byte)(((AlarmBits & 0b01000000u) << 1) | decToBcd(A2Day));
    if (A2Dy) {
        temp_buffer = (byte)(temp_buffer | 0b01000000);
    }
    const byte data2[] = { 0x0Du, temp_buffer };
    (void)writeBytes(data2[0], &data2[1], 1u);
}

void DS3231::setAlarm1Simple(byte hour, byte minute) {
    setA1Time(1u, hour, minute, 0u, 0b00001000u, false, false, false);
}

void DS3231::setAlarm2Simple(byte hour, byte minute) {
    setA2Time(1u, hour, minute, 0b01000000u, false, false, false);
}

void DS3231::turnOnAlarm(byte Alarm) {
    byte temp_buffer = readControlByte(0);
    if (Alarm == 1u) {
        temp_buffer = (byte)(temp_buffer | 0b00000101u);
    } else {
        temp_buffer = (byte)(temp_buffer | 0b00000110u);
    }
    writeControlByte(temp_buffer, 0);
}

void DS3231::turnOffAlarm(byte Alarm) {
    byte temp_buffer = readControlByte(0);
    if (Alarm == 1u) {
        temp_buffer = (byte)(temp_buffer & 0b11111110u);
    } else {
        temp_buffer = (byte)(temp_buffer & 0b11111101u);
    }
    writeControlByte(temp_buffer, 0);
}

bool DS3231::checkAlarmEnabled(byte Alarm) {
    const byte temp_buffer = readControlByte(0);
    if (Alarm == 1u) {
        return (temp_buffer & 0b00000001u) != 0u;
    }
    return (temp_buffer & 0b00000010u) != 0u;
}

bool DS3231::checkIfAlarm(byte Alarm) {
    return checkIfAlarm(Alarm, true);
}

bool DS3231::checkIfAlarm(byte Alarm, bool clearflag) {
    byte temp_buffer = readControlByte(1);
    const bool result = (Alarm == 1u)
        ? ((temp_buffer & 0b00000001u) != 0u)
        : ((temp_buffer & 0b00000010u) != 0u);

    if (Alarm == 1u) {
        temp_buffer = (byte)(temp_buffer & 0b11111110u);
    } else {
        temp_buffer = (byte)(temp_buffer & 0b11111101u);
    }
    if (clearflag) {
        writeControlByte(temp_buffer, 1);
    }
    return result;
}

void DS3231::enableOscillator(bool TF, bool battery, byte frequency) {
    if (frequency > 3u) {
        frequency = 3u;
    }
    byte temp_buffer = (byte)(readControlByte(0) & 0b11100111u);
    if (battery) {
        temp_buffer = (byte)(temp_buffer | 0b01000000u);
    } else {
        temp_buffer = (byte)(temp_buffer & 0b10111111u);
    }
    if (TF) {
        temp_buffer = (byte)(temp_buffer & 0b01111011u);
    } else {
        temp_buffer = (byte)(temp_buffer | 0b10000000u);
    }
    frequency = (byte)(frequency << 3);
    temp_buffer = (byte)(temp_buffer | frequency);
    writeControlByte(temp_buffer, 0);
}

void DS3231::enable32kHz(bool TF) {
    byte temp_buffer = readControlByte(1);
    if (TF) {
        temp_buffer = (byte)(temp_buffer | 0b00001000u);
    } else {
        temp_buffer = (byte)(temp_buffer & 0b11110111u);
    }
    writeControlByte(temp_buffer, 1);
}

bool DS3231::oscillatorCheck() {
    const byte temp_buffer = readControlByte(1);
    return (temp_buffer & 0b10000000u) == 0u;
}

byte DS3231::readControlByte(bool which) {
    byte reg = which ? 0x0Fu : 0x0Eu;
    byte value = 0u;
    if (!readBytes(reg, &value, 1u)) {
        return 0u;
    }
    return value;
}

void DS3231::writeControlByte(byte control, bool which) {
    const byte reg = which ? 0x0Fu : 0x0Eu;
    (void)writeBytes(reg, &control, 1u);
}

#endif /* HAL_ENABLE_RTC && HAL_ENABLE_DS3231 */
