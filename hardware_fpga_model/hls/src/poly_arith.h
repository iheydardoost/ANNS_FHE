#pragma once

#include "hls_params.h"
#include "mod_arith.h"

// Polynomial Addition
#include "mod_arith.h"
#include <ap_int.h>

void poly_add(
    const uint64_t* a,
    const uint64_t* b,
    uint64_t* out,
    uint64_t q
);

// Polynomial Subtraction
void poly_sub(
    const uint64_t* a,
    const uint64_t* b,
    uint64_t* out,
    uint64_t q
);

// Polynomial Multiplication (Component-wise, arrays are in Evaluation format)
void poly_mul(
    const uint64_t* a,
    const uint64_t* b,
    uint64_t* out,
    uint64_t q,
    ap_uint<128> m_barrett,
    uint32_t k_barrett
);

// Polynomial Multiplication by constant
void poly_mul_const(
    const uint64_t* a,
    uint64_t c,
    uint64_t* out,
    uint64_t q,
    ap_uint<128> m_barrett,
    uint32_t k_barrett
);

// Polynomial Addition with constant
void poly_add_const(
    const uint64_t* a,
    uint64_t c,
    uint64_t* out,
    uint64_t q
);
