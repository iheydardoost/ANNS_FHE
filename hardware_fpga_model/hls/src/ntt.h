#ifndef NTT_H
#define NTT_H

#include <stdint.h>
#include "mod_arith.h"

// Forward NTT (in-place)
// N = 16384
// a: array of size N
// twiddles: array of twiddle factors
void ntt_forward(uint64_t* a, const uint64_t* twiddles, uint64_t q, ap_uint<128> m, uint32_t k);

// Inverse NTT (in-place)
// N = 16384
// a: array of size N
// inv_twiddles: array of inverse twiddle factors
// N_inv: N^-1 mod q
void intt_inverse(uint64_t* a, const uint64_t* inv_twiddles, uint64_t N_inv, uint64_t q, ap_uint<128> m, uint32_t k);

#endif
