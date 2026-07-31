/**
 * @file sorting.h
 * @brief OpenFHE CKKS Statistical Operations - Homomorphic Sorting Algorithms.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_SORTING_H
#define OPENFHE_STATS_SORTING_H

#include "openfhe.h"
#include "utils-basics.h"
#include "utils-eval.h"
#include "utils-matrices.h"
#include "utils-ptxt.h"

namespace openfhe_stats {

using namespace lbcrypto;

/**
 * @brief Sorts the elements of a plaintext vector in ascending order.
 */
std::vector<double> sort(
    std::vector<double> vec
);

/**
 * @brief Sorts elements in a single ciphertext vector.
 */
Ciphertext<DCRTPoly> sort(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    double leftBoundC,
    double rightBoundC,
    uint32_t degreeC,
    uint32_t degreeI
);

/**
 * @brief Sorts elements in a vector stored across multiple ciphertexts.
 */
std::vector<Ciphertext<DCRTPoly>> sort(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    double leftBoundC,
    double rightBoundC,
    uint32_t degreeC,
    uint32_t degreeI
);

/**
 * @brief Sorts elements in a single ciphertext vector with tie-correction.
 */
Ciphertext<DCRTPoly> sortWithCorrection(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    double leftBoundC,
    double rightBoundC,
    uint32_t degreeC,
    uint32_t degreeI
);

/**
 * @brief Sorts elements stored across multiple ciphertexts with tie-correction.
 */
std::vector<Ciphertext<DCRTPoly>> sortWithCorrection(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    double leftBoundC,
    double rightBoundC,
    uint32_t degreeC,
    uint32_t degreeI
);

/**
 * @brief Sorts elements in a single ciphertext using fg composition polynomial comparison.
 */
Ciphertext<DCRTPoly> sortFG(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    uint32_t dg_c,
    uint32_t df_c,
    uint32_t dg_i,
    uint32_t df_i
);

/**
 * @brief Sorts elements in a single ciphertext with tie-correction using fg polynomial comparison.
 */
Ciphertext<DCRTPoly> sortWithCorrectionFG(
    Ciphertext<DCRTPoly> c,
    const size_t vectorLength,
    uint32_t dg_c,
    uint32_t df_c,
    uint32_t dg_i,
    uint32_t df_i
);

/**
 * @brief Sorts elements stored across multiple ciphertexts with tie-correction using fg polynomial comparison.
 */
std::vector<Ciphertext<DCRTPoly>> sortWithCorrectionFG(
    const std::vector<Ciphertext<DCRTPoly>> &c,
    const size_t subVectorLength,
    uint32_t dg_c,
    uint32_t df_c,
    uint32_t dg_i,
    uint32_t df_i
);

} // namespace openfhe_stats

#endif // OPENFHE_STATS_SORTING_H
