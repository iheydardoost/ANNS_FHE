#ifndef SIGN_APPROXIMATOR_H
#define SIGN_APPROXIMATOR_H

#include "openfhe.h"
#include "fhe_config.h"

namespace anns_fhe
{

    /**
     * SignApproximator: Homomorphic sign/comparison and indicator functions for HMR.
     *
     * Generic, parameterized functions for Chebyshev approximation of:
     *   1. Step function: sign(x) ≈ 1 if x > 0, 0 if x < 0, 0.5 if x = 0
     *   2. Indicator function: I(x) ≈ 1 if x < threshold, 0 if x > threshold, 0.5 if x = threshold
     */
    class SignApproximator
    {
    public:
        SignApproximator() = default;

        /**
         * Evaluate Chebyshev-approximated sign function with explicit degree.
         */
        static lbcrypto::Ciphertext<lbcrypto::DCRTPoly> eval_sign(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_diff,
            double left_bound,
            double right_bound,
            uint32_t degree,
            const double error);

        /**
         * Evaluate Chebyshev-approximated indicator function I(x < threshold).
         */
        static lbcrypto::Ciphertext<lbcrypto::DCRTPoly> eval_indicator(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_input,
            double threshold_min,
            double threshold_max,
            double left_bound,
            double right_bound,
            uint32_t degree);

        /**
         * Depth consumed by Chebyshev approximation for a given degree.
         */
        static int depth_for_degree(int degree);
    };

} // namespace anns_fhe

#endif // SIGN_APPROXIMATOR_H
