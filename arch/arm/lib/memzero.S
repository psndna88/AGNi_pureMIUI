/*
 *  linux/arch/arm/lib/memzero.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Optimization for modern ARM platforms
 *  Copyright 2013 Harm Hanemaaijer
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.syntax unified
ARM(	.p2align 5	)
THUMB(	.p2align 2	)

ENTRY(__memzero)
	ands	r3, r0, #3
	mov	ip, r0
	mov	r2, #0
	bne	8f

/*
 * we know that the pointer in r0 is aligned to a word boundary.
 */
1:	cmp	r1, #8
	blt	5f
	mov	r3, r2

	cmp	r1, #64
	push 	{r4}
	blt	4f
#if MEMSET_WRITE_ALIGN_BYTES > 0
	ands	r4, r0, #(MEMSET_WRITE_ALIGN_BYTES - 1)
	/* Let r4 be equal to the number of bytes align.  */
	rsb	r4, r4, #MEMSET_WRITE_ALIGN_BYTES
	/*
	 * At this point r4 contains the number of bytes to align
	 * if eq is not set. The eq flag is set if there are no bytes
	 * to align.
	 */
#if MEMSET_WRITE_ALIGN_BYTES == 8
	subne	r1, r1, r4
	strne	r2, [r0], #4
#elif MEMSET_WRITE_ALIGN_BYTES == 32
	beq	2f
	tst     r4, #4
	sub	r1, r1, r4
	strne	r2, [r0], #4
	tst     r4, #8
	stmiane r0!, {r2, r3}
	cmp	r4, #16
	stmiage r0!, {r2, r3}
        stmiage r0!, {r2, r3}
#endif
	cmp	r1, #64
	blt	4f
#endif

2:	mov	r4, r2
	push	{r5}
	mov	r5, r2

3:	stmia	r0!, {r2, r3, r4, r5}
	subs	r1, r1, #64		/* Thumb16 */
	stmia	r0!, {r2, r3, r4, r5}
	cmp	r1, #64
	stmia	r0!, {r2, r3, r4, r5}
	stmia	r0!, {r2, r3, r4, r5}
	bge	3b

	pop	{r5}
	/* Early exit if there are 0 bytes left. */
THUMB(	cbz	r1, 7f	)
ARM(	teq	r1, #0	)
ARM(	beq	7f	)

	/* Handle 8-64 bytes. */
4:	bic	r4, r1, #7
	subs	r1, r1, r4
	rsb	r4, r4, #64
	/* The stmia instruction is 32-bit for ARM, 16-bit for Thumb2. */
THUMB(	lsrs	r4, r4, #2	)
ARM(	lsrs	r4, r4, #1	)
	add	pc, pc, r4
	nop
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	stmia	r0!, {r2, r3}
	pop	{r4}

5:	cmp	r1, #4
	strge	r2, [r0], #4
	/* Early exit for multiple of 4 size. */
	ands	r1, r1, #3
	moveq	r0, ip
	moveq	pc, lr

	/*
	 * At this point there are 1, 2 or 3 bytes,
	 * and the destination is word aligned.
	 */
6:	cmp	r1, #2
	strhge	r2, [r0], #2
	strbne	r2, [r0]
	mov	r0, ip
	mov	pc, lr

7:	pop	{r4}
	mov	r0, ip
	mov	pc, lr

8:	cmp	r1, #4
	blt	9f
	cmp	r3, #2
	sub	r1, r1, #4
	strble	r2, [r0]
	strble	r2, [r0, #1]
	addle	r0, r0, #2
	add	r1, r1, r3
	strbne	r2, [r0], #1
	b	1b

	/* 0 to 3 bytes left. */
9:	cmp	r1, #2
	strbge	r2, [r0]
	strbge	r2, [r0, #1]
	addge	r0, r0, #2
	tst	r1, #1
	strbne	r2, [r0]
	mov	pc, lr
ENDPROC(__memzero)
