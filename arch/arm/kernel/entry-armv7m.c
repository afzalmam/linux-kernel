// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/entry-armv7m.c
 *
 */

#include <linux/linkage.h>
#include <linux/sched.h>
#include <asm/v7m.h>
#include <asm/ptrace.h>
#include <asm/io.h>

void nvic_handle_irq(irq_hw_number_t hwirq, struct pt_regs *regs);

asmlinkage void irq_entry(unsigned long unused, struct pt_regs *regs)
{
	unsigned long val;

	asm("mrs %0, ipsr" : "=r" (val));

	val &= V7M_xPSR_EXCEPTIONNO;
	val -= 16;

	nvic_handle_irq(val, regs);

	val = readl_relaxed(BASEADDR_V7M_SCB + V7M_SCB_ICSR);
	if (val & V7M_SCB_ICSR_RETTOBASE) {
		if (current_thread_info()->flags & _TIF_WORK_MASK) {
			writel_relaxed(V7M_SCB_ICSR_PENDSVSET,
					BASEADDR_V7M_SCB + V7M_SCB_ICSR);
		}
	}
}

asmlinkage void pendsv_entry(unsigned long unused, struct pt_regs *regs)
{
	writel_relaxed(V7M_SCB_ICSR_PENDSVCLR,
			BASEADDR_V7M_SCB + V7M_SCB_ICSR);
}
