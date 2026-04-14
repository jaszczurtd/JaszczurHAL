#ifndef JASZCZURHAL_TOOLS_SENSOR_CONFIG_H
#define JASZCZURHAL_TOOLS_SENSOR_CONFIG_H

/**
 * @file tools_sensor_config.h
 * @brief Sensor/math utility defaults and compatibility aliases.
 */

/** @brief Default ADC resolution in bits used by helper conversions. */
#ifndef HAL_TOOLS_ADC_BITS
#define HAL_TOOLS_ADC_BITS 12
#endif

/** @brief Default ADC maximum value based on resolution. */
#ifndef HAL_TOOLS_ADC_MAXVALUE
#define HAL_TOOLS_ADC_MAXVALUE ((1 << HAL_TOOLS_ADC_BITS) - 1)
#endif

/** @brief Default averaging window size for ADC smoothing helpers. */
#ifndef HAL_TOOLS_NUMSAMPLES
#define HAL_TOOLS_NUMSAMPLES 4
#endif

/** @brief Default rolling-table size for temperature averaging helpers. */
#ifndef HAL_TOOLS_TEMPERATURE_TABLES_SIZE
#define HAL_TOOLS_TEMPERATURE_TABLES_SIZE 5
#endif

/** @brief Default NTC beta coefficient for thermistor calculations. */
#ifndef HAL_TOOLS_BCOEFFICIENT
#define HAL_TOOLS_BCOEFFICIENT 3600
#endif

/** @brief Default nominal temperature (deg C) for thermistor model. */
#ifndef HAL_TOOLS_TEMPERATURENOMINAL
#define HAL_TOOLS_TEMPERATURENOMINAL 21
#endif

/* Backward-compatible aliases used by legacy code. */
/** @brief Legacy alias for @ref HAL_TOOLS_ADC_BITS. */
#ifndef ADC_BITS
#define ADC_BITS HAL_TOOLS_ADC_BITS
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_NUMSAMPLES. */
#ifndef NUMSAMPLES
#define NUMSAMPLES HAL_TOOLS_NUMSAMPLES
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_TEMPERATURE_TABLES_SIZE. */
#ifndef TEMPERATURE_TABLES_SIZE
#define TEMPERATURE_TABLES_SIZE HAL_TOOLS_TEMPERATURE_TABLES_SIZE
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_BCOEFFICIENT. */
#ifndef BCOEFFICIENT
#define BCOEFFICIENT HAL_TOOLS_BCOEFFICIENT
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_TEMPERATURENOMINAL. */
#ifndef TEMPERATURENOMINAL
#define TEMPERATURENOMINAL HAL_TOOLS_TEMPERATURENOMINAL
#endif

#endif
