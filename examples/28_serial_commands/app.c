#include <hal/commands/hal_command_router.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_commands.h>
#include <hal/serial/hal_serial_session.h>
#include <hal/system/hal_system.h>
#include <stdio.h>

static hal_command_router_t s_router = NULL;
static hal_serial_session_t s_session;
static hal_serial_commands_t s_serialCommands;
static bool s_adapterReady = false;
static bool s_ready = false;

static const hal_serial_session_vocabulary_t s_vocabulary = {
    .cmd_bye = "BYE",
    .reply_bye_ok = "OK BYE",
    .reply_unknown_cmd = "ERR UNKNOWN",
    .reply_not_ready_hello_required = "ERR HELLO_REQUIRED",
};

static hal_status_t echoCommand(const hal_command_request_t *request,
                                hal_command_response_t *response, void *user) {
  (void)user;
  if (request == NULL || response == NULL) {
    return HAL_EINVAL;
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, HAL_COMMAND_ENCODING_TEXT);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

static hal_status_t infoCommand(const hal_command_request_t *request,
                                hal_command_response_t *response, void *user) {
  (void)user;
  if (request == NULL || response == NULL) {
    return HAL_EINVAL;
  }

  char info[128] = {0};
  const int length = snprintf(
      info, sizeof(info), "source=%s request=%lu session=%llu uptime_ms=%lu",
      hal_command_source_to_string(request->source),
      (unsigned long)request->request_id,
      (unsigned long long)request->session_id, (unsigned long)hal_millis());
  if (length <= 0 || (size_t)length >= sizeof(info)) {
    return HAL_EOVERFLOW;
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, HAL_COMMAND_ENCODING_TEXT);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, info, (size_t)length);
  }
  return status;
}

static hal_status_t registerCommand(const char *name,
                                    hal_command_handler_t handler) {
  const hal_command_definition_t definition = {
      .name = name,
      .allowed_sources =
          HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION),
      .required_security = 0u,
      .handler = handler,
      .user = NULL,
  };
  return hal_command_router_register_unique(s_router, &definition);
}

static void releaseExample(void) {
  if (s_adapterReady) {
    (void)hal_serial_commands_deinit(&s_serialCommands);
    s_adapterReady = false;
  }
  if (s_router != NULL) {
    (void)hal_command_router_destroy(s_router);
    s_router = NULL;
  }
  s_ready = false;
}

static void failStart(const char *stage, hal_status_t status) {
  derr("Serial command example failed: stage=%s status=%s", stage,
       hal_status_to_string(status));
  releaseExample();
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL Serial command router ===");

  hal_status_t status = hal_command_router_create(&s_router);
  if (status != HAL_OK) {
    failStart("router", status);
    return;
  }

  status = registerCommand("echo", echoCommand);
  if (status != HAL_OK) {
    failStart("echo", status);
    return;
  }
  status = registerCommand("info", infoCommand);
  if (status != HAL_OK) {
    failStart("info", status);
    return;
  }

  hal_serial_session_init_with_vocabulary(
      &s_session, "ROUTER", "1.0.0", "serial-commands-example", &s_vocabulary);
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  status = hal_serial_commands_init(&s_serialCommands, &config);
  if (status != HAL_OK) {
    failStart("adapter", status);
    return;
  }

  s_adapterReady = true;
  s_ready = true;
  deb("Send HELLO, then echo or info in SC frames");
}

void app_task0(void) {
  if (s_ready) {
    hal_serial_session_poll(&s_session);
  }
  hal_delay_ms(1u);
}
