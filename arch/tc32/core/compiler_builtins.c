/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/sys/util.h>

union u64_words {
	uint64_t value;
	struct {
		uint32_t lo;
		uint32_t hi;
	} word;
};

static void u64_shl1(union u64_words *value)
{
	value->word.hi = (value->word.hi << 1) | (value->word.lo >> 31);
	value->word.lo <<= 1;
}

static void u64_set_bit(union u64_words *value, unsigned int bit)
{
	if (bit >= 32U) {
		value->word.hi |= BIT(bit - 32U);
	} else {
		value->word.lo |= BIT(bit);
	}
}

static uint32_t u64_get_bit(union u64_words value, unsigned int bit)
{
	if (bit >= 32U) {
		return (value.word.hi >> (bit - 32U)) & 1U;
	}

	return (value.word.lo >> bit) & 1U;
}

static int u64_ge(union u64_words lhs, union u64_words rhs)
{
	if (lhs.word.hi != rhs.word.hi) {
		return lhs.word.hi > rhs.word.hi;
	}

	return lhs.word.lo >= rhs.word.lo;
}

static void u64_sub(union u64_words *lhs, union u64_words rhs)
{
	uint32_t borrow = lhs->word.lo < rhs.word.lo;

	lhs->word.lo -= rhs.word.lo;
	lhs->word.hi = lhs->word.hi - rhs.word.hi - borrow;
}

uint64_t __muldi3(uint64_t a, uint64_t b)
{
	union u64_words multiplicand = { .value = a };
	union u64_words multiplier = { .value = b };
	union u64_words result = { .value = 0 };

	while ((multiplier.word.lo != 0U) || (multiplier.word.hi != 0U)) {
		if ((multiplier.word.lo & 1U) != 0U) {
			uint32_t carry = UINT32_MAX - result.word.lo < multiplicand.word.lo;

			result.word.lo += multiplicand.word.lo;
			result.word.hi += multiplicand.word.hi + carry;
		}

		multiplier.word.lo = (multiplier.word.lo >> 1) | (multiplier.word.hi << 31);
		multiplier.word.hi >>= 1;
		u64_shl1(&multiplicand);
	}

	return result.value;
}

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor)
{
	union u64_words numerator = { .value = dividend };
	union u64_words denominator = { .value = divisor };
	union u64_words quotient = { .value = 0 };
	union u64_words remainder = { .value = 0 };

	if ((denominator.word.lo == 0U) && (denominator.word.hi == 0U)) {
		return UINT64_MAX;
	}

	for (int bit = 63; bit >= 0; bit--) {
		u64_shl1(&remainder);
		remainder.word.lo |= u64_get_bit(numerator, bit);

		if (u64_ge(remainder, denominator)) {
			u64_sub(&remainder, denominator);
			u64_set_bit(&quotient, bit);
		}
	}

	return quotient.value;
}
