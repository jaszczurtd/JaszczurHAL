#include <hal/hal_app.h>
#include <hal/hal_system.h>
#include <hal/impl/shared/frameworks/cjson/cJSON.h>
#include <hal/impl/shared/frameworks/cjson/cJSON_Utils.h>
#include <tools_c.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  char name[32];
  uint32_t sample_ms;
  bool enabled;
} example_config_t;

static const char kConfigJson[] =
    "{\"name\":\"JaszczurHAL\",\"sample_ms\":250,\"enabled\":true}";

static bool parse_config(const char *json, example_config_t *out) {
  const char *parse_end = NULL;
  cJSON *root = cJSON_ParseWithOpts(json, &parse_end, 1);
  if (root == NULL) {
    derr("parse failed near: %.16s\r\n", parse_end != NULL ? parse_end : "");
    return false;
  }

  const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
  const cJSON *sample_ms = cJSON_GetObjectItemCaseSensitive(root, "sample_ms");
  const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");

  bool ok = false;
  if (cJSON_IsString(name) && name->valuestring != NULL &&
      cJSON_IsNumber(sample_ms) && cJSON_IsBool(enabled)) {
    snprintf(out->name, sizeof(out->name), "%s", name->valuestring);
    out->sample_ms = (uint32_t)cJSON_GetNumberValue(sample_ms);
    out->enabled = cJSON_IsTrue(enabled);

    deb("parsed name=%s sample_ms=%lu enabled=%u\r\n", out->name,
        (unsigned long)out->sample_ms, out->enabled ? 1u : 0u);
    ok = true;
  }

  cJSON_Delete(root);
  return ok;
}

static void print_status_json(const example_config_t *cfg) {
  cJSON *status = cJSON_CreateObject();
  if (status == NULL) {
    derr("status allocation failed\r\n");
    return;
  }

  cJSON_AddStringToObject(status, "module", "cJSON");
  cJSON_AddStringToObject(status, "name", cfg->name);
  cJSON_AddNumberToObject(status, "sample_ms", (double)cfg->sample_ms);
  cJSON_AddBoolToObject(status, "enabled", cfg->enabled);
  cJSON_AddNumberToObject(status, "uptime_ms", (double)hal_millis());

  cJSONUtils_SortObject(status);

  char buffer[160];
  if (cJSON_PrintPreallocated(status, buffer, (int)sizeof(buffer), false)) {
    deb("status %s\r\n", buffer);
  } else {
    derr("status buffer too small\r\n");
  }

  cJSON_Delete(status);
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("CJSON");

  example_config_t cfg = {};
  if (parse_config(kConfigJson, &cfg)) {
    print_status_json(&cfg);
  }
}

void app_task0(void) { hal_delay_ms(1000u); }
