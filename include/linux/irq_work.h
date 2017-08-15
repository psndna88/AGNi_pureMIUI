#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <linux/llist.h>

/*
 * An entry can be in one of four states:
 *
 * free	     NULL, 0 -> {claimed}       : free to be used
 * claimed   NULL, 3 -> {pending}       : claimed to be enqueued
 * pending   next, 3 -> {busy}          : queued, pending callback
 * busy      NULL, 2 -> {free, claimed} : callback in progress, can be claimed
 */

#define IRQ_WORK_PENDING	BIT(0)
#define IRQ_WORK_BUSY		BIT(1)
#define IRQ_WORK_FLAGS		(IRQ_WORK_PENDING | IRQ_WORK_BUSY)
#define IRQ_WORK_LAZY		BIT(2) /* Doesn't want IPI, wait for tick */

struct irq_work {
	unsigned long flags;
	struct llist_node llnode;
	void (*func)(struct irq_work *);
};

static inline
void init_irq_work(struct irq_work *work, void (*func)(struct irq_work *))
{
	work->flags = 0;
	work->func = func;
}

void irq_work_queue(struct irq_work *work);

#ifdef CONFIG_SMP
bool irq_work_queue_on(struct irq_work *work, int cpu);
#endif

void irq_work_run(void);
void irq_work_tick(void);
void irq_work_sync(struct irq_work *work);

#ifdef CONFIG_IRQ_WORK
#include <asm/irq_work.h>

bool irq_work_needs_cpu(void);
#else
static inline bool irq_work_needs_cpu(void) { return false; }
#endif

#endif /* _LINUX_IRQ_WORK_H */
