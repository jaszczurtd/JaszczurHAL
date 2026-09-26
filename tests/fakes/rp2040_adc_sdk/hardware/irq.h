#pragma once

#include <stdbool.h>

#define DMA_IRQ_0 11u

typedef void (*irq_handler_t)(void);

bool irq_has_handler(unsigned int irq);
void irq_set_exclusive_handler(unsigned int irq, irq_handler_t handler);
void irq_remove_handler(unsigned int irq, irq_handler_t handler);
void irq_set_enabled(unsigned int irq, bool enabled);
