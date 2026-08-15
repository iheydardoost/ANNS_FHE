#define _USE_MATH_DEFINES
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <random>

#define _USE_MATH_DEFINES
#include <cmath>
#include "openfhe.h"

using namespace lbcrypto;

struct TVHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t N;
    uint32_t num_limbs;
    uint32_t num_vectors;
    uint32_t word_width;
    uint64_t reserved;
};

void write_header(std::ofstream& out, uint32_t N, uint32_t num_limbs, uint32_t num_vectors) {
    TVHeader hdr;
    hdr.magic = 0x46484554; // "FHET"
    hdr.version = 1;
    hdr.N = N;
    hdr.num_limbs = num_limbs;
    hdr.num_vectors = num_vectors;
    hdr.word_width = 64;
    hdr.reserved = 0;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(TVHeader));
}

uint32_t ReverseBits(uint32_t x, int numBits) {
    uint32_t result = 0;
    for (int i = 0; i < numBits; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

void DumpDCRTPoly(const DCRTPoly& poly, const std::string& filename) {
    auto params = poly.GetParams()->GetParams();
    size_t num_limbs = params.size();
    size_t N = params[0]->GetRingDimension();
    std::vector<uint64_t> data(num_limbs * N);
    for(size_t i=0; i<num_limbs; i++) {
        auto &elem = poly.GetElementAtIndex(i);
        for(size_t j=0; j<N; j++) {
            data[i * N + j] = elem[j].ConvertToInt();
        }
    }
    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint64_t));
    out.close();
}

// Extended Euclidean Algorithm for modular inverse
uint64_t mod_inverse(uint64_t a, uint64_t m) {
    int64_t m0 = m, t, q;
    int64_t x0 = 0, x1 = 1;
    if (m == 1) return 0;
    int64_t a_signed = a;
    while (a_signed > 1) {
        q = a_signed / m0;
        t = m0;
        m0 = a_signed % m0, a_signed = t;
        t = x0;
        x0 = x1 - q * x0;
        x1 = t;
    }
    if (x1 < 0) x1 += m;
    return x1;
}

