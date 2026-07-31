#include "sign_approximator.h"
#include <stdexcept>
#include <iostream>
#include <cmath>

using namespace lbcrypto;

namespace anns_fhe
{

int SignApproximator::depth_for_degree(int degree)
{
    if (degree <=   2) return 3;
    if (degree <=   5) return 4;
    if (degree <=  13) return 5;
    if (degree <=  27) return 6;
    if (degree <=  59) return 7;
    if (degree <= 119) return 8;
    if (degree <= 247) return 9;
    return 10;
}

Ciphertext<DCRTPoly> SignApproximator::eval_sign(
    const CryptoContext<DCRTPoly>& cc,
    const Ciphertext<DCRTPoly>& ct_diff,
    double left_bound,
    double right_bound,
    uint32_t degree,
    const double error)
{
    auto approx_heaviside = [error](double x) -> double 
        { 
            if      (x > error)   return 1;
            else if (x >= -error) return 0.5;
            else                  return 0;
        };

    return cc->EvalChebyshevFunction(
        approx_heaviside,
        ct_diff,
        left_bound,
        right_bound,
        degree);
}

Ciphertext<DCRTPoly> SignApproximator::eval_indicator(
    const CryptoContext<DCRTPoly>& cc,
    const Ciphertext<DCRTPoly>& ct_input,
    double threshold_min,
    double threshold_max,
    double left_bound,
    double right_bound,
    uint32_t degree)
{
    auto indicator_fn = [threshold_min, threshold_max](double x) -> double
    {
        if (x < threshold_min || x > threshold_max) return 0.0;
        else return 1.0;
    };

    return cc->EvalChebyshevFunction(
        indicator_fn,
        ct_input,
        left_bound,
        right_bound,
        degree);
}

} // namespace anns_fhe
