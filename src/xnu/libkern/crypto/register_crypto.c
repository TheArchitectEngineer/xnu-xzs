/*
 * Copyright (c) 2012 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <libkern/libkern.h>
#include <libkern/crypto/register_crypto.h>
#include <libkern/crypto/crypto_internal.h>
#include <libkern/crypto/rand.h>
#include <libkern/section_keywords.h>

#include <corecrypto/ccdrbg.h>
#include <corecrypto/ccsha2.h>
#include <corecrypto/ccrng.h>

#include <kern/startup.h>

extern uint64_t early_random(void);
extern uint64_t ml_get_timebase(void);
extern int cpu_number(void);

SECURITY_READ_ONLY_LATE(bool) crypto_init = false;
SECURITY_READ_ONLY_LATE(crypto_functions_t) g_crypto_funcs = NULL;

int
register_crypto_functions(const crypto_functions_t funcs)
{
	if (g_crypto_funcs) {
		return -1;
	}

	g_crypto_funcs = funcs;
	crypto_init = true;

	return 0;
}

/*
 * =====================================================================
 * XZS_CRYPTO_PROVIDER_COMPATIBILITY
 *
 * Provides minimum required crypto provider callbacks for XNU KMEM RNG
 * and ccrng compatibility on Sony Xperia XZs (MSM8996).
 *
 * Technical Debt / Classification:
 * - XZS_CRYPTO_PROVIDER_COMPATIBILITY
 * - Boot entropy fallback: relies on early_random() / bootseed_init()
 * - SCTLR.C = 0 bring-up compatible (lock-free, no exclusive atomic loops)
 * - Implements minimum required subset (KMEM RNG + ccrng)
 * =====================================================================
 */

struct xzs_kmem_rng_ctx {
	_Alignas(uint64_t) uint8_t drbg_state[160];
	uint64_t gen_counter;
};

_Static_assert(sizeof(struct xzs_kmem_rng_ctx) <= CRYPTO_RANDOM_MAX_CTX_SIZE,
    "xzs_kmem_rng_ctx exceeds CRYPTO_RANDOM_MAX_CTX_SIZE (256)");
_Static_assert(sizeof(struct xzs_kmem_rng_ctx) == 168,
    "xzs_kmem_rng_ctx unexpected size");

static struct ccdrbg_info xzs_kmem_drbg_info;
static const struct ccdrbg_nisthmac_custom xzs_kmem_drbg_custom = {
	.di         = &ccsha256_ltc_di,
	.strictFIPS = 0,
};

static size_t
xzs_kmem_ctx_size(void)
{
	return sizeof(struct xzs_kmem_rng_ctx);
}

static void
xzs_kmem_init(crypto_random_ctx_t ctx)
{
	struct xzs_kmem_rng_ctx *rng = (struct xzs_kmem_rng_ctx *)ctx;
	uint8_t seed[32];
	uint64_t nonce[2];
	const char ps[] = "xnu.xzs.kmem.rng";

	/*
	 * Seed material: 32 bytes from early_random() (CSPRNG NIST HMAC-DRBG).
	 * Diversification / Personalization: ctx address, cpu_number(), ml_get_timebase().
	 * No global atomic counter, no shared mutable state.
	 */
	for (int i = 0; i < 4; i++) {
		uint64_t r = early_random();
		memcpy(&seed[i * sizeof(uint64_t)], &r, sizeof(uint64_t));
	}

	nonce[0] = ml_get_timebase();
	nonce[1] = ((uint64_t)cpu_number() << 32) | ((uint32_t)(uintptr_t)ctx);

	int rc = ccdrbg_init(&xzs_kmem_drbg_info,
	    (struct ccdrbg_state *)rng->drbg_state,
	    sizeof(seed), seed,
	    sizeof(nonce), nonce,
	    sizeof(ps) - 1, ps);
	if (__improbable(rc != CCDRBG_STATUS_OK)) {
		extern void xzs_early_puts(const char *s);
		extern void xzs_early_puthex64(uint64_t v);
		xzs_early_puts("    [XZS-CRYPTO] ERROR: ccdrbg_init failed rc=");
		xzs_early_puthex64((uint64_t)rc);
		xzs_early_puts("\n");
		panic("[XZS-CRYPTO] ccdrbg_init failed with status %d", rc);
	}

	rng->gen_counter = 0;
	memset(seed, 0, sizeof(seed));
	memset(nonce, 0, sizeof(nonce));
}

