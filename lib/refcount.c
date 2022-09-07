// SPDX-License-Identifier: GPL-2.0
/*
 * Out-of-line refcount functions.
 */

#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/bug.h>

#define REFCOUNT_WARN(str)	WARN_ONCE(1, "refcount_t: " str ".\n")

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t)
{
	refcount_set(r, REFCOUNT_SATURATED);

	switch (t) {
	case REFCOUNT_ADD_NOT_ZERO_OVF:
		REFCOUNT_WARN("saturated; leaking memory");
		break;
	case REFCOUNT_ADD_OVF:
		REFCOUNT_WARN("saturated; leaking memory");
		break;
	case REFCOUNT_ADD_UAF:
		REFCOUNT_WARN("addition on 0; use-after-free");
		break;
	case REFCOUNT_SUB_UAF:
		REFCOUNT_WARN("underflow; use-after-free");
		break;
	case REFCOUNT_DEC_LEAK:
		REFCOUNT_WARN("decrement hit 0; leaking memory");
		break;
	default:
		REFCOUNT_WARN("unknown saturation event!?");
	}
}
EXPORT_SYMBOL(refcount_warn_saturate);

/**
 * refcount_dec_if_one - decrement a refcount if it is 1
 * @r: the refcount
 *
 * No atomic_t counterpart, it attempts a 1 -> 0 transition and returns the
 * success thereof.
 *
 * Like all decrement operations, it provides release memory order and provides
 * a control dependency.
 *
 * It can be used like a try-delete operator; this explicit case is provided
 * and not cmpxchg in generic, because that would allow implementing unsafe
 * operations.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
bool refcount_dec_if_one(refcount_t *r)
{
	int val = 1;

	return atomic_try_cmpxchg_release(&r->refs, &val, 0);
}
EXPORT_SYMBOL(refcount_dec_if_one);

/**
 * refcount_dec_not_one - decrement a refcount if it is not 1
 * @r: the refcount
 *
 * No atomic_t counterpart, it decrements unless the value is 1, in which case
 * it will return false.
 *
 * Was often done like: atomic_add_unless(&var, -1, 1)
 *
 * Return: true if the decrement operation was successful, false otherwise
 */
bool refcount_dec_not_one(refcount_t *r)
{
	unsigned int new, val = atomic_read(&r->refs);

	do {
		if (unlikely(val == REFCOUNT_SATURATED))
			return true;

		if (val == 1)
			return false;

		new = val - 1;
		if (new > val) {
			WARN_ONCE(new > val, "refcount_t: underflow; use-after-free.\n");
			return true;
		}

	} while (!atomic_try_cmpxchg_release(&r->refs, &val, new));

	return true;
}
EXPORT_SYMBOL(refcount_dec_not_one);

/**
 * refcount_dec_and_mutex_lock - return holding mutex if able to decrement
 *                               refcount to 0
 * @r: the refcount
 * @lock: the mutex to be locked
 *
 * Similar to atomic_dec_and_mutex_lock(), it will WARN on underflow and fail
 * to decrement when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides a control dependency such that free() must come after.
 * See the comment on top.
 *
 * Return: true and hold mutex if able to decrement refcount to 0, false
 *         otherwise
 */
