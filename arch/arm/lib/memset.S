/*
 *  linux/arch/arm/lib/memset.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ASM optimised string functions
 *
 *  Optimization for modern ARM platforms
 *  Copyright 2013 Harm Hanemaaijer
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.syntax unified
ARM(	.p2align 5	)
THUMB(	.p2align 2	)

ENTRY(memset)
	ands	r3, r0, #3
	mov	ip, r0
	bne	8f

/*
 * we know that the pointer in r0 is aligned to a word boundary.
 */
1:	orr	r1, r1, r1, lsl #8
	cmp	r2, #8
	orr	r1, r1, r1, lsl #16
	blt	5f
	mov	r3, r1

	cmp	r2, #64
	push 	{r4}
	blt	4f
#if MEMSET_WRITE_ALIGN_BYTES > 0
	ands	r4, r0, #(MEMSET_WRITE_ALIGN_BYTES - 1)
	/* Let r4 be equal to the number of bytes to align.  */
	rsb	r4, r4, #MEMSET_WRITE_ALIGN_BYTES
	/*
	 * At this point r4 contains the number of bytes to align
	 * if eq is not set. The eq flag is set if there are no bytes
	 * to align.
	 */
#if MEMSET_WRITE_ALIGN_BYTES == 8
	subne	r2, r2, r4
	strne	r1, [r0], #4
#elif MEMSET_WRITE_ALIGN_BYTES == 32
	beq	2f
	tst     r4, #4
	sub	r2, r2, r4
	strne	r1, [r0], #4
	tst     r4, #8
	stmiane r0!, {r1, r3}
	cmp	r4, #16
	stmiage r0!, {r1, r3}
	stmiage r0!, {r1, r3}
#endif
	cmp	r2, #64
	blt	4f
#endif

2:	mov	r4, r1
	push	{r5}
	mov	r5, r1

3:	stmia	r0!, {r1, r3, r4, r5}
	subs	r2, r2, #64		/* Thumb16 */
	stmia	r0!, {r1, r3, r4, r5}
	cmp	r2, #64
	stmia	r0!, {r1, r3, r4, r5}
	stmia	r0!, {r1, r3, r4, r5}
	bge	3b

	pop	{r5}
	/* Early exit if there are 0 bytes left. */
THUMB(	cbz	r2, 7f	)
ARM(	teq	r2, #0	)
ARM(	beq	7f	)

	/* Handle 8-64 bytes. */
4:	bic	r4, r2, #7
	subs	r2, r2, r4
	rsb	r4, r4, #64
	/* The stmia instruction is 32-bit for ARM, 16-bit for Thumb2. */
THUMB(	lsrs	r4, r4, #2	)
ARM(	lsrs	r4, r4, #1	)
	add	pc, pc, r4
	nop
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	pop	{r4}

5:	cmp	r2, #4
	strge	r1, [r0], #4
	/* Early exit for multiple of 4 size. */
	ands	r2, r2, #3
	moveq	r0, ip
	moveq	pc, lr

	/*
	 * At this point there are 1, 2 or 3 bytes,
	 * and the destination is aligned.
	 */
6:	cmp	r2, #2
	strhge	r1, [r0], #2
	strbne	r1, [r0]
	mov	r0, ip
	mov 	pc, lr

7:	pop	{r4}
	mov	r0, ip
	mov	pc, lr

8:	cmp	r2, #4
	blt	9f
	cmp	r3, #2
	sub	r2, r2, #4
	strble	r1, [r0]
	strble	r1, [r0, #1]
	addle	r0, r0, #2
	add	r2, r2, r3
	strbne	r1, [r0], #1
	b	1b

	/* 0 to 3 bytes left. */
9:	cmp	r2, #2
	strbge  r1, [r0]
	strbge  r1, [r0, #1]
	addge	r0, r0, #2
	tst	r2, #1
	strbne  r1, [r0]
	mov	r0, ip
	mov	pc, lr
ENDPROC(memset)
