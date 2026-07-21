#include <hal/hal_app.h>
#include <tools_c.h>

static void log_architecture_snapshot(void) {
  hal_system_architecture_t arch = {0};
  hal_status_t status = hal_system_get_current_architecture(&arch);
  if (status != HAL_OK) {
    derr("architecture snapshot failed: %s", hal_status_to_string(status));
    return;
  }

  deb("target=%s backend=%s mcu=%s subtype=%s cpu=%s rtos=%s", arch.target_name,
      arch.backend_name, arch.mcu, arch.mcu_subtype, arch.cpu_arch,
      arch.rtos_name);
  deb("cores=%u hw=%u fpu=%u cpu_hz=%lu peri_hz=%lu", (unsigned)arch.cpu_cores,
      (unsigned)arch.is_hardware, (unsigned)arch.has_fpu,
      (unsigned long)arch.cpu_clock_hz,
      (unsigned long)arch.peripheral_clock_hz);
  deb("flash total=%lu usable=%lu reserved=%lu ram total=%lu usable=%lu",
      (unsigned long)arch.flash_total_bytes,
      (unsigned long)arch.flash_usable_bytes,
      (unsigned long)arch.flash_reserved_bytes,
      (unsigned long)arch.ram_total_bytes,
      (unsigned long)arch.ram_usable_bytes);
  deb("heap total=%lu free=%lu stack=%lu uid_bytes=%lu",
      (unsigned long)arch.heap_total_bytes, (unsigned long)arch.heap_free_bytes,
      (unsigned long)arch.stack_total_bytes, (unsigned long)arch.uid_bytes);
  deb("network backend=%s stack=%s type=%u", arch.network_backend_name,
      arch.network_stack_name, (unsigned)arch.network_stack_type);
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("EXAMPLE");
  deb("ready");
  log_architecture_snapshot();
}

void app_task0(void) {}
