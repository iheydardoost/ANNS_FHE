/**
 * @file minimum.h
 * @brief OpenFHE CKKS Statistical Operations - Homomorphic Minimum Value Algorithms.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_MINIMUM_H
#define OPENFHE_STATS_MINIMUM_H

#include "openfhe.h"
#include "utils-basics.h"
#include "utils-eval.h"
#include "utils-matrices.h"
#include "utils-ptxt.h"
#include "ranking.h"

namespace openfhe_stats {

using namespace lbcrypto;

/**
 * @brief Finds the minimum element in a plaintext vector of doubles.
 */
double min(
    const std::vector<double> &vec
);

/**
 * @brief Computes a ciphertext bitmask representing the position of the minimum value in a ciphertext vector.
 */
Ciphertext<DCRTPoly> min(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    const double leftBoundC,
    const double rightBoundC,
    const uint32_t degreeC,
    const uint32_t degreeI
);

/**
 * @brief Computes minimum position mask across a vector stored in multiple ciphertexts.
 */
Ciphertext<DCRTPoly> min(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    const double leftBoundC,
    const double rightBoundC,
    const uint32_t degreeC,
    const uint32_t degreeI
);

/**
 * @brief Computes minimum position mask across multiple ciphertexts using fg polynomial approximation.
 */
Ciphertext<DCRTPoly> minFG(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    const uint32_t dg_c,
    const uint32_t df_c,
    const uint32_t dg_i,
    const uint32_t df_i
);

} // namespace openfhe_stats

#endif // OPENFHE_STATS_MINIMUM_H
