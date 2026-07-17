#include "fhe_context_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <set>
#include <algorithm>

// OpenFHE serialization — must include these AFTER openfhe.h
#include "pke/cryptocontext-ser.h"
#include "pke/key/key-ser.h"
#include "pke/ciphertext-ser.h"
#include "pke/scheme/ckksrns/ckksrns-ser.h"

using namespace lbcrypto;
namespace fs = std::filesystem;

namespace anns_fhe
{

// ---------------------------------------------------------------------------
// Private helper: compute all rotation key indices needed by the algorithm.
// ---------------------------------------------------------------------------
std::vector<int32_t> FHEContextManager::compute_rotation_indices(const FHEConfig& config)
{
    std::set<int32_t> idx;

    const int slots = config.poly_modulus_degree >> 1; // N/2 slots

    // By decomposing any arbitrary rotation rot into its binary representation
    // (rot = sum of +/- (1 << k)), we ONLY need power-of-2 rotation keys.
    // This reduces the required automorphism keys from ~806 down to ~30!
    for (int step = 1; step < slots; step *= 2)
    {
        idx.insert(step);
        idx.insert(-step);
    }

    return std::vector<int32_t>(idx.begin(), idx.end());
}

// ---------------------------------------------------------------------------
// Offline: Initialize CryptoContext and generate all keys
// ---------------------------------------------------------------------------
bool FHEContextManager::init_and_keygen(const FHEConfig& config)
{
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(config.multiplicative_depth);
    parameters.SetScalingModSize(config.scale_bits);
    parameters.SetBatchSize(config.poly_modulus_degree >> 1);  // N/2 slots

    // Security level
    SecurityLevel secLevel = HEStd_128_classic;
    if      (config.security_level == "HEStd_128_classic") secLevel = HEStd_128_classic;
    else if (config.security_level == "HEStd_192_classic") secLevel = HEStd_192_classic;
    else if (config.security_level == "HEStd_256_classic") secLevel = HEStd_256_classic;
    else if (config.security_level == "HEStd_NotSet")      secLevel = HEStd_NotSet;
    parameters.SetSecurityLevel(secLevel);

    if (config.security_level == "HEStd_NotSet")
        parameters.SetRingDim(config.poly_modulus_degree);

    parameters.SetScalingTechnique(FIXEDMANUAL);

    m_cc = GenCryptoContext(parameters);

    m_cc->Enable(PKE);
    m_cc->Enable(KEYSWITCH);
    m_cc->Enable(LEVELEDSHE);
    m_cc->Enable(ADVANCEDSHE);   // Required for EvalChebyshevSeries (HMR)
    // NOTE: FHE (bootstrapping) intentionally NOT enabled.

    std::cout << "[FHEContextManager] CryptoContext initialized (depth="
              << config.multiplicative_depth << ", slots="
              << (config.poly_modulus_degree >> 1) << ")." << std::endl;

    // Key generation
    m_keypair = m_cc->KeyGen();
    m_cc->EvalMultKeyGen(m_keypair.secretKey);

    auto rotIndices = compute_rotation_indices(config);
    std::cout << "[FHEContextManager] Generating " << rotIndices.size()
              << " rotation keys..." << std::endl;
    m_cc->EvalRotateKeyGen(m_keypair.secretKey, rotIndices);

    m_is_initialized = true;
    std::cout << "[FHEContextManager] Key generation complete." << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Offline: Serialize CryptoContext + all keys to disk
// ---------------------------------------------------------------------------
bool FHEContextManager::serialize_all(const FHEConfig& config) const
{
    if (!m_is_initialized)
    {
        std::cerr << "[FHEContextManager] Not initialized — call init_and_keygen() first." << std::endl;
        return false;
    }

    const std::string dir = config.resolve_path(config.serialization_dir);
    fs::create_directories(dir);

    bool ok = true;

    // CryptoContext
    if (!Serial::SerializeToFile(dir + "/cryptocontext.bin", m_cc, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to serialize CryptoContext." << std::endl;
        ok = false;
    }

    // Public Key
    if (!Serial::SerializeToFile(dir + "/publickey.bin", m_keypair.publicKey, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to serialize PublicKey." << std::endl;
        ok = false;
    }

    // Secret Key (client-side; kept for simulation/testing)
    if (!Serial::SerializeToFile(dir + "/secretkey.bin", m_keypair.secretKey, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to serialize SecretKey." << std::endl;
        ok = false;
    }

    // EvalMult (Relinearization) Keys — stream-based API
    {
        std::ofstream fmult(dir + "/evalkeys_mult.bin", std::ios::binary);
        if (!fmult.is_open() ||
            !m_cc->SerializeEvalMultKey(fmult, SerType::BINARY))
        {
            std::cerr << "[FHEContextManager] Failed to serialize EvalMult keys." << std::endl;
            ok = false;
        }
    }

    // EvalRotate (Galois) Keys — stream-based API
    {
        std::ofstream frot(dir + "/evalkeys_rot.bin", std::ios::binary);
        if (!frot.is_open() ||
            !m_cc->SerializeEvalAutomorphismKey(frot, SerType::BINARY))
        {
            std::cerr << "[FHEContextManager] Failed to serialize EvalRotate keys." << std::endl;
            ok = false;
        }
    }

    if (ok)
        std::cout << "[FHEContextManager] All keys serialized to: " << dir << std::endl;
    return ok;
}

// ---------------------------------------------------------------------------
// Offline: Encrypt and serialize IVF centroids
// Layout: [c0_d0..c0_d127, c1_d0..c1_d127, ..., c{N-1}_d0..c{N-1}_d127, 0...]
// This packs n_list=32 centroids × dim=128 = 4096 floats into 1 ciphertext.
// ---------------------------------------------------------------------------
bool FHEContextManager::encrypt_and_serialize_centroids(const FHEConfig& config) const
{
    if (!m_is_initialized)
    {
        std::cerr << "[FHEContextManager] Not initialized." << std::endl;
        return false;
    }

    const std::string centroids_path = config.resolve_path(
        config.models_output_dir + "/ivf_centroids.bin");

    // Load raw float32 centroids
    std::ifstream f(centroids_path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
    {
        std::cerr << "[FHEContextManager] Cannot open: " << centroids_path << std::endl;
        return false;
    }
    size_t bytes = f.tellg();
    f.seekg(0);
    size_t num_floats = bytes / sizeof(float);
    std::vector<float> raw(num_floats);
    f.read(reinterpret_cast<char*>(raw.data()), bytes);

    const size_t expected = static_cast<size_t>(config.n_list) * config.dimension;
    if (num_floats != expected)
    {
        std::cerr << "[FHEContextManager] Centroid size mismatch: got " << num_floats
                  << ", expected " << expected << std::endl;
        return false;
    }

    // Build SIMD slot vector: n_list * dim floats, row-major
    const int slots = config.poly_modulus_degree >> 1;  // N/2 = 8192
    std::vector<double> packed(slots, 0.0);
    for (size_t i = 0; i < num_floats; ++i)
        packed[i] = static_cast<double>(raw[i]);

    Plaintext pt = m_cc->MakeCKKSPackedPlaintext(packed);
    auto ct      = m_cc->Encrypt(m_keypair.publicKey, pt);

    // Serialize
    const std::string out_dir = config.resolve_path(config.serialization_dir);
    fs::create_directories(out_dir);
    const std::string out_path = out_dir + "/encrypted_centroids.bin";

    if (!Serial::SerializeToFile(out_path, ct, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to serialize encrypted centroids." << std::endl;
        return false;
    }

    std::cout << "[FHEContextManager] Encrypted centroids saved → " << out_path
              << "  (" << config.n_list << " centroids × " << config.dimension << " dims)" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Offline: Encrypt and serialize PQ codebooks
// M ciphertexts, one per subvector.
// Layout per ciphertext m: [cb0_d0..cb0_d{sub_dim-1}, cb1_d0..cb1_d{sub_dim-1}, ...,
//                           cb{K-1}_d0..cb{K-1}_d{sub_dim-1}, 0...]
// ---------------------------------------------------------------------------
bool FHEContextManager::encrypt_and_serialize_codebooks(const FHEConfig& config) const
{
    if (!m_is_initialized)
    {
        std::cerr << "[FHEContextManager] Not initialized." << std::endl;
        return false;
    }

    const std::string codebooks_path = config.resolve_path(
        config.models_output_dir + "/pq_codebooks.bin");

    std::ifstream f(codebooks_path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
    {
        std::cerr << "[FHEContextManager] Cannot open: " << codebooks_path << std::endl;
        return false;
    }
    size_t bytes = f.tellg();
    f.seekg(0);
    size_t num_floats = bytes / sizeof(float);
    std::vector<float> raw(num_floats);
    f.read(reinterpret_cast<char*>(raw.data()), bytes);

    const int M       = config.m_subvectors;
    const int K       = config.k_subcentroids;
    const int sub_dim = config.dimension / M;
    const size_t expected = static_cast<size_t>(M) * K * sub_dim;

    if (num_floats != expected)
    {
        std::cerr << "[FHEContextManager] Codebook size mismatch: got " << num_floats
                  << ", expected " << expected << std::endl;
        return false;
    }

    const std::string out_dir = config.resolve_path(config.serialization_dir);
    fs::create_directories(out_dir);
    const int slots = config.poly_modulus_degree >> 1;

    // Serialize to a single binary container: [count(int32)] then M ciphertexts
    // We serialize each individually, indexed by m.
    std::vector<Ciphertext<DCRTPoly>> ct_codebooks(M);

    for (int m = 0; m < M; ++m)
    {
        // Build slot vector: K * sub_dim floats for subspace m
        std::vector<double> packed(slots, 0.0);
        const size_t offset = static_cast<size_t>(m) * K * sub_dim;
        for (int k = 0; k < K; ++k)
            for (int d = 0; d < sub_dim; ++d)
                packed[static_cast<size_t>(k) * sub_dim + d] =
                    static_cast<double>(raw[offset + k * sub_dim + d]);

        Plaintext pt  = m_cc->MakeCKKSPackedPlaintext(packed);
        ct_codebooks[m] = m_cc->Encrypt(m_keypair.publicKey, pt);
    }

    // Serialize the entire vector to one file
    const std::string out_path = out_dir + "/encrypted_codebooks.bin";
    if (!Serial::SerializeToFile(out_path, ct_codebooks, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to serialize encrypted codebooks." << std::endl;
        return false;
    }

    std::cout << "[FHEContextManager] Encrypted codebooks saved → " << out_path
              << "  (" << M << " ciphertexts, K=" << K << ", sub_dim=" << sub_dim << ")" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Online: Deserialize everything from disk
// ---------------------------------------------------------------------------
bool FHEContextManager::load_from_disk(const FHEConfig& config)
{
    const std::string dir = config.resolve_path(config.serialization_dir);

    // 1. CryptoContext
    if (!Serial::DeserializeFromFile(dir + "/cryptocontext.bin", m_cc, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to load CryptoContext from: "
                  << dir + "/cryptocontext.bin" << std::endl;
        return false;
    }

    // 2. Public Key
    if (!Serial::DeserializeFromFile(dir + "/publickey.bin", m_keypair.publicKey, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to load PublicKey." << std::endl;
        return false;
    }

    // 3. Secret Key (needed for client-side decrypt simulation)
    if (!Serial::DeserializeFromFile(dir + "/secretkey.bin", m_keypair.secretKey, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to load SecretKey." << std::endl;
        return false;
    }

    // 4. EvalMult Keys — stream-based API
    {
        std::ifstream fmult(dir + "/evalkeys_mult.bin", std::ios::binary);
        if (!fmult.is_open() ||
            !m_cc->DeserializeEvalMultKey(fmult, SerType::BINARY))
        {
            std::cerr << "[FHEContextManager] Failed to load EvalMult keys." << std::endl;
            return false;
        }
    }

    // 5. EvalRotate Keys — stream-based API
    {
        std::ifstream frot(dir + "/evalkeys_rot.bin", std::ios::binary);
        if (!frot.is_open() ||
            !m_cc->DeserializeEvalAutomorphismKey(frot, SerType::BINARY))
        {
            std::cerr << "[FHEContextManager] Failed to load EvalRotate keys." << std::endl;
            return false;
        }
    }

    std::cout << "[FHEContextManager] CryptoContext + keys loaded from: " << dir << std::endl;

    // 6. Encrypted centroids
    if (!Serial::DeserializeFromFile(
            dir + "/encrypted_centroids.bin", m_encrypted_centroids, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to load encrypted centroids." << std::endl;
        return false;
    }

    // 7. Encrypted codebooks
    if (!Serial::DeserializeFromFile(
            dir + "/encrypted_codebooks.bin", m_encrypted_codebooks, SerType::BINARY))
    {
        std::cerr << "[FHEContextManager] Failed to load encrypted codebooks." << std::endl;
        return false;
    }

    std::cout << "[FHEContextManager] Encrypted index loaded ("
              << m_encrypted_codebooks.size() << " codebook ciphertexts)." << std::endl;

    m_is_initialized = true;
    return true;
}

// ---------------------------------------------------------------------------
// Client-side: Encrypt query — replicated layout matching centroid packing
// Layout: [q_d0..q_d{D-1}, q_d0..q_d{D-1}, ...] × n_list copies
// ---------------------------------------------------------------------------
Ciphertext<DCRTPoly> FHEContextManager::encrypt_query_packed(
    const std::vector<float>& query,
    const FHEConfig& config) const
{
    const int slots = config.poly_modulus_degree >> 1;
    const int dim   = config.dimension;
    const int n     = config.n_list;

    std::vector<double> packed(slots, 0.0);
    for (int rep = 0; rep < n && rep * dim < slots; ++rep)
        for (int d = 0; d < dim && rep * dim + d < slots; ++d)
            packed[rep * dim + d] = static_cast<double>(query[d]);

    Plaintext pt = m_cc->MakeCKKSPackedPlaintext(packed);
    return m_cc->Encrypt(m_keypair.publicKey, pt);
}

// ---------------------------------------------------------------------------
// Client-side: Decrypt first 'limit' slots
// ---------------------------------------------------------------------------
std::vector<double> FHEContextManager::decrypt_vector(
    const Ciphertext<DCRTPoly>& ct, int limit) const
{
    Plaintext pt;
    m_cc->Decrypt(m_keypair.secretKey, ct, &pt);
    pt->SetLength(limit);
    const auto& packed = pt->GetRealPackedValue();
    std::vector<double> result(limit, 0.0);
    for (int i = 0; i < limit && i < static_cast<int>(packed.size()); ++i)
        result[i] = packed[i];
    return result;
}

} // namespace anns_fhe
