#include "jh_cli_internal.h"
#include "jh_firmware_read.h"

static hal_status_t firmware_log(void *user, jb_process_stream_t stream,
                                 const unsigned char *data, size_t length) {
  (void)user;
  (void)stream;
  return fwrite(data, 1, length, stderr) == length ? HAL_OK : HAL_EIO;
}

int jh_cli_firmware(jh_cli_t *ctx) {
  if (!jh_cli_value(ctx, opt_project, "firmware read requires --project") ||
      !jh_cli_value(ctx, opt_assembly, "firmware read requires --assembly") ||
      !jh_cli_value(ctx, opt_output, "firmware read requires --output"))
    return 0;
  const jh_arguments_t *a = &ctx->args;
  jh_firmware_options_t options = {0};
  options.project = jh_cli_path(ctx, a->values[opt_project], ctx->base, true);
  options.assembly = a->values[opt_assembly];
  options.output = jh_cli_path(ctx, a->values[opt_output], ctx->base, false);
  if (a->present[opt_build_dir])
    options.build_directory =
        jh_cli_path(ctx, a->values[opt_build_dir], ctx->base, true);
  if (a->present[opt_tool])
    options.tool = jh_cli_path(ctx, a->values[opt_tool], ctx->base, true);
  options.variant =
      a->present[opt_variant_option] ? a->values[opt_variant_option] : NULL;
  options.serial = a->present[opt_serial] ? a->values[opt_serial] : NULL;
  options.port = a->present[opt_port] ? a->values[opt_port] : NULL;
  options.address = a->present[opt_address] ? a->values[opt_address] : NULL;
  options.size = a->present[opt_size] ? a->values[opt_size] : NULL;
  options.reset = a->present[opt_reset];
  options.timeout_ms = (uint32_t)jh_cli_count(
      ctx, opt_timeout_ms, 0, UINT32_MAX,
      "--timeout-ms requires an integer between 0 and 4294967295");
  options.automatic_timeout = !a->present[opt_timeout_ms];
  options.baud_rate = (uint32_t)jh_cli_count(
      ctx, opt_baud, 0, UINT32_MAX,
      "--baud requires an integer between 1 and 4294967295");
  if (a->present[opt_baud] && !options.baud_rate)
    (void)jh_fail_text(&ctx->failure, HAL_EINVAL, 2, "--baud must be positive");
  options.log = firmware_log;
  options.canceled = jh_cli_canceled;
  if (ctx->failure.status != HAL_OK || !jh_cli_install_cancellation(ctx))
    return 0;
  jh_firmware_result_t result;
  const hal_status_t status =
      jh_firmware_read(&options, &result, &ctx->location, &ctx->failure);
  ctx->process = result.process;
  ctx->process_attempted = result.attempted;
  ctx->device_state = result.device_state;
  ctx->firmware_published = result.published;
  if (!jh_cli_ok(ctx, status))
    return 0;
  if (ctx->json) {
    jh_result_begin(ctx, jh_cli_list_bytes(&ctx->strings) + 2048, 32);
    jh_result_text(ctx, "command", "firmware.read");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "project", options.project);
    jh_result_text(ctx, "assembly", options.assembly);
    jh_result_text(ctx, "variant", options.variant ? options.variant : "");
    jh_result_text(ctx, "target", result.target);
    if (options.serial)
      jh_result_text(ctx, "deviceSerial", options.serial);
    if (options.port)
      jh_result_text(ctx, "port", options.port);
    jh_result_text(ctx, "output", options.output);
    jh_result_number(ctx, "address", result.address);
    jh_result_number(ctx, "size", result.size);
    jh_result_number(ctx, "timeoutMs", result.timeout_ms);
    if (result.baud_rate)
      jh_result_number(ctx, "baudRate", result.baud_rate);
    jh_result_number(ctx, "flashAddress", result.flash_address);
    jh_result_number(ctx, "flashSize", result.flash_size);
    jh_result_boolean(ctx, "completeFlash", result.complete);
    jh_result_text(ctx, "format", "bin");
    jh_result_text(ctx, "restoreTool",
                   result.target[0] == 'r'   ? "picotool"
                   : result.target[0] == 's' ? "openocd"
                                             : "esptool");
    jh_result_number(ctx, "restoreAddress", result.address);
    jh_result_text(ctx, "sha256", result.sha256);
    jh_result_text(ctx, "deviceState", result.device_state);
    jh_result_boolean(ctx, "changed", result.published);
    jh_result_process(ctx);
    (void)jh_result_finish(ctx);
  } else {
    const char *message =
        jh_cli_text(ctx, JH_PARTS(jh_span(options.output), jh_span(": "),
                                  jh_span(jh_cli_size(ctx, result.size)),
                                  jh_span(" bytes, SHA-256 "),
                                  jh_span(result.sha256), jh_span(", device "),
                                  jh_span(result.device_state), jh_span("\n")));
    (void)jh_cli_stdout(ctx, jh_span(message));
  }
  return 0;
}
