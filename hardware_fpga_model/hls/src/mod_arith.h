#ifndef MOD_ARITH_H
#define MOD_ARITH_H
#pragma once
#include <stdint.h>
#include <ap_int.h>

uint64_t mod_add(uint64_t a, uint64_t b, uint64_t q);
uint64_t mod_sub(uint64_t a, uint64_t b, uint64_t q);
uint64_t mod_mul(uint64_t a, uint64_t b, uint64_t q, ap_uint<128> m, uint32_t k);

#endif
