#include "sign_approximator.h"
#include <iostream>
#include <vector>
#include <cmath>

using namespace lbcrypto;

namespace anns_fhe
{

    Ciphertext<DCRTPoly> SignApproximator::eval_sign(
        CryptoContext<DCRTPoly> cc,
        const Ciphertext<DCRTPoly>& ct_x,
        const std::string& method,
        int iterations_or_degree)
    {

        if(method == "composition")
        {
            // g(x) = 1.5*x - 0.5*x^3
            // Coefficients are in increasing power order: [0.0, 1.5, 0.0, -0.5]
            std::vector<double> coeffs = { 0.0, 1.5, 0.0, -0.5 };

            auto temp = ct_x;
            for(int i = 0; i < iterations_or_degree; ++i)
            {
                temp = cc->EvalPoly(temp, coeffs);
            }
            return temp;
        }
        else if(method == "chebyshev")
        {
            // We approximate the sign function over the interval [-1.0, 1.0]
            // using Chebyshev polynomial of the specified degree.
            auto sign_func = [](double x) -> double {
                if(x < -0.05) return -1.0;
                if(x > 0.05) return 1.0;
                return 20.0 * x; // Smooth line between -0.05 and 0.05 to avoid high Gibbs oscillation
                };

            // lowerBound = -1.0, upperBound = 1.0
            uint32_t degree = static_cast<uint32_t>(iterations_or_degree);
            if(degree == 0) degree = 15; // sensible default

            return cc->EvalChebyshevFunction(sign_func, ct_x, -1.0, 1.0, degree);
        }
        else
        {
            std::cerr << "Warning: Unknown sign approximation method '" << method
                << "', falling back to 3-step composition." << std::endl;
            return eval_sign(cc, ct_x, "composition", 3);
        }
    }

} // namespace anns_fhe
