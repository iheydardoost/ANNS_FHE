#include "sign_approximator.h"
#include <stdexcept>
#include <iostream>
#include <cmath>

using namespace lbcrypto;

namespace anns_fhe
{

// ---------------------------------------------------------------------------
// Depth consumed for a given Chebyshev degree.
// Derived from the depth2degree table in Mazzone et al. (USENIX '25)
// which gives max_degree that fits within a given depth budget.
// ---------------------------------------------------------------------------
int SignApproximator::depth_for_degree(int degree)
{
    // depth → max_degree mapping (from utils-eval.cpp)
    //  3 → 2,   4 → 5,  5 → 13,  6 → 27,  7 → 59,  8 → 119
    if (degree <=   2) return 3;
    if (degree <=   5) return 4;
    if (degree <=  13) return 5;
    if (degree <=  27) return 6;
    if (degree <=  59) return 7;
    if (degree <= 119) return 8;
    if (degree <= 247) return 9;
    return 10;  // fallback
}

// ---------------------------------------------------------------------------
// Evaluate the approximate sign function homomorphically.
//
// We use OpenFHE's EvalChebyshevFunction which internally uses the
// coefficient-based Chebyshev evaluation (more depth-efficient than
// naive monomial expansion).
//
// The sign function: f(x) = (x > 0) ? 1 : 0
// approximated on [left_bound, right_bound] via Chebyshev of degree d.
//
// NOTE: For HMR, the input ct_diff contains values (d_i - d_j) for all
// i,j pairs. We want sign(d_i - d_j) = 1 if d_i > d_j.
// Since d_i, d_j are non-negative distances, d_i - d_j ∈ [-max_d, +max_d].
// The caller is responsible for ensuring values are in [left_bound, right_bound].
// ---------------------------------------------------------------------------
Ciphertext<DCRTPoly> SignApproximator::eval_sign(
    const CryptoContext<DCRTPoly>& cc,
    const Ciphertext<DCRTPoly>& ct_diff,
    double left_bound,
    double right_bound,
    const FHEConfig& config)
{
    const std::string& method = config.sign_approx_method;

    if (method == "chebyshev")
    {
        // Use OpenFHE's built-in Chebyshev series evaluation.
        // EvalChebyshevFunction samples the function at Chebyshev nodes and
        // internally builds the coefficient-based expansion up to 'degree'.
        const uint32_t degree = static_cast<uint32_t>(config.chebyshev_degree);

        // sign-like function: (1 + tanh(x / epsilon)) / 2
        // For a hard sign approximation, we use the Chebyshev approximation
        // of the Heaviside step: H(x) = (sign(x) + 1) / 2
        // This maps: x < 0 → ≈0,  x > 0 → ≈1
        auto approx_heaviside = [](double x) -> double
        {
            if (x > 0.0) return 1.0;
            if (x < 0.0) return 0.0;
            return 0.5;  // tie-breaking at exactly 0
        };

        return cc->EvalChebyshevFunction(
            approx_heaviside,
            ct_diff,
            left_bound,
            right_bound,
            degree);
    }
    else if (method == "composition")
    {
        // Iterative composition of lower-degree approximations.
        // Each composition iteration doubles the effective degree while
        // consuming only one additional Chebyshev evaluation's depth.
        //
        // g_0(x) = low_degree_sign(x)
        // g_{k+1}(x) = low_degree_sign(g_k(x))  (mapped to [-1, 1] range)
        //
        // After 'composition_iterations' steps, effective degree ≈ d^k.
        const int iters  = config.composition_iterations;
        const uint32_t deg = static_cast<uint32_t>(config.chebyshev_degree);

        // Simple odd polynomial approximation of sign function
        auto sign_approx = [](double x) -> double
        {
            // Minimax approximation of sign(x) ≈ x*(a - b*x²) for |x| ≤ 1
            double ax = std::abs(x);
            if (ax >= 1.0) return (x >= 0) ? 1.0 : -1.0;
            return x;  // linear for now; Chebyshev handles the approx
        };

        Ciphertext<DCRTPoly> ct_result = cc->EvalChebyshevFunction(
            sign_approx, ct_diff, left_bound, right_bound, deg);

        for (int it = 1; it < iters; ++it)
        {
            ct_result = cc->EvalChebyshevFunction(
                sign_approx, ct_result, -1.0, 1.0, deg);
        }

        // Map from [-1, 1] (sign output) to [0, 1] (comparison output)
        ct_result = cc->EvalAdd(cc->EvalMult(ct_result, 0.5), 0.5);
        return ct_result;
    }
    else
    {
        throw std::invalid_argument("[SignApproximator] Unknown method: " + method);
    }
}

} // namespace anns_fhe
