/**
 * @file ranking.h
 * @brief OpenFHE CKKS Statistical Operations - Homomorphic Ranking Algorithms.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_RANKING_H
#define OPENFHE_STATS_RANKING_H

#include "openfhe.h"
#include "utils-basics.h"
#include "utils-eval.h"
#include "utils-matrices.h"
#include "utils-ptxt.h"

namespace openfhe_stats {

using namespace lbcrypto;

/**
 * @brief Plaintext rank of elements in a vector of doubles.
 * @param vec Input vector.
 * @param fractional If true, computes fractional rank for tied elements.
 * @param epsilon Equality tolerance threshold.
 * @return Vector of double ranks.
 */
std::vector<double> rank(
    const std::vector<double> &vec,
    const bool fractional = true,
    const double epsilon = 0.0
);

/**
 * @brief Computes homomorphic rank for elements in a single ciphertext vector.
 */
Ciphertext<DCRTPoly> rank(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    const double leftBoundC,
    const double rightBoundC,
    const uint32_t degreeC,
    const bool cmpGt = false
);

/**
 * @brief Computes homomorphic rank for elements stored across multiple ciphertexts.
 */
std::vector<Ciphertext<DCRTPoly>> rank(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    const double leftBoundC,
    const double rightBoundC,
    const uint32_t degreeC,
    const bool cmpGt = false,
    const bool complOpt = true
);

/**
 * @brief Computes rank of elements in a single ciphertext with tie-correction.
 */
Ciphertext<DCRTPoly> rankWithCorrection(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    const double leftBoundC,
    const double rightBoundC,
    const uint32_t degreeC,
    const bool parallel = false
);

/**
 * @brief Computes rank of elements stored across multiple ciphertexts with tie-correction.
 */
std::vector<Ciphertext<DCRTPoly>> rankWithCorrection(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    const double leftBoundC,
    const double rightBoundC,
    const uint32_t degreeC
);

/**
 * @brief Computes rank across multiple ciphertexts using fg composition polynomial comparison.
 */
std::vector<Ciphertext<DCRTPoly>> rankFG(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    const uint32_t dg,
    const uint32_t df,
    const bool cmpGt = false,
    const bool complOpt = true
);

/**
 * @brief Computes rank in a single ciphertext with tie-correction using fg polynomial comparison.
 */
Ciphertext<DCRTPoly> rankWithCorrectionFG(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    const uint32_t dg,
    const uint32_t df
);

/**
 * @brief Computes rank across multiple ciphertexts with tie-correction using fg polynomial comparison.
 */
std::vector<Ciphertext<DCRTPoly>> rankWithCorrectionFG(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    const uint32_t dg,
    const uint32_t df
);

} // namespace openfhe_stats

#endif // OPENFHE_STATS_RANKING_H
