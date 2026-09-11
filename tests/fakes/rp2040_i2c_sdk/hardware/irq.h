#pragma once
typedef void (*irq_handler_t)(void);
extern irq_handler_t jh_test_i2c_irqs[2];
inline void irq_set_exclusive_handler(unsigned int irq, irq_handler_t handler) {
  jh_test_i2c_irqs[irq] = handler;
}
inline void irq_remove_handler(unsigned int irq, irq_handler_t) {
  jh_test_i2c_irqs[irq] = nullptr;
}
inline void irq_set_enabled(unsigned int, bool) {}
