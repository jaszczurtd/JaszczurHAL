# Example 35: cJSON

Parses a small JSON configuration string with bundled `cJSON`, validates the
fields, builds a status object, sorts it with `cJSON_Utils`, and prints the
result through the HAL debug logger.

Enabled module:

- `HAL_ENABLE_CJSON`

This is a memory-only utility example. It does not require external wiring.
