/**
 * @file utils-basics.h
 * @brief OpenFHE CKKS Statistical Operations - Basic Context and Key Generation Utilities.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_UTILS_BASICS_H
#define OPENFHE_STATS_UTILS_BASICS_H

#include "openfhe.h"
#include <vector>
#include <cstdint>

namespace openfhe_stats {

using namespace lbcrypto;

/**
 * @brief Ciphertext left shift (evaluates EvalRotate with positive index).
 */
template <typename Element>
Ciphertext<Element> operator<<(const Ciphertext<Element> &a, int32_t index) {
    return a->GetCryptoContext()->EvalRotate(a, index);
}

/**
 * @brief Ciphertext right shift (evaluates EvalRotate with negative index).
 */
template <typename Element>
Ciphertext<Element> operator>>(const Ciphertext<Element> &a, int32_t index) {
    return a->GetCryptoContext()->EvalRotate(a, -index);
}

/**
 * @brief Add a scalar double to a Ciphertext.
 */
template <typename Element>
Ciphertext<Element> operator+(const Ciphertext<Element> &a, const double &b) {
    return a->GetCryptoContext()->EvalAdd(a, b);
}

/**
 * @brief Add a vector of doubles to a Ciphertext as packed CKKS plaintext.
 */
template <typename Element>
Ciphertext<Element> operator+(const Ciphertext<Element> &a, const std::vector<double> &b) {
    CryptoContext<Element> cc = a->GetCryptoContext();
    auto plaintext = cc->MakeCKKSPackedPlaintext(b);
    return cc->EvalAdd(a, plaintext);
}

/**
 * @brief Subtract a Ciphertext from a scalar double.
 */
template <typename Element>
Ciphertext<Element> operator-(const double &a, const Ciphertext<Element> &b) {
    return b->GetCryptoContext()->EvalSub(a, b);
}

/**
 * @brief Subtract a scalar double from a Ciphertext.
 */
template <typename Element>
Ciphertext<Element> operator-(const Ciphertext<Element> &a, const double &b) {
    return a->GetCryptoContext()->EvalSub(a, b);
}

/**
 * @brief Multiply a Ciphertext by a vector of doubles as packed CKKS plaintext.
 */
template <typename Element>
Ciphertext<Element> operator*(const Ciphertext<Element> &a, const std::vector<double> &b) {
    CryptoContext<Element> cc = a->GetCryptoContext();
    auto plaintext = cc->MakeCKKSPackedPlaintext(b);
    return cc->EvalMult(a, plaintext);
}

/**
 * @brief Multiply a vector of doubles by a Ciphertext.
 */
template <typename Element>
Ciphertext<Element> operator*(const std::vector<double> &a, const Ciphertext<Element> &b) {
    return b * a;
}

/**
 * @brief Multiply a Ciphertext by a scalar double.
 */
template <typename Element>
Ciphertext<Element> operator*(const Ciphertext<Element> &a, const double b) {
    CryptoContext<Element> cc = a->GetCryptoContext();
    return cc->EvalMult(a, b);
}

/**
 * @brief Multiply a scalar double by a Ciphertext.
 */
template <typename Element>
Ciphertext<Element> operator*(const double a, const Ciphertext<Element> &b) {
    return b * a;
}

} // namespace openfhe_stats

#endif // OPENFHE_STATS_UTILS_BASICS_H
