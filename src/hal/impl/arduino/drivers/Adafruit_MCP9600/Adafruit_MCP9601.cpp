#include "../../../../hal_config.h"
#if defined(HAL_ENABLE_THERMOCOUPLE) && defined(HAL_ENABLE_MCP9600) && defined(HAL_ENABLE_I2C)

/**************************************************************************/
/*!
  @file     Adafruit_MCP9601.cpp


  This is a library for the Adafruit MCP9601 breakout board
  ----> https://www.adafruit.com/product/4101

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  K. Townsend (Adafruit Industries)
  BSD (see license.txt)
*/
/**************************************************************************/
#include "Adafruit_MCP9601.h"

/**************************************************************************/
/*!
    @brief  Instantiates a new MCP9601 class
*/
/**************************************************************************/
Adafruit_MCP9601::Adafruit_MCP9601() { _device_id = 0x41; }

#endif /* HAL_ENABLE_THERMOCOUPLE && !HAL_ENABLE_MCP9600 && !HAL_ENABLE_I2C */
