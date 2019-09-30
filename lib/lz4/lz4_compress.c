/*
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011 - 2016, Yann Collet.
 * BSD 2 - Clause License (http://www.opensource.org/licenses/bsd - license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 */

/*-************************************
 *	Dependencies
 **************************************/
#include <linux/lz4.h>
#include "lz4defs.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>

static const int LZ4_minLength = (MFLIMIT + 1);
static const int LZ4_64Klimit = ((64 * KB) + (MFLIMIT - 1));

/*-******************************
 *	Compression functions
 ********************************/
static FORCE_INLINE U32 LZ4_hash4(
	U32 sequence,
	tableType_t const tableType)
{
	if (tableType == byU16)
		return ((sequence * 2654435761U)
			>> ((MINMATCH * 8) - (LZ4_HASHLOG + 1)));
	else
		return ((sequence * 2654435761U)
			>> ((MINMATCH * 8) - LZ4_HASHLOG));
}

static FORCE_INLINE U32 LZ4_hash5(
	U64 sequence,
	tableType_t const tableType)
{
	const U32 hashLog = (tableType == byU16)
		? LZ4_HASHLOG + 1
		: LZ4_HASHLOG;

#if LZ4_LITTLE_ENDIAN
	static const U64 prime5bytes = 889523592379ULL;

	return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
#else
	static const U64 prime8bytes = 11400714785074694791ULL;

	return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
#endif
}

static FORCE_INLINE U32 LZ4_hashPosition(
	const void *p,
	tableType_t const tableType)
{
#if LZ4_ARCH64
	if (tableType == byU32)
		return LZ4_hash5(LZ4_read_ARCH(p), tableType);
#endif

	return LZ4_hash4(LZ4_read32(p), tableType);
}

static void LZ4_putPositionOnHash(
	const BYTE *p,
	U32 h,
	void *tableBase,
	tableType_t const tableType,
	const BYTE *srcBase)
{
	switch (tableType) {
	case byPtr:
	{
		const BYTE **hashTable = (const BYTE **)tableBase;

		hashTable[h] = p;
		return;
	}
	case byU32:
	{
		U32 *hashTable = (U32 *) tableBase;

		hashTable[h] = (U32)(p - srcBase);
		return;
	}
	case byU16:
	{
		U16 *hashTable = (U16 *) tableBase;

		hashTable[h] = (U16)(p - srcBase);
		return;
	}
	}
}

static FORCE_INLINE void LZ4_putPosition(
	const BYTE *p,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	LZ4_putPositionOnHash(p, h, tableBase, tableType, srcBase);
}

static const BYTE *LZ4_getPositionOnHash(
	U32 h,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	if (tableType == byPtr) {
		const BYTE **hashTable = (const BYTE **) tableBase;

		return hashTable[h];
	}

	if (tableType == byU32) {
		const U32 * const hashTable = (U32 *) tableBase;

		return hashTable[h] + srcBase;
	}

	{
		/* default, to ensure a return */
		const U16 * const hashTable = (U16 *) tableBase;

		return hashTable[h] + srcBase;
	}
}

static FORCE_INLINE const BYTE *LZ4_getPosition(
	const BYTE *p,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	return LZ4_getPositionOnHash(h, tableBase, tableType, srcBase);
}


/*
 * LZ4_compress_generic() :
 * inlined, to ensure branches are decided at compilation time
 */
