#if defined(HAL_TARGET_RP2350_RISCV) && HAL_TARGET_RP2350_RISCV &&             \
    defined(HAL_ENABLE_FREERTOS)

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/riscv_platform_timer.h"
#include "pico/platform.h"

extern uint64_t ullNextTime;
extern size_t uxTimerIncrementsForOneTick;
extern volatile uint64_t *pullMachineTimerCompareRegister;

extern UBaseType_t callTaskEnterCriticalFromISR(void);
extern void callTaskExitCriticalFromISR(UBaseType_t saved_interrupt_status);

static void __time_critical_func(jh_rp2350_riscv_tick_handler)(void) {
  riscv_timer_set_mtimecmp(ullNextTime);
  ullNextTime += uxTimerIncrementsForOneTick;

  UBaseType_t saved_interrupt_status = callTaskEnterCriticalFromISR();
  BaseType_t switch_required = xTaskIncrementTick();
  callTaskExitCriticalFromISR(saved_interrupt_status);
  portYIELD_FROM_ISR(switch_required);
}

void vPortSetupTimerInterrupt(void) {
  riscv_timer_set_fullspeed(true);
  uxTimerIncrementsForOneTick =
      (size_t)(clock_get_hz(clk_sys) / configTICK_RATE_HZ);
  pullMachineTimerCompareRegister = (volatile uint64_t *)&sio_hw->mtimecmp;

  ullNextTime = riscv_timer_get_mtime() + uxTimerIncrementsForOneTick;
  riscv_timer_set_mtimecmp(ullNextTime);
  ullNextTime += uxTimerIncrementsForOneTick;

  irq_set_exclusive_handler(SIO_IRQ_MTIMECMP, jh_rp2350_riscv_tick_handler);
  irq_set_enabled(SIO_IRQ_MTIMECMP, true);
}

#endif