bool refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock)
{
	if (refcount_dec_not_one(r))
		return false;

	mutex_lock(lock);
	if (!refcount_dec_and_test(r)) {
		mutex_unlock(lock);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(refcount_dec_and_mutex_lock);

/**
 * refcount_dec_and_lock - return holding spinlock if able to decrement
 *                         refcount to 0
 * @r: the refcount
 * @lock: the spinlock to be locked
 *
 * Similar to atomic_dec_and_lock(), it will WARN on underflow and fail to
 * decrement when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides a control dependency such that free() must come after.
 * See the comment on top.
 *
 * Return: true and hold spinlock if able to decrement refcount to 0, false
 *         otherwise
 */
bool refcount_dec_and_lock(refcount_t *r, spinlock_t *lock)
{
	if (refcount_dec_not_one(r))
		return false;

	spin_lock(lock);
	if (!refcount_dec_and_test(r)) {
		spin_unlock(lock);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(refcount_dec_and_lock);

/**
 * refcount_dec_and_lock_irqsave - return holding spinlock with disabled
 *                                 interrupts if able to decrement refcount to 0
 * @r: the refcount
 * @lock: the spinlock to be locked
 * @flags: saved IRQ-flags if the is acquired
 *
 * Same as refcount_dec_and_lock() above except that the spinlock is acquired
 * with disabled interupts.
 *
 * Return: true and hold spinlock if able to decrement refcount to 0, false
 *         otherwise
 */
bool refcount_dec_and_lock_irqsave(refcount_t *r, spinlock_t *lock,
				   unsigned long *flags)
{
	if (refcount_dec_not_one(r))
		return false;

	spin_lock_irqsave(lock, *flags);
	if (!refcount_dec_and_test(r)) {
		spin_unlock_irqrestore(lock, *flags);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(refcount_dec_and_lock_irqsave);

/**************************************************************************/
/*
 * Android backport use only, due to these functions going away in 5.4.208
 * Do not use these in new code, this is only for ABI preservation.
 */
void refcount_inc_checked(refcount_t *r)
{
	refcount_inc(r);
}
EXPORT_SYMBOL(refcount_inc_checked);

bool refcount_inc_not_zero_checked(refcount_t *r)
{
	return refcount_inc_not_zero(r);
}
EXPORT_SYMBOL(refcount_inc_not_zero_checked);

void refcount_dec_checked(refcount_t *r)
{
	refcount_dec(r);
}
EXPORT_SYMBOL(refcount_dec_checked);

bool refcount_dec_and_test_checked(refcount_t *r)
{
	return refcount_sub_and_test(1, r);
}
EXPORT_SYMBOL(refcount_dec_and_test_checked);

/**
 * refcount_add_not_zero_checked - add a value to a refcount unless it is 0
 * @i: the value to add to the refcount
 * @r: the refcount
 *
 * Will saturate at UINT_MAX and WARN.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See the comment on top.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_inc(), or one of its variants, should instead be used to
 * increment a reference count.
 *
 * Return: false if the passed refcount is 0, true otherwise
 */
bool refcount_add_not_zero_checked(unsigned int i, refcount_t *r)
{
	unsigned int new, val = atomic_read(&r->refs);

	do {
		if (!val)
			return false;

		if (unlikely(val == UINT_MAX))
			return true;

		new = val + i;
		if (new < val)
			new = UINT_MAX;

	} while (!atomic_try_cmpxchg_relaxed(&r->refs, &val, new));

	WARN_ONCE(new == UINT_MAX, "refcount_t: saturated; leaking memory.\n");

	return true;
}
EXPORT_SYMBOL(refcount_add_not_zero_checked);

/**
 * refcount_add_checked - add a value to a refcount
 * @i: the value to add to the refcount
 * @r: the refcount
 *
 * Similar to atomic_add(), but will saturate at UINT_MAX and WARN.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See the comment on top.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_inc(), or one of its variants, should instead be used to
 * increment a reference count.
 */
void refcount_add_checked(unsigned int i, refcount_t *r)
{
	WARN_ONCE(!refcount_add_not_zero_checked(i, r), "refcount_t: addition on 0; use-after-free.\n");
}
EXPORT_SYMBOL(refcount_add_checked);

/**
 * refcount_sub_and_test_checked - subtract from a refcount and test if it is 0
 * @i: amount to subtract from the refcount
 * @r: the refcount
 *
 * Similar to atomic_dec_and_test(), but it will WARN, return false and
 * ultimately leak on underflow and will fail to decrement when saturated
 * at UINT_MAX.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides an acquire ordering on success such that free()
 * must come after.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_dec(), or one of its variants, should instead be used to
 * decrement a reference count.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
bool refcount_sub_and_test_checked(unsigned int i, refcount_t *r)
{
	unsigned int new, val = atomic_read(&r->refs);

	do {
		if (unlikely(val == UINT_MAX))
			return false;

		new = val - i;
		if (new > val) {
			WARN_ONCE(new > val, "refcount_t: underflow; use-after-free.\n");
			return false;
		}

	} while (!atomic_try_cmpxchg_release(&r->refs, &val, new));

	if (!new) {
		smp_acquire__after_ctrl_dep();
		return true;
	}
	return false;

}
EXPORT_SYMBOL(refcount_sub_and_test_checked);