static FORCE_INLINE int LZ4_compress_generic(
	LZ4_stream_t_internal * const dictPtr,
	const char * const source,
	char * const dest,
	const int inputSize,
	const int maxOutputSize,
	const limitedOutput_directive outputLimited,
	const tableType_t tableType,
	const dict_directive dict,
	const dictIssue_directive dictIssue,
	const U32 acceleration)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *base;
	const BYTE *lowLimit;
	const BYTE * const lowRefLimit = ip - dictPtr->dictSize;
	const BYTE *anchor = (const BYTE *) source;
	const BYTE * const iend = ip + inputSize;
	const BYTE * const mflimit = iend - MFLIMIT;
	const BYTE * const matchlimit = iend - LASTLITERALS;

	BYTE *op = (BYTE *) dest;
	BYTE * const olimit = op + maxOutputSize;

	U32 forwardH;
	size_t refDelta = 0;

	/* Init conditions */
	if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE) {
		/* Unsupported inputSize, too large (or negative) */
		return 0;
	}

	switch (dict) {
	case noDict:
	default:
		base = (const BYTE *)source;
		lowLimit = (const BYTE *)source;
		break;
	case withPrefix64k:
		base = (const BYTE *)source - dictPtr->currentOffset;
		lowLimit = (const BYTE *)source - dictPtr->dictSize;
		break;
	}

	if ((tableType == byU16)
		&& (inputSize >= LZ4_64Klimit)) {
		/* Size too large (not within 64K limit) */
		return 0;
	}

	if (inputSize < LZ4_minLength) {
		/* Input too small, no compression (all literals) */
		goto _last_literals;
	}

	/* First Byte */
	LZ4_putPosition(ip, dictPtr->hashTable, tableType, base);
	ip++;
	forwardH = LZ4_hashPosition(ip, tableType);

	/* Main Loop */
	for ( ; ; ) {
		const BYTE *match;
		BYTE *token;

		/* Find a match */
		{
			const BYTE *forwardIp = ip;
			unsigned int step = 1;
			unsigned int searchMatchNb = acceleration << LZ4_SKIPTRIGGER;

			do {
				U32 const h = forwardH;

				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_SKIPTRIGGER);

				if (unlikely(forwardIp > mflimit))
					goto _last_literals;

				match = LZ4_getPositionOnHash(h,
					dictPtr->hashTable,
					tableType, base);

				forwardH = LZ4_hashPosition(forwardIp,
					tableType);

				LZ4_putPositionOnHash(ip, h, dictPtr->hashTable,
					tableType, base);
			} while (((dictIssue == dictSmall)
					? (match < lowRefLimit)
					: 0)
				|| ((tableType == byU16)
					? 0
					: (match + MAX_DISTANCE < ip))
				|| (LZ4_read32(match + refDelta)
					!= LZ4_read32(ip)));
		}

		/* Catch up */
		while (((ip > anchor) & (match + refDelta > lowLimit))
				&& (unlikely(ip[-1] == match[refDelta - 1]))) {
			ip--;
			match--;
		}

		/* Encode Literals */
		{
			unsigned const int litLength = (unsigned int)(ip - anchor);

			token = op++;

			if ((outputLimited) &&
				/* Check output buffer overflow */
				(unlikely(op + litLength +
					(2 + 1 + LASTLITERALS) +
					(litLength / 255) > olimit)))
				return 0;

			if (litLength >= RUN_MASK) {
				int len = (int)litLength - RUN_MASK;

				*token = (RUN_MASK << ML_BITS);

				for (; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = (BYTE)len;
			} else
				*token = (BYTE)(litLength << ML_BITS);

			/* Copy Literals */
			LZ4_wildCopy(op, anchor, op + litLength);
			op += litLength;
		}

_next_match:
		/* Encode Offset */
		LZ4_writeLE16(op, (U16)(ip - match));
		op += 2;

		/* Encode MatchLength */
		{
			unsigned int matchCode;

			matchCode = LZ4_count(ip + MINMATCH,
				match + MINMATCH, matchlimit);
			ip += MINMATCH + matchCode;

			if (outputLimited &&
				/* Check output buffer overflow */
				(unlikely(op +
					(1 + LASTLITERALS) +
					(matchCode >> 8) > olimit)))
				return 0;

			if (matchCode >= ML_MASK) {
				*token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4_write32(op, 0xFFFFFFFF);

				while (matchCode >= 4 * 255) {
					op += 4;
					LZ4_write32(op, 0xFFFFFFFF);
					matchCode -= 4 * 255;
				}

				op += matchCode / 255;
				*op++ = (BYTE)(matchCode % 255);
			} else
				*token += (BYTE)(matchCode);
		}

		anchor = ip;

		/* Test end of chunk */
		if (ip > mflimit)
			break;

		/* Fill table */
		LZ4_putPosition(ip - 2, dictPtr->hashTable, tableType, base);

		/* Test next position */
		match = LZ4_getPosition(ip, dictPtr->hashTable,
			tableType, base);

		LZ4_putPosition(ip, dictPtr->hashTable, tableType, base);

		if (((dictIssue == dictSmall) ? (match >= lowRefLimit) : 1)
			&& (match + MAX_DISTANCE >= ip)
			&& (LZ4_read32(match + refDelta) == LZ4_read32(ip))) {
			token = op++;
			*token = 0;
			goto _next_match;
		}

		/* Prepare next loop */
		forwardH = LZ4_hashPosition(++ip, tableType);
	}

