// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/entry-armv7m.c
 *
 */

#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/context_tracking.h>
#include <asm/v7m.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/unistd.h>

void nvic_handle_irq(irq_hw_number_t hwirq, struct pt_regs *regs);
void addr_limit_check_failed(void);
int do_work_pending(struct pt_regs *regs, unsigned int thread_flags,
		    int syscall);
int syscall_trace_enter(struct pt_regs *regs, int scno);

#ifndef CONFIG_CONTEXT_TRACKING
static inline void context_tracking_user_enter(void) {}
#endif

asmlinkage void _irq_entry(unsigned long unused, struct pt_regs *regs)
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

static inline void _reload_syscall_args(void) { }
static inline void _push_syscall_args_56(void) { }

static void _local_restart(struct pt_regs *reg)
{
	unsigned long scno = reg->ARM_r8;

	_push_syscall_args_56();
	if (current_thread_info()->flags & _TIF_SYSCALL_WORK) {
		scno = syscall_trace_enter(reg, scno);
	}
}

static void _ret_to_user_from_irq(struct pt_regs *reg, struct thread_info *tinfo,
				  unsigned long scno)
{
	int ret;

	if (current_thread_info()->addr_limit != TASK_SIZE)
		addr_limit_check_failed();
	if (current_thread_info()->flags & _TIF_WORK_MASK) {
		ret = do_work_pending(reg, tinfo->flags, scno);
		if (ret != 0) {
			if (ret < 0)
				scno = __NR_restart_syscall - __NR_SYSCALL_BASE;
			_reload_syscall_args();
			_local_restart(reg);
			return;
		}
	}
	trace_hardirqs_on();
	if (IS_ENABLED(CONFIG_CONTEXT_TRACKING))
		context_tracking_user_enter();
}

asmlinkage void _pendsv_entry(struct pt_regs *reg)
{
	writel_relaxed(V7M_SCB_ICSR_PENDSVCLR,
			BASEADDR_V7M_SCB + V7M_SCB_ICSR);
	_ret_to_user_from_irq(reg, current_thread_info(), 0);
}
