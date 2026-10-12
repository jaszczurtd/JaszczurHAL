#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

#define HAL_ENABLE_EEPROM
#define HAL_ENABLE_LITTLEFS
#undef HAL_EEPROM_TYPE
#define HAL_EEPROM_TYPE EEPROM_TYPE_FLASH