_last_literals:
	/* Encode Last Literals */
	{
		size_t const lastRun = (size_t)(iend - anchor);

		if ((outputLimited) &&
			/* Check output buffer overflow */
			((op - (BYTE *)dest) + lastRun + 1 +
			((lastRun + 255 - RUN_MASK) / 255) > (U32)maxOutputSize))
			return 0;

		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;
			*op++ = RUN_MASK << ML_BITS;
			for (; accumulator >= 255; accumulator -= 255)
				*op++ = 255;
			*op++ = (BYTE) accumulator;
		} else {
			*op++ = (BYTE)(lastRun << ML_BITS);
		}

		memcpy(op, anchor, lastRun);

		op += lastRun;
	}

	/* End */
	return (int) (((char *)op) - dest);
}

static int LZ4_compress_fast_extState(
	void *state,
	const char *source,
	char *dest,
	int inputSize,
	int maxOutputSize,
	int acceleration)
{
	LZ4_stream_t_internal *ctx = &((LZ4_stream_t *)state)->internal_donotuse;
#if LZ4_ARCH64
	const tableType_t tableType = byU32;
#else
	const tableType_t tableType = byPtr;
#endif

	memset(state, 0, sizeof(LZ4_stream_t));

	if (acceleration < 1)
		acceleration = LZ4_ACCELERATION_DEFAULT;

	if (maxOutputSize >= LZ4_COMPRESSBOUND(inputSize)) {
		if (inputSize < LZ4_64Klimit)
			return LZ4_compress_generic(ctx, source,
				dest, inputSize, 0,
				noLimit, byU16, noDict,
				noDictIssue, acceleration);
		else
			return LZ4_compress_generic(ctx, source,
				dest, inputSize, 0,
				noLimit, tableType, noDict,
				noDictIssue, acceleration);
	} else {
		if (inputSize < LZ4_64Klimit)
			return LZ4_compress_generic(ctx, source,
				dest, inputSize,
				maxOutputSize, limitedOutput, byU16, noDict,
				noDictIssue, acceleration);
		else
			return LZ4_compress_generic(ctx, source,
				dest, inputSize,
				maxOutputSize, limitedOutput, tableType, noDict,
				noDictIssue, acceleration);
	}
}

static int LZ4_compress_fast(const char *source, char *dest, int inputSize,
	int maxOutputSize, int acceleration, void *wrkmem)
{
	return LZ4_compress_fast_extState(wrkmem, source, dest, inputSize,
		maxOutputSize, acceleration);
}

int LZ4_compress_default(const char *source, char *dest, int inputSize,
	int maxOutputSize, void *wrkmem)
{
	return LZ4_compress_fast(source, dest, inputSize,
		maxOutputSize, LZ4_ACCELERATION_DEFAULT, wrkmem);
}
EXPORT_SYMBOL(LZ4_compress_default);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LZ4 compressor");