int main() {
    std::cout << "Generating test vectors for HLS..." << std::endl;

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(10);
    parameters.SetScalingModSize(45);
    parameters.SetScalingTechnique(FIXEDMANUAL);
    parameters.SetFirstModSize(60);
    parameters.SetRingDim(16384);
    parameters.SetSecurityLevel(HEStd_NotSet);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);

    auto cryptoParams = cc->GetCryptoParameters();
    auto elemParams = cryptoParams->GetElementParams();
    auto params = elemParams->GetParams();
    
    uint32_t num_limbs = params.size();
    uint32_t N = elemParams->GetRingDimension();
    
    std::cout << "N = " << N << ", Limbs = " << num_limbs << std::endl;

    // 1. Extract RNS Primes
    auto cryptoParamsBase = cc->GetCryptoParameters();
    auto cryptoParamsRNS = std::dynamic_pointer_cast<CryptoParametersRNS>(cryptoParamsBase);
    auto paramsP = cryptoParamsRNS->GetParamsP()->GetParams();
    
    // Combine paramsQ and paramsP for extraction
    std::vector<std::shared_ptr<ILNativeParams>> all_params;
    for (size_t i = 0; i < num_limbs; i++) all_params.push_back(params[i]);
    for (size_t i = 0; i < paramsP.size(); i++) all_params.push_back(paramsP[i]);
    
    size_t total_limbs = all_params.size();

    std::vector<uint64_t> primes(total_limbs);
    std::vector<uint64_t> n_inv(total_limbs);

    // Barrett constants
    // m is up to 128-bit, so we'll save it as 16 bytes, and k as 4 bytes
    std::ofstream out_barrett("test_vectors/barrett_constants.bin", std::ios::binary);

    for (size_t i = 0; i < total_limbs; i++) {
        uint64_t q = all_params[i]->GetModulus().ConvertToInt();
        primes[i] = q;
        
        // Compute Barrett constant: m = floor(2^k / q), k = 2*msb + 3
        // OpenFHE's ComputeMu() returns NativeIntegerT (64-bit, truncated).
        // Our HLS mod_mul uses single-shift Barrett and needs the full 128-bit m.
        uint32_t msb = 64 - __builtin_clzll(q); // number of bits in q
        uint32_t k = 2 * msb + 3;
        __uint128_t m = ((__uint128_t)1 << k) / (__uint128_t)q;
        
        out_barrett.write(reinterpret_cast<const char*>(&m), 16);
        out_barrett.write(reinterpret_cast<const char*>(&k), 4);
        
        // N inverse mod q
        n_inv[i] = mod_inverse(N, q);
    }
    out_barrett.close();
    
    std::ofstream out_primes("test_vectors/rns_primes.bin", std::ios::binary);
    out_primes.write(reinterpret_cast<const char*>(primes.data()), total_limbs * sizeof(uint64_t));
    out_primes.close();
    
    std::ofstream out_ninv("test_vectors/n_inv.bin", std::ios::binary);
    out_ninv.write(reinterpret_cast<const char*>(n_inv.data()), total_limbs * sizeof(uint64_t));
    out_ninv.close();

    // 2. Extract Twiddles
    std::vector<uint64_t> twiddles(total_limbs * N);
    for (size_t i = 0; i < total_limbs; i++) {
        uint64_t q = primes[i];
        NativeInteger root = all_params[i]->GetRootOfUnity();
        for (size_t j = 0; j < N; j++) {
            uint32_t br = ReverseBits(j, 14); // log2(16384)=14
            twiddles[i * N + j] = root.ModExp(br, q).ConvertToInt();
        }
    }
    std::ofstream out_tw("test_vectors/twiddles.bin", std::ios::binary);
    out_tw.write(reinterpret_cast<const char*>(twiddles.data()), twiddles.size() * sizeof(uint64_t));
    out_tw.close();

    std::vector<uint64_t> inv_twiddles(total_limbs * N);
    for(size_t i=0; i<total_limbs; i++) {
        uint64_t q = primes[i];
        NativeInteger root = all_params[i]->GetRootOfUnity();
        NativeInteger root_inv = root.ModInverse(q);
        for(size_t j=0; j<N; j++) {
            uint32_t brIdx = ReverseBits(j, 14); // ringDim = N, msb = 14
            inv_twiddles[i * N + j] = root_inv.ModExp(brIdx, q).ConvertToInt();
        }
    }
    std::ofstream out_inv_tw("test_vectors/inv_twiddles.bin", std::ios::binary);
    out_inv_tw.write(reinterpret_cast<const char*>(inv_twiddles.data()), inv_twiddles.size() * sizeof(uint64_t));
    out_inv_tw.close();

    // 3. Generate NTT Test Vectors
    std::vector<uint64_t> ntt_input(num_limbs * N);
    std::vector<uint64_t> ntt_output(num_limbs * N);
    std::uniform_int_distribution<uint64_t> dist(0, -1);
    std::mt19937_64 gen(42);

    DCRTPoly poly_ntt(elemParams, Format::COEFFICIENT, true);
    for(size_t i=0; i<num_limbs; i++) {
        uint64_t q = primes[i];
        NativeVector vec(N, q);
        for(size_t j=0; j<N; j++) {
            uint64_t val = dist(gen) % q;
            vec[j] = NativeInteger(val);
            ntt_input[i * N + j] = val;
        }
        NativePoly p(params[i], Format::COEFFICIENT, true);
        p.SetValues(std::move(vec), Format::COEFFICIENT);
        poly_ntt.SetElementAtIndex(i, std::move(p));
    }
    
    poly_ntt.SwitchFormat(); // performs NTT
    
    for(size_t i=0; i<num_limbs; i++) {
        auto &elem = poly_ntt.GetElementAtIndex(i);
        for(size_t j=0; j<N; j++) {
            ntt_output[i * N + j] = elem[j].ConvertToInt();
        }
    }
    
    std::ofstream out_ntt_in("test_vectors/tv_ntt_in.bin", std::ios::binary);
    out_ntt_in.write(reinterpret_cast<const char*>(ntt_input.data()), ntt_input.size() * sizeof(uint64_t));
    out_ntt_in.close();
    
    std::ofstream out_ntt_out("test_vectors/tv_ntt_out.bin", std::ios::binary);
    out_ntt_out.write(reinterpret_cast<const char*>(ntt_output.data()), ntt_output.size() * sizeof(uint64_t));
    out_ntt_out.close();

    // 4. Generate Modular Arithmetic test vectors
    std::vector<uint64_t> mod_arith_vectors;
    // format: a, b, add, sub, mul
    for (size_t i = 0; i < num_limbs; i++) {
        uint64_t q = primes[i];
        for (size_t j = 0; j < 1000; j++) {
            uint64_t a = dist(gen) % q;
            uint64_t b = dist(gen) % q;
            uint64_t add = (a + b) % q;
            uint64_t sub = (a >= b) ? (a - b) : (a + q - b);
            uint64_t mul = ((__uint128_t)a * b) % q;
            mod_arith_vectors.push_back(a);
            mod_arith_vectors.push_back(b);
            mod_arith_vectors.push_back(add);
            mod_arith_vectors.push_back(sub);
            mod_arith_vectors.push_back(mul);
        }
    }
    std::ofstream out_mod_arith("test_vectors/tv_mod_arith.bin", std::ios::binary);
    out_mod_arith.write(reinterpret_cast<const char*>(mod_arith_vectors.data()), mod_arith_vectors.size() * sizeof(uint64_t));
    out_mod_arith.close();

    // 5. Generate Polynomial Arithmetic test vectors
    DCRTPoly poly_b(elemParams, Format::COEFFICIENT, true);
    for(size_t i=0; i<num_limbs; i++) {
        uint64_t q = primes[i];
        NativeVector vec(N, q);
        for(size_t j=0; j<N; j++) {
            vec[j] = NativeInteger(dist(gen) % q);
        }
        NativePoly p(params[i], Format::COEFFICIENT, true);
        p.SetValues(std::move(vec), Format::COEFFICIENT);
        poly_b.SetElementAtIndex(i, std::move(p));
    }
    poly_b.SwitchFormat();
    
    DCRTPoly poly_add = poly_ntt + poly_b;
    DCRTPoly poly_sub = poly_ntt - poly_b;
    DCRTPoly poly_mul = poly_ntt * poly_b;
    
    DumpDCRTPoly(poly_ntt, "test_vectors/tv_poly_a.bin");
    DumpDCRTPoly(poly_b, "test_vectors/tv_poly_b.bin");
    DumpDCRTPoly(poly_add, "test_vectors/tv_poly_add.bin");
    DumpDCRTPoly(poly_sub, "test_vectors/tv_poly_sub.bin");
    DumpDCRTPoly(poly_mul, "test_vectors/tv_poly_mul.bin");

    // 6. Generate Automorphism test vectors
    uint32_t auto_idx = 5;
    DCRTPoly poly_auto = poly_ntt.AutomorphismTransform(auto_idx);
    DumpDCRTPoly(poly_auto, "test_vectors/tv_poly_auto.bin");
    
    // Generate the permutation map
    DCRTPoly poly_map = poly_ntt.Clone();
    for (size_t l = 0; l < num_limbs; l++) {
        auto vec = poly_map.GetElementAtIndex(l).GetValues();
        for (size_t i = 0; i < N; i++) {
            vec[i] = NativeInteger(i);
        }
        NativePoly p(params[l], Format::EVALUATION, true);
        p.SetValues(std::move(vec), Format::EVALUATION);
        poly_map.SetElementAtIndex(l, std::move(p));
    }
    DCRTPoly poly_map_out = poly_map.AutomorphismTransform(auto_idx);
    
    std::vector<uint32_t> auto_map(N);
    auto map_vec = poly_map_out.GetElementAtIndex(0).GetValues();
    for (size_t i = 0; i < N; i++) {
        auto_map[i] = map_vec[i].ConvertToInt();
    }
    
    std::ofstream out_map("test_vectors/auto_map.bin", std::ios::binary);
    out_map.write(reinterpret_cast<const char*>(auto_map.data()), N * sizeof(uint32_t));
    out_map.close();
    
    // 7. Generate Rescale test vectors
    // To generate mathematically correct rescale, we need the CryptoContext.
    // Instead of doing it directly on DCRTPoly, we can create a ciphertext and rescale it.
    std::vector<double> x1 = {1.5, 2.5, 3.5, 4.5};
    Plaintext ptx1 = cc->MakeCKKSPackedPlaintext(x1);
    auto keys = cc->KeyGen();
    cc->EvalMultKeyGen(keys.secretKey);
    auto ct1 = cc->Encrypt(keys.publicKey, ptx1);
    
    // Evaluate a multiplication to increase depth so Rescale works properly
    auto ct_mul = cc->EvalMult(ct1, ct1);
    
    // Extract constants
    auto cryptoParamsCKKS = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(cc->GetCryptoParameters());
    uint32_t level = ct_mul->GetElements()[0].GetNumOfElements() - 1; // last level to be dropped
    uint32_t diffQl = num_limbs - ct_mul->GetElements()[0].GetNumOfElements();
    
    // In OpenFHE, the index corresponds to the number of levels already dropped.
    // Since this is the first level we drop, the index is 0.
    auto QlQlInv = cryptoParamsCKKS->GetQlQlInvModqlDivqlModq(diffQl);
    auto qlInv = cryptoParamsCKKS->GetqlInvModq(diffQl);
    
    std::vector<uint64_t> qlql_inv_vec;
    std::vector<uint64_t> qlinv_vec;
    for (size_t i = 0; i < level; i++) {
        qlql_inv_vec.push_back(QlQlInv[i].ConvertToInt());
        qlinv_vec.push_back(qlInv[i].ConvertToInt());
    }
    
    std::ofstream out_qlql("test_vectors/QlQlInvModqlDivqlModq.bin", std::ios::binary);
    out_qlql.write(reinterpret_cast<const char*>(qlql_inv_vec.data()), qlql_inv_vec.size() * sizeof(uint64_t));
    out_qlql.close();
    
    std::ofstream out_qlinv("test_vectors/qlInvModq.bin", std::ios::binary);
    out_qlinv.write(reinterpret_cast<const char*>(qlinv_vec.data()), qlinv_vec.size() * sizeof(uint64_t));
    out_qlinv.close();
    
    auto ct_rescale = cc->Rescale(ct_mul);
    
    std::cout << "ct_mul levels: " << ct_mul->GetElements()[0].GetNumOfElements() << std::endl;
    std::cout << "ct_rescale levels: " << ct_rescale->GetElements()[0].GetNumOfElements() << std::endl;
    
    auto poly_mul_ct = ct_mul->GetElements()[0];
    auto poly_rescale_ct = ct_rescale->GetElements()[0];
    
    DumpDCRTPoly(poly_mul_ct, "test_vectors/tv_rescale_in.bin");
    DumpDCRTPoly(poly_rescale_ct, "test_vectors/tv_rescale_out.bin");
    
    auto lastPoly = poly_mul_ct.GetElementAtIndex(level);
    std::cout << "LEVEL: " << level << std::endl;
    std::cout << "lastPoly modulus: " << lastPoly.GetModulus() << std::endl;
    std::cout << "lastPoly root of unity: " << lastPoly.GetRootOfUnity() << std::endl;
    std::cout << "params modulus: " << cryptoParamsCKKS->GetElementParams()->GetParams()[level]->GetModulus() << std::endl;
    std::cout << "params root of unity: " << cryptoParamsCKKS->GetElementParams()->GetParams()[level]->GetRootOfUnity() << std::endl;
    std::ofstream out_lastin("test_vectors/tv_last_eval.bin", std::ios::binary);
    for(size_t j=0; j<N; j++) {
        uint64_t val = lastPoly.GetValues()[j].ConvertToInt();
        out_lastin.write(reinterpret_cast<const char*>(&val), sizeof(uint64_t));
    }
    out_lastin.close();
    
    lastPoly.SetFormat(Format::COEFFICIENT);
    std::ofstream out_lastout("test_vectors/tv_last_coeff.bin", std::ios::binary);
    for(size_t j=0; j<N; j++) {
        uint64_t val = lastPoly.GetValues()[j].ConvertToInt();
        out_lastout.write(reinterpret_cast<const char*>(&val), sizeof(uint64_t));
    }
    out_lastout.close();
    
    // 8. Generate Key Switch test vectors (using CryptoContext)
    // We already have a cc
    auto keyPair = cc->KeyGen();
    auto evalKey = cc->KeySwitchGen(keyPair.secretKey, keyPair.secretKey);
    
    uint32_t numPartQ = cryptoParamsRNS->GetNumberOfQPartitions();
    uint32_t alpha = cryptoParamsRNS->GetNumPerPartQ();
    uint32_t sizeP = paramsP.size();
    
    std::cout << "KeySwitch Params:" << std::endl;
    std::cout << "  numPartQ (max digits): " << numPartQ << std::endl;
    std::cout << "  alpha (limbs per digit): " << alpha << std::endl;
    std::cout << "  sizeP (number of P primes): " << sizeP << std::endl;
    
    // Dump P primes
    std::vector<uint64_t> p_primes;
    std::vector<uint64_t> p_roots;
    for (size_t i = 0; i < sizeP; i++) {
        p_primes.push_back(paramsP[i]->GetModulus().ConvertToInt());
        p_roots.push_back(paramsP[i]->GetRootOfUnity().ConvertToInt());
    }
    std::ofstream out_p("test_vectors/rns_primes_P.bin", std::ios::binary);
    out_p.write(reinterpret_cast<const char*>(p_primes.data()), sizeP * sizeof(uint64_t));
    out_p.close();
    std::ofstream out_proots("test_vectors/rns_roots_P.bin", std::ios::binary);
    out_proots.write(reinterpret_cast<const char*>(p_roots.data()), sizeP * sizeof(uint64_t));
    out_proots.close();
    
    // Dump roots of unity for Q primes
    std::vector<uint64_t> q_roots;
    auto paramsQ = cryptoParamsRNS->GetElementParams()->GetParams();
    for (uint32_t i = 0; i < paramsQ.size(); i++) {
        q_roots.push_back(paramsQ[i]->GetRootOfUnity().ConvertToInt());
    }
    std::ofstream out_qroots("test_vectors/rns_roots.bin", std::ios::binary);
    out_qroots.write(reinterpret_cast<const char*>(q_roots.data()), paramsQ.size() * sizeof(uint64_t));
    out_qroots.close();
    
    // 8.1 Extract EvalKey (AVector, BVector)
    const auto& av = evalKey->GetAVector();
    const auto& bv = evalKey->GetBVector();
    for (uint32_t j = 0; j < av.size(); ++j) {
        std::string a_name = "test_vectors/tv_evalkey_a_" + std::to_string(j) + ".bin";
        std::string b_name = "test_vectors/tv_evalkey_b_" + std::to_string(j) + ".bin";
        DumpDCRTPoly(av[j], a_name);
        DumpDCRTPoly(bv[j], b_name);
    }
    
    // 8.2 Extract KeySwitchExt input and output
    auto c_in = ct_rescale->GetElements()[0]; // Use the first element of rescale output
    uint32_t sizeQl = c_in.GetNumOfElements();
    DumpDCRTPoly(c_in, "test_vectors/tv_keyswitch_in.bin");
    
    auto scheme = cc->GetScheme();
    auto digits = scheme->EvalKeySwitchPrecomputeCore(c_in, cryptoParamsBase);
    

    for (uint32_t j = 0; j < digits->size(); ++j) {
        std::string d_name = "test_vectors/tv_keyswitch_digit_" + std::to_string(j) + ".bin";
        DumpDCRTPoly((*digits)[j], d_name);
        
        // Wait, to dump partsCt and partsCtCompl, we can just do the EXACT SAME logic as OpenFHE!
        uint32_t sizePartQl = (j == digits->size() - 1) ? (sizeQl - alpha * j) : alpha;
        
        auto paramsPartQ = cryptoParamsRNS->GetParamsPartQ(j);
        lbcrypto::DCRTPoly partsCt(paramsPartQ, Format::EVALUATION, true);
        if (partsCt.GetNumOfElements() > sizePartQl) {
            partsCt.DropLastElements(partsCt.GetNumOfElements() - sizePartQl);
        }
        const uint32_t startPartIdx = alpha * j;
        for (uint32_t i = 0, idx = startPartIdx; i < sizePartQl; ++i, ++idx) {
            partsCt.SetElementAtIndex(i, c_in.GetElementAtIndex(idx));
        }
        partsCt.SetFormat(Format::COEFFICIENT);
        
        // DUMP partsCt (the coefficients of the alpha limbs)
        std::string name_partsCt = "test_vectors/tv_keyswitch_partsCt_" + std::to_string(j) + ".bin";
        DumpDCRTPoly(partsCt, name_partsCt);
        
        auto partsCtCompl = partsCt.ApproxSwitchCRTBasis(cryptoParamsRNS->GetParamsPartQ(j),
                                                         cryptoParamsRNS->GetParamsComplPartQ(sizeQl - 1, j),
                                                         cryptoParamsRNS->GetPartQlHatInvModq(j, sizePartQl - 1),
                                                         cryptoParamsRNS->GetPartQlHatInvModqPrecon(j, sizePartQl - 1),
                                                         cryptoParamsRNS->GetPartQlHatModp(sizeQl - 1, j),
                                                         cryptoParamsRNS->GetmodComplPartqBarrettMu(sizeQl - 1, j));
                                                         
        // DUMP partsCtCompl (the output limbs in coefficient format)
        std::string name_partsCtCompl = "test_vectors/tv_keyswitch_partsCtCompl_" + std::to_string(j) + ".bin";
        DumpDCRTPoly(partsCtCompl, name_partsCtCompl);

        // Dump the tables for FastBasesConv
        auto partQlHatInvModq = cryptoParamsRNS->GetPartQlHatInvModq(j, sizePartQl - 1);
        std::vector<uint64_t> vec_invmodq;
        for (const auto& val : partQlHatInvModq) vec_invmodq.push_back(val.ConvertToInt());
        std::ofstream out_invmodq("test_vectors/tv_keyswitch_PartQlHatInvModq_" + std::to_string(j) + ".bin", std::ios::binary);
        out_invmodq.write(reinterpret_cast<const char*>(vec_invmodq.data()), vec_invmodq.size() * sizeof(uint64_t));
        out_invmodq.close();
        
        auto partQlHatModp = cryptoParamsRNS->GetPartQlHatModp(sizeQl - 1, j);
        std::vector<uint64_t> vec_modp;
        for (const auto& row : partQlHatModp) {
            for (const auto& val : row) vec_modp.push_back(val.ConvertToInt());
        }
        std::ofstream out_modp("test_vectors/tv_keyswitch_PartQlHatModp_" + std::to_string(j) + ".bin", std::ios::binary);
        out_modp.write(reinterpret_cast<const char*>(vec_modp.data()), vec_modp.size() * sizeof(uint64_t));
        out_modp.close();
    }
    
    auto paramsQl = c_in.GetParams();
    auto ext_res = scheme->EvalFastKeySwitchCoreExt(digits, evalKey, paramsQl);
    DumpDCRTPoly((*ext_res)[0], "test_vectors/tv_keyswitch_ext_res0.bin");
    DumpDCRTPoly((*ext_res)[1], "test_vectors/tv_keyswitch_ext_res1.bin");
    
    // Dump tables for ApproxModDown
    auto PInvModq = cryptoParamsRNS->GetPInvModq();
    std::vector<uint64_t> vec_pinvmodq;
    for (size_t i = 0; i < sizeQl; ++i) vec_pinvmodq.push_back(PInvModq[i].ConvertToInt());
    std::ofstream out_pinvmodq("test_vectors/tv_keyswitch_PInvModq.bin", std::ios::binary);
    out_pinvmodq.write(reinterpret_cast<const char*>(vec_pinvmodq.data()), vec_pinvmodq.size() * sizeof(uint64_t));
    out_pinvmodq.close();
    
    auto PHatInvModp = cryptoParamsRNS->GetPHatInvModp();
    std::vector<uint64_t> vec_phatinvmodp;
    for (size_t i = 0; i < sizeP; ++i) vec_phatinvmodp.push_back(PHatInvModp[i].ConvertToInt());
    std::ofstream out_phatinvmodp("test_vectors/tv_keyswitch_PHatInvModp.bin", std::ios::binary);
    out_phatinvmodp.write(reinterpret_cast<const char*>(vec_phatinvmodp.data()), vec_phatinvmodp.size() * sizeof(uint64_t));
    out_phatinvmodp.close();
    
    auto PHatModq = cryptoParamsRNS->GetPHatModq();
    std::vector<uint64_t> vec_phatmodq;
    for (size_t j = 0; j < sizeP; ++j) {
        for (size_t i = 0; i < sizeQl; ++i) vec_phatmodq.push_back(PHatModq[j][i].ConvertToInt());
    }
    std::ofstream out_phatmodq("test_vectors/tv_keyswitch_PHatModq.bin", std::ios::binary);
    out_phatmodq.write(reinterpret_cast<const char*>(vec_phatmodq.data()), vec_phatmodq.size() * sizeof(uint64_t));
    out_phatmodq.close();
    
    auto final_res = scheme->EvalFastKeySwitchCore(digits, evalKey, paramsQl);
    DumpDCRTPoly((*final_res)[0], "test_vectors/tv_keyswitch_final_res0.bin");
    DumpDCRTPoly((*final_res)[1], "test_vectors/tv_keyswitch_final_res1.bin");
    
    std::cout << "Test vectors generation complete for all requested types." << std::endl;

    
    std::cout << "Initial extraction complete." << std::endl;
    return 0;
}
