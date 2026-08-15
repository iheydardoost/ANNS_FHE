#ifndef RESCALE_H
#define RESCALE_H

#include <stdint.h>
#include "ntt.h"
#include "mod_arith.h"

// Rescale operation for CKKS
// a_in: input polynomial with L limbs, each of size N (evaluation domain)
// a_out: output polynomial with L-1 limbs, each of size N (evaluation domain)
// num_limbs: L (the number of limbs of the input polynomial)
// q: array of moduli (size L)
// m_barrett: array of Barrett constants (size L)
// k_barrett: array of Barrett k values (size L)
// inv_twiddles: array of INTT twiddles for limb L-1 (size N)
// twiddles: array of NTT twiddles for limbs 0 to L-2 (size (L-1)*N)
// n_inv_mod_q_last: N^-1 mod q_last
// QlQlInvModqlDivqlModq: scaling constants (size L-1)
// qlInvModq: inverse constants (size L-1)
void rescale(const uint64_t* a_in, uint64_t* a_out,
             uint32_t num_limbs,
             const uint64_t* q,
             const ap_uint<128>* m_barrett,
             const uint32_t* k_barrett,
             const uint64_t* inv_twiddles,
             const uint64_t* twiddles,
             uint64_t n_inv_mod_q_last,
             const uint64_t* QlQlInvModqlDivqlModq,
             const uint64_t* qlInvModq);

#endif
