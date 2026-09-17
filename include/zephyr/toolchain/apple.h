/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_TOOLCHAIN_APPLE_H_
#define ZEPHYR_INCLUDE_TOOLCHAIN_APPLE_H_

#ifndef ZEPHYR_INCLUDE_TOOLCHAIN_H_
#error Please do not include toolchain-specific headers directly, use <zephyr/toolchain.h> instead
#endif

#define FUNC_ALIAS(real_func, new_alias, return_type) \
	return_type new_alias(); \
	__asm__(".globl _" #new_alias "\n_" #new_alias " = _" #real_func)
#define Z_ALIAS_IMPL(return_type, name, args) \
	return_type __noinline name args
#define Z_ALIAS_IMPL_INLINE(return_type, name, args) \
	return_type __noinline name args
#define Z_ALIAS_DECL(real_func, new_alias, return_type, args) \
	return_type new_alias args; \
	__asm__(".globl _" #new_alias "\n_" #new_alias " = _" #real_func)

#include <zephyr/macho_iter_sections.h>

#define Z_MACHO_SEC_GET(token) _CONCAT(Z_MACHO_SEC_, token)
#define Z_MACHO_SECNAME(token) STRINGIFY(Z_MACHO_SEC_GET(token))

#define __GENERIC_SECTION(segment) \
	__attribute__((section("__DATA," STRINGIFY(segment))))
#define Z_GENERIC_SECTION(segment) __GENERIC_SECTION(segment)

#define __GENERIC_DOT_SECTION(segment) \
	__attribute__((section("__DATA,." STRINGIFY(segment))))
#define Z_GENERIC_DOT_SECTION(segment) __GENERIC_DOT_SECTION(segment)

#define ___in_section(a, b, c) \
	__attribute__((section("__DATA," Z_MACHO_SECNAME(a))))
#define __in_section(a, b, c) ___in_section(a, b, c)

#define ___in_section_unique(a, b) \
	__attribute__((section("__DATA," Z_MACHO_SECNAME(a))))

#if defined(__clang__) && !defined(__OBJC__)
#undef __weak
#define __weak __attribute__((__weak__))
#elif !defined(__weak)
#define __weak __attribute__((__weak__))
#endif

#endif /* ZEPHYR_INCLUDE_TOOLCHAIN_APPLE_H_ */
