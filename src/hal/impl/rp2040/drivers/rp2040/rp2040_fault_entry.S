#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"

#define JH_RP_FAULT_EMERGENCY_STACK_SIZE 512

#if defined(__riscv)

// The mtvec table itself runs from SRAM and reaches this handler with a JAL,
// whose range cannot span from SRAM to XIP flash.
.section .time_critical.jh_rp_fault_entry, "ax", @progbits
.p2align 2

.global jh_rp_fault_handlers_link_anchor
.type jh_rp_fault_handlers_link_anchor, @function
jh_rp_fault_handlers_link_anchor:
    ret
.size jh_rp_fault_handlers_link_anchor, . - jh_rp_fault_handlers_link_anchor

.global jh_stack_overflow_reset
.type jh_stack_overflow_reset, @function
jh_stack_overflow_reset:
#if defined(__riscv)
    csrci mstatus, 8
#else
    cpsid i
#endif
    mv a0, ra
    li a1, 0
    j jh_stack_overflow_reset_with_context
.size jh_stack_overflow_reset, . - jh_stack_overflow_reset

.global jh_stack_overflow_reset_with_context
.type jh_stack_overflow_reset_with_context, @function
jh_stack_overflow_reset_with_context:
    csrci mstatus, 8
    csrr t0, mhartid
    bnez t0, 1f
    la sp, jh_rp_fault_emergency_stack0 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
    j 2f
1:
    la sp, jh_rp_fault_emergency_stack1 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
2:
    la t0, jh_rp_stack_overflow_reset_c
    jr t0
.size jh_stack_overflow_reset_with_context, . - jh_stack_overflow_reset_with_context

// Hazard3 does not switch stacks or save GPRs on exception entry. Preserve the
// complete foreground register file on a per-core emergency stack so the C
// decoder can derive a load/store effective address despite MTVAL being zero.
.global isr_riscv_machine_exception
.type isr_riscv_machine_exception, @function
isr_riscv_machine_exception:
    csrw mscratch, t0
    csrr t0, mhartid
    bnez t0, 1f
    la t0, jh_rp_fault_emergency_stack0 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
    j 2f
1:
    la t0, jh_rp_fault_emergency_stack1 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
2:
    csrrw t0, mscratch, t0
    csrrw sp, mscratch, sp
    addi sp, sp, -128
    sw zero,   0(sp)
    sw ra,     4(sp)
    // 8(sp), the original stack pointer, is filled after saving t0.
    sw gp,    12(sp)
    sw tp,    16(sp)
    sw t0,    20(sp)
    sw t1,    24(sp)
    sw t2,    28(sp)
    sw s0,    32(sp)
    sw s1,    36(sp)
    sw a0,    40(sp)
    sw a1,    44(sp)
    sw a2,    48(sp)
    sw a3,    52(sp)
    sw a4,    56(sp)
    sw a5,    60(sp)
    sw a6,    64(sp)
    sw a7,    68(sp)
    sw s2,    72(sp)
    sw s3,    76(sp)
    sw s4,    80(sp)
    sw s5,    84(sp)
    sw s6,    88(sp)
    sw s7,    92(sp)
    sw s8,    96(sp)
    sw s9,   100(sp)
    sw s10,  104(sp)
    sw s11,  108(sp)
    sw t3,   112(sp)
    sw t4,   116(sp)
    sw t5,   120(sp)
    sw t6,   124(sp)
    csrr t0, mscratch
    sw t0,     8(sp)

    mv a0, sp
    csrr a1, mcause
    csrr a2, mepc
    la t0, jh_rp_riscv_fault_capture
    jr t0
.size isr_riscv_machine_exception, . - isr_riscv_machine_exception

#else

.syntax unified
.thumb
.section .text.jh_rp_fault_entry, "ax", %progbits
.p2align 2

.global jh_rp_fault_handlers_link_anchor
.type jh_rp_fault_handlers_link_anchor, %function
.thumb_func
jh_rp_fault_handlers_link_anchor:
    bx lr
.size jh_rp_fault_handlers_link_anchor, . - jh_rp_fault_handlers_link_anchor

.global jh_stack_overflow_reset
.type jh_stack_overflow_reset, %function
.thumb_func
jh_stack_overflow_reset:
    cpsid i
    mov r0, lr
    movs r1, #0
    b jh_stack_overflow_reset_with_context
.size jh_stack_overflow_reset, . - jh_stack_overflow_reset

.global jh_stack_overflow_reset_with_context
.type jh_stack_overflow_reset_with_context, %function
.thumb_func
jh_stack_overflow_reset_with_context:
    cpsid i
#if defined(PICO_RP2350)
    movs r2, #0
    msr msplim, r2
    dsb
    isb
#endif
    ldr r2, =(SIO_BASE + SIO_CPUID_OFFSET)
    ldr r2, [r2]
    cmp r2, #0
    bne 1f
    ldr r2, =jh_rp_fault_emergency_stack0 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
    b 2f
1:
    ldr r2, =jh_rp_fault_emergency_stack1 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
2:
    mov sp, r2
    ldr r2, =jh_rp_stack_overflow_reset_c
    bx r2
.size jh_stack_overflow_reset_with_context, . - jh_stack_overflow_reset_with_context

.global isr_hardfault
.type isr_hardfault, %function
.thumb_func
isr_hardfault:
    cpsid i
    mov r3, lr
    movs r1, #4
    tst r1, r3
    beq 1f
    mrs r0, psp
    b 2f
1:
    mrs r0, msp
2:
#if defined(PICO_RP2350)
    // The emergency stack is below __StackBottom, so disable the exhausted
    // main-stack limit before changing MSP on this fatal, non-returning path.
    movs r1, #0
    msr msplim, r1
    dsb
    isb
#endif
    ldr r1, =(SIO_BASE + SIO_CPUID_OFFSET)
    ldr r1, [r1]
    cmp r1, #0
    bne 3f
    ldr r1, =jh_rp_fault_emergency_stack0 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
    b 4f
3:
    ldr r1, =jh_rp_fault_emergency_stack1 + JH_RP_FAULT_EMERGENCY_STACK_SIZE
4:
    mov sp, r1
    mov r1, r3
    ldr r2, =jh_rp_arm_fault_capture
    bx r2
.size isr_hardfault, . - isr_hardfault

#if defined(PICO_RP2350)
.global isr_usagefault
.type isr_usagefault, %function
.thumb_func
isr_usagefault:
    b isr_hardfault
.size isr_usagefault, . - isr_usagefault
#endif

.ltorg

#endif
