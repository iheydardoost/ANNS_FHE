/**
 * @file utils-eval.h
 * @brief OpenFHE CKKS Statistical Operations - Polynomial & Function Evaluation Utilities.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_UTILS_EVAL_H
#define OPENFHE_STATS_UTILS_EVAL_H

#include "utils-basics.h"

namespace openfhe_stats {

using namespace lbcrypto;

/**
 * @brief Compute the highest polynomial degree that can be evaluated in a circuit of given depth.
 * @param depth Multiplicative depth.
 * @return Max polynomial degree supported at this depth.
 */
usint depth2degree(
    const usint depth
);

/**
 * @brief Compares two ciphertexts c1 and c2 as (c1 == c2).
 * Uses Chebyshev approximation of indicator function around zero.
 * @return Ciphertext bitmask where ~1 indicates c1 == c2 and 0 indicates c1 != c2.
 */
Ciphertext<DCRTPoly> equal(
    const Ciphertext<DCRTPoly> &c1,
    const Ciphertext<DCRTPoly> &c2,
    const double a,
    const double b,
    const uint32_t degree,
    const double error = 0.00001
);

/**
 * @brief Compares two ciphertexts c1 and c2 as (c1 > c2).
 * Uses Chebyshev approximation of sign function.
 * @return Ciphertext output where 1 => c1 > c2, 0 => c1 < c2, 0.5 => c1 == c2.
 */
Ciphertext<DCRTPoly> compare(
    const Ciphertext<DCRTPoly> &c1,
    const Ciphertext<DCRTPoly> &c2,
    const double a,
    const double b,
    const uint32_t degree,
    const double error = 0.00001
);

/**
 * @brief Advanced sign evaluation using fg composition polynomials (Cheon et al., ASIACRYPT '20).
 * @param c Input ciphertext.
 * @param dg Composition degree of g.
 * @param df Composition degree of f.
 * @return Evaluated sign ciphertext.
 */
Ciphertext<DCRTPoly> signAdv(
    Ciphertext<DCRTPoly> &c,
    const size_t dg,
    const size_t df
);

/**
 * @brief Advanced comparison (c1 > c2) using fg sign approximation.
 * @param c1 First ciphertext.
 * @param c2 Second ciphertext.
 * @param dg Composition degree of g.
 * @param df Composition degree of f.
 * @return Ciphertext representing result of (c1 > c2).
 */
Ciphertext<DCRTPoly> compareAdv(
    const Ciphertext<DCRTPoly> &c1,
    const Ciphertext<DCRTPoly> &c2,
    const size_t dg,
    const size_t df
);

/**
 * @brief Strict greater-than comparison (c1 > c2) returning 1 for strictly greater and 0 otherwise.
 */
Ciphertext<DCRTPoly> compareGt(
    const Ciphertext<DCRTPoly> &c1,
    const Ciphertext<DCRTPoly> &c2,
    const double a,
    const double b,
    const uint32_t degree,
    const double error = 0.00001
);

/**
 * @brief Evaluates indicator function for interval [a1, b1] using Chebyshev approximation.
 */
Ciphertext<DCRTPoly> indicator(
    const Ciphertext<DCRTPoly> &c,
    const double a1,
    const double b1,
    const double a,
    const double b,
    const uint32_t degree
);

/**
 * @brief Evaluates indicator function for interval [-0.5, 0.5] using fg sign approximation.
 */
Ciphertext<DCRTPoly> indicatorAdv(
    const Ciphertext<DCRTPoly> &c,
    const double b,
    const size_t dg,
    const size_t df
);

/**
 * @brief Evaluates indicator function for interval [(b-1)/2, (b+3)/2] using fg sign approximation.
 */
Ciphertext<DCRTPoly> indicatorAdvShifted(
    const Ciphertext<DCRTPoly> &c,
    const double b,
    const size_t dg,
    const size_t df
);

} // namespace openfhe_stats

#endif // OPENFHE_STATS_UTILS_EVAL_H
