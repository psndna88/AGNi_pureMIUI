/*
 * Copyright (c) 2014-2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: i_qdf_trace.h
 *
 * Linux-specific definitions for QDF trace
 *
 */

#if !defined(__I_QDF_TRACE_H)
#define __I_QDF_TRACE_H

/* older kernels have a bug in kallsyms, so ensure module.h is included */
#include <linux/module.h>
#include <linux/kallsyms.h>

#if !defined(__printf)
#define __printf(a, b)
#endif

#ifdef CONFIG_MCL
/* QDF_TRACE is the macro invoked to add trace messages to code.  See the
 * documenation for qdf_trace_msg() for the parameters etc. for this function.
 *
 * NOTE:  Code QDF_TRACE() macros into the source code.  Do not code directly
 * to the qdf_trace_msg() function.
 *
 * NOTE 2:  qdf tracing is totally turned off if WLAN_DEBUG is *not* defined.
 * This allows us to build 'performance' builds where we can measure performance
 * without being bogged down by all the tracing in the code
 */
#define QDF_MAX_LOGS_PER_SEC 2
/**
 * __QDF_TRACE_RATE_LIMITED() - rate limited version of QDF_TRACE
 * @params: parameters to pass through to QDF_TRACE
 *
 * This API prevents logging a message more than QDF_MAX_LOGS_PER_SEC times per
 * second. This means any subsequent calls to this API from the same location
 * within 1/QDF_MAX_LOGS_PER_SEC seconds will be dropped.
 *
 * Return: None
 */
#define QDF_TRACE(arg ...)
#define QDF_VTRACE(arg ...)
#define QDF_TRACE_HEX_DUMP(arg ...)
#define __QDF_TRACE_RATE_LIMITED(arg ...)
#else /* CONFIG_MCL */
#define qdf_trace(log_level, args...)
#endif /* CONFIG_MCL */

#define __QDF_TRACE_NO_FL(log_level, module_id, format, args...)

#define __QDF_TRACE_FL(log_level, module_id, format, args...)

#define __QDF_TRACE_RL(log_level, module_id, format, args...)

#define __QDF_TRACE_RL_NO_FL(log_level, module_id, format, args...)

static inline void __qdf_trace_noop(QDF_MODULE_ID module, char *format, ...) { }

#define QDF_TRACE_FATAL(params...)
#define QDF_TRACE_FATAL_NO_FL(params...)
#define QDF_TRACE_FATAL_RL(params...)
#define QDF_TRACE_FATAL_RL_NO_FL(params...)

#define QDF_TRACE_ERROR(params...)
#define QDF_TRACE_ERROR_NO_FL(params...)
#define QDF_TRACE_ERROR_RL(params...)
#define QDF_TRACE_ERROR_RL_NO_FL(params...)

#define QDF_TRACE_WARN(params...)
#define QDF_TRACE_WARN_NO_FL(params...)
#define QDF_TRACE_WARN_RL(params...)
#define QDF_TRACE_WARN_RL_NO_FL(params...)

#define QDF_TRACE_INFO(params...)
#define QDF_TRACE_INFO_NO_FL(params...)
#define QDF_TRACE_INFO_RL(params...)
#define QDF_TRACE_INFO_RL_NO_FL(params...)

#define QDF_TRACE_DEBUG(params...)
#define QDF_TRACE_DEBUG_NO_FL(params...)
#define QDF_TRACE_DEBUG_RL(params...)
#define QDF_TRACE_DEBUG_RL_NO_FL(params...)

#define QDF_ENABLE_TRACING
#define qdf_scnprintf scnprintf

#define QDF_ASSERT(_condition)

#ifdef PANIC_ON_BUG
#ifdef CONFIG_SLUB_DEBUG
/**
 * __qdf_bug() - Calls BUG() when the PANIC_ON_BUG compilation option is enabled
 *
 * Note: Calling BUG() can cause a compiler to assume any following code is
 * unreachable. Because these BUG's may or may not be enabled by the build
 * configuration, this can cause developers some pain. Consider:
 *
 *	bool bit;
 *
 *	if (ptr)
 *		bit = ptr->returns_bool();
 *	else
 *		__qdf_bug();
 *
 *	// do stuff with @bit
 *
 *	return bit;
 *
 * In this case, @bit is potentially uninitialized when we return! However, the
 * compiler can correctly assume this case is impossible when PANIC_ON_BUG is
 * enabled. Because developers typically enable this feature, the "maybe
 * uninitialized" warning will not be emitted, and the bug remains uncaught
 * until someone tries to make a build without PANIC_ON_BUG.
 *
 * A simple workaround for this, is to put the definition of __qdf_bug in
 * another compilation unit, which prevents the compiler from assuming
 * subsequent code is unreachable. For CONFIG_SLUB_DEBUG, do this to catch more
 * bugs. Otherwise, use the typical inlined approach.
 *
 * Return: None
 */
void __qdf_bug(void);
#else /* CONFIG_SLUB_DEBUG */
static inline void __qdf_bug(void)
{
	BUG();
}
#endif /* CONFIG_SLUB_DEBUG */

/**
 * QDF_DEBUG_PANIC() - In debug builds, panic, otherwise do nothing
 * @reason: An optional reason format string, followed by args
 *
 * Return: None
 */
#define QDF_DEBUG_PANIC(reason...) \
	QDF_DEBUG_PANIC_FL(__func__, __LINE__, "" reason)

/**
 * QDF_DEBUG_PANIC_FL() - In debug builds, panic, otherwise do nothing
 * @func: origin function name to be logged
 * @line: origin line number to be logged
 * @fmt: printf compatible format string to be logged
 * @args: zero or more printf compatible logging arguments
 *
 * Return: None
 */
#define QDF_DEBUG_PANIC_FL(func, line, fmt, args...) \
	do { \
		pr_err("WLAN Panic @ %s:%d: " fmt "\n", func, line, ##args); \
		__qdf_bug(); \
	} while (false)

#define QDF_BUG(_condition) \
	do { \
		if (!(_condition)) { \
			pr_err("QDF BUG in %s Line %d: Failed assertion '" \
			       #_condition "'\n", __func__, __LINE__); \
			__qdf_bug(); \
		} \
	} while (0)

#else /* PANIC_ON_BUG */

#define QDF_DEBUG_PANIC(reason...) \
	do { \
		/* no-op */ \
	} while (false)

#define QDF_DEBUG_PANIC_FL(func, line, fmt, args...) \
	do { \
		/* no-op */ \
	} while (false)

#define QDF_BUG(_condition) \
	do { \
		if (!(_condition)) { \
			/* no-op */ \
		} \
	} while (0)

#endif /* PANIC_ON_BUG */

#ifdef KSYM_SYMBOL_LEN
#define __QDF_SYMBOL_LEN KSYM_SYMBOL_LEN
#else
#define __QDF_SYMBOL_LEN 1
#endif

#endif /* __I_QDF_TRACE_H */
