#pragma once

#include <cstdint>

// Compile-time constants for Vitis HLS Prototype
constexpr int N              = 16384;  // Polynomial degree
constexpr int LOG2_N         = 14;     // = log2(N)
constexpr int MAX_LIMBS      = 15;     // Maximum RNS chain length (11 Q + 4 P)
constexpr int WORD_BITS      = 64;     // Bits per RNS residue
constexpr int NUM_BFLY       = 8;      // Parallel butterfly units in NTT
constexpr int AXI_WIDTH      = 512;    // AXI data bus width (bits)
constexpr int WORDS_PER_BEAT = AXI_WIDTH / WORD_BITS; // = 8
constexpr int POLY_WORDS     = N;      // Words per single limb
constexpr int POLY_TOTAL     = N * MAX_LIMBS; // Words per full polynomial
constexpr int CT_TOTAL       = 2 * POLY_TOTAL; // Words per ciphertext

// Operation Codes for Top-Level Kernel
constexpr uint32_t OP_NTT           = 1;
constexpr uint32_t OP_INTT          = 2;
constexpr uint32_t OP_POLY_ADD      = 3;
constexpr uint32_t OP_POLY_SUB      = 4;
constexpr uint32_t OP_POLY_MUL_NTT  = 5;
constexpr uint32_t OP_AUTOMORPHISM  = 6;
constexpr uint32_t OP_KEY_SWITCH    = 7;
constexpr uint32_t OP_RESCALE       = 8;
constexpr uint32_t OP_LOAD_CONTEXT  = 99;
