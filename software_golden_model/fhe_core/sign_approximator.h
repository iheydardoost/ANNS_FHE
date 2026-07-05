#ifndef SIGN_APPROXIMATOR_H
#define SIGN_APPROXIMATOR_H

#include "openfhe.h"
#include <string>

namespace anns_fhe {

class SignApproximator {
public:
    SignApproximator() = default;
    ~SignApproximator() = default;

    // Evaluates the sign function on the input ciphertext homomorphically
    // Returns a ciphertext approximating sign(x) (i.e. close to +1 for x > 0 and -1 for x < 0)
    static lbcrypto::Ciphertext<lbcrypto::DCRTPoly> eval_sign(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_x,
        const std::string& method,
        int iterations_or_degree);
};

} // namespace anns_fhe

#endif // SIGN_APPROXIMATOR_H