static void
xzs_kmem_generate(crypto_random_ctx_t ctx, void *random, size_t random_size)
{
	struct xzs_kmem_rng_ctx *rng = (struct xzs_kmem_rng_ctx *)ctx;

	int rc = ccdrbg_generate(&xzs_kmem_drbg_info,
	    (struct ccdrbg_state *)rng->drbg_state,
	    random_size, random,
	    0, NULL);
	if (__improbable(rc != CCDRBG_STATUS_OK)) {
		extern void xzs_early_puts(const char *s);
		extern void xzs_early_puthex64(uint64_t v);
		xzs_early_puts("    [XZS-CRYPTO] ERROR: ccdrbg_generate failed rc=");
		xzs_early_puthex64((uint64_t)rc);
		xzs_early_puts("\n");
		panic("[XZS-CRYPTO] ccdrbg_generate failed with status %d", rc);
	}

	rng->gen_counter++;
}

static void
xzs_kmem_uniform(crypto_random_ctx_t ctx, uint64_t bound, uint64_t *random)
{
	assert(bound > 0);
	if (__improbable(bound == 0)) {
		panic("[XZS-CRYPTO] xzs_kmem_uniform called with bound == 0");
	}
	if (bound == 1) {
		*random = 0;
		return;
	}

	uint64_t x;
	/* Daniel Lemire nearly-divisionless algorithm: threshold = 2^64 % bound */
	uint64_t threshold = (-bound) % bound;

	for (;;) {
		xzs_kmem_generate(ctx, &x, sizeof(x));
		__uint128_t m = (__uint128_t)x * (__uint128_t)bound;
		uint64_t l = (uint64_t)m;
		if (__probable(l >= threshold)) {
			*random = (uint64_t)(m >> 64);
			return;
		}
	}
}

/*
 * ccrng_fn support for cc_rand_generate() callers:
 * (e.g. vm_compressor_backing_store, netkey, necp)
 */
static int
xzs_ccrng_generate(struct ccrng_state *rng, size_t outlen, void *out)
{
	(void)rng;
	uint8_t *p = (uint8_t *)out;
	while (outlen >= sizeof(uint64_t)) {
		uint64_t r = early_random();
		memcpy(p, &r, sizeof(uint64_t));
		p += sizeof(uint64_t);
		outlen -= sizeof(uint64_t);
	}
	if (outlen > 0) {
		uint64_t r = early_random();
		memcpy(p, &r, outlen);
	}
	return 0;
}

static struct ccrng_state xzs_default_ccrng_state = {
	.generate = xzs_ccrng_generate,
};

static struct ccrng_state *
xzs_ccrng_fn(int *error)
{
	if (error) {
		*error = 0;
	}
	return &xzs_default_ccrng_state;
}

static struct crypto_functions xzs_crypto_funcs = {
	.random_kmem_ctx_size_fn = xzs_kmem_ctx_size,
	.random_kmem_init_fn     = xzs_kmem_init,
	.random_generate_fn      = xzs_kmem_generate,
	.random_uniform_fn       = xzs_kmem_uniform,
	.ccrng_fn                = xzs_ccrng_fn,
};

__startup_func
static void
xzs_crypto_provider_init(void)
{
	extern void xzs_early_puts(const char *s);
	extern void xzs_early_puthex64(uint64_t v);

	xzs_early_puts("[XZS-CRYPTO] provider init ENTER\n");

	ccdrbg_factory_nisthmac(&xzs_kmem_drbg_info, &xzs_kmem_drbg_custom);

	xzs_early_puts("[XZS-CRYPTO] ccdrbg_info.size = ");
	xzs_early_puthex64((uint64_t)xzs_kmem_drbg_info.size);
	xzs_early_puts("\n");

	if (xzs_kmem_drbg_info.size > sizeof(((struct xzs_kmem_rng_ctx *)0)->drbg_state)) {
		panic("[XZS-CRYPTO] ccdrbg_info.size exceeds drbg_state buffer");
	}

	xzs_early_puts("[XZS-CRYPTO] ctx size = ");
	xzs_early_puthex64((uint64_t)sizeof(struct xzs_kmem_rng_ctx));
	xzs_early_puts("\n");

	int rc = register_crypto_functions(&xzs_crypto_funcs);
	xzs_early_puts("[XZS-CRYPTO] register_crypto_functions rc = ");
	xzs_early_puthex64((uint64_t)rc);
	xzs_early_puts("\n");

	xzs_early_puts("[XZS-CRYPTO] g_crypto_funcs = ");
	xzs_early_puthex64((uint64_t)g_crypto_funcs);
	xzs_early_puts("\n");

	if (rc != 0 || g_crypto_funcs == NULL) {
		panic("[XZS-CRYPTO] register_crypto_functions failed");
	}

	xzs_early_puts("[XZS-CRYPTO] provider init RETURN\n");
}
STARTUP(EARLY_BOOT, STARTUP_RANK_FOURTH, xzs_crypto_provider_init);

