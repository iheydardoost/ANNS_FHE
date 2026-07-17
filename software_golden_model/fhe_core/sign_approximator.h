#ifndef SIGN_APPROXIMATOR_H
#define SIGN_APPROXIMATOR_H

#include "openfhe.h"
#include "fhe_config.h"

namespace anns_fhe
{

    /**
     * SignApproximator: Homomorphic sign/comparison function for HMR.
     *
     * Applies an approximate sign function to a ciphertext whose slots contain
     * values in the range [left_bound, right_bound]. The sign function returns
     * values in approximately [0, 1]:
     *   sign(x) ≈ 1  if x > 0  (first element is larger)
     *   sign(x) ≈ 0  if x < 0  (second element is larger)
     *
     * Used in the HMR pairwise comparison step.
     */
    class SignApproximator
    {
    public:
        SignApproximator() = default;

        /**
         * Evaluate the Chebyshev-approximated sign function on a ciphertext.
         *
         * @param cc         CryptoContext (const ref — no deep copy)
         * @param ct_diff    Encrypted differences (slot_i = val_i - val_j) in [left, right]
         * @param left_bound Domain left bound for Chebyshev approximation (e.g. -1.0)
         * @param right_bound Domain right bound (e.g. 1.0)
         * @param config     FHEConfig containing chebyshev_degree and sign_approx_method
         * @return           Ciphertext with approximated sign values in [0, 1]
         */
        static lbcrypto::Ciphertext<lbcrypto::DCRTPoly> eval_sign(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_diff,
            double left_bound,
            double right_bound,
            const FHEConfig& config);

        /**
         * Depth consumed by the sign approximation for a given Chebyshev degree.
         * Reference: depth2degree mapping from Mazzone et al. (USENIX '25).
         *   degree  3 → depth 3   (not used)
         *   degree  5 → depth 4
         *   degree 13 → depth 5   ← our default
         *   degree 27 → depth 6
         *   degree 59 → depth 7
         */
        static int depth_for_degree(int degree);
    };

} // namespace anns_fhe

#endif // SIGN_APPROXIMATOR_H
