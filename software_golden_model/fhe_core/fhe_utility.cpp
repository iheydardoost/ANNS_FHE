#include "fhe_utility.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <cassert>
#include "utils-ptxt.h"

using namespace lbcrypto;

namespace anns_fhe
{

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> mask_row(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len,
    const size_t row_index)
{
    // Suppose row_len is power of 2
    assert(row_index < row_len && "Invalid row index");
    auto cc = c->GetCryptoContext();
    size_t slots = cc->GetRingDimension() >> 1;

    std::vector<double> mask(slots, 0.0);
    size_t start_idx = row_index * row_len;
    for (size_t col = 0; col < row_len && (start_idx + col) < slots; col++) {
        mask[start_idx + col] = 1.0;
    }

    auto pt_mask = cc->MakeCKKSPackedPlaintext(mask, 1, c->GetLevel());
    return cc->EvalMult(c, pt_mask);
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> mask_column(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len,
    const size_t column_index)
{
    // Suppose row_len is power of 2
    assert(column_index < row_len && "Invalid column index");
    auto cc = c->GetCryptoContext();
    size_t slots = cc->GetRingDimension() >> 1;

    std::vector<double> mask(slots, 0.0);
    for (size_t r = 0; r < row_len; r++) {
        size_t idx = r * row_len + column_index;
        if (idx < slots) {
            mask[idx] = 1.0;
        }
    }

    auto pt_mask = cc->MakeCKKSPackedPlaintext(mask, 1, c->GetLevel());
    return cc->EvalMult(c, pt_mask);
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> replicate_row(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len)
{
    // Suppose row_len is power of 2
    auto cc = c->GetCryptoContext();
    
    // Suppose ONLY Row 0 is non-zero
    for (size_t i = 0; i < LOG2(row_len); i++) {
        int32_t shift = static_cast<int32_t>(row_len * (1 << i));
        // Negative shift moves values from top rows into lower rows (slot x -> slot x + shift)
        auto c_rot = cc->EvalRotate(c, -shift);
        c = cc->EvalAdd(c, c_rot);
    }

    return c;
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> replicate_column(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len)
{
    // Suppose row_len is power of 2
    auto cc = c->GetCryptoContext();
    
    // Suppose ONLY Column 0 is non-zero
    for (size_t i = 0; i < LOG2(row_len); i++) {
        int32_t shift = static_cast<int32_t>(1 << i);
        // Negative shift moves values rightward within rows (slot x -> slot x + shift)
        auto c_rot = cc->EvalRotate(c, -shift);
        c = cc->EvalAdd(c, c_rot);
    }

    return c;
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sum_rows(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len,
    bool mask_output,
    const size_t output_row)
{
    // Suppose row_len is power of 2
    auto cc = c->GetCryptoContext();

    // Sum rows vertically into Row 0
    for (size_t i = 0; i < LOG2(row_len); i++) {
        int32_t shift = static_cast<int32_t>(row_len * (1 << i));
        // Positive shift moves lower row elements UP into top rows
        auto c_rot = cc->EvalRotate(c, shift);
        c = cc->EvalAdd(c, c_rot);
    }

    if (mask_output) {
        c = mask_row(c, row_len, output_row);
    }

    return c;
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sum_columns( 
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len,
    bool mask_output,
    const size_t output_column)
{
    // Suppose row_len is power of 2
    auto cc = c->GetCryptoContext();

    // Sum columns horizontally into Column 0
    for (size_t i = 0; i < LOG2(row_len); i++) {
        int32_t shift = static_cast<int32_t>(1 << i);
        // Positive shift moves lower row elements UP into top rows
        auto c_rot = cc->EvalRotate(c, shift);
        c = cc->EvalAdd(c, c_rot);
    }

    if (mask_output) {
        c = mask_column(c, row_len, output_column);
    }

    return c;
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> transpose_row(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len,
    bool mask_output)
{
    // Suppose row_len is power of 2
    auto cc = c->GetCryptoContext();

    // Suppose ONLY Row 0 is non-zero
    for (size_t i = 1; i <= LOG2(row_len); i++) {
        int32_t shift = static_cast<int32_t>(row_len * (row_len - 1) / (1 << i));
        // Negative shift moves index i to higher index i * N
        auto c_rot = cc->EvalRotate(c, -shift);
        c = cc->EvalAdd(c, c_rot);
    }

    if (mask_output) {
        c = mask_column(c, row_len, 0);
    }

    return c;
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> transpose_column(
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
    const size_t row_len,
    bool mask_output)
{
    // Suppose row_len is power of 2
    auto cc = c->GetCryptoContext();

    // Suppose ONLY Column 0 is non-zero
    for (size_t i = 1; i <= LOG2(row_len); i++) {
        int32_t shift = static_cast<int32_t>(row_len * (row_len - 1) / (1 << i));
        // Positive shift moves index i * N to lower index i
        auto c_rot = cc->EvalRotate(c, shift);
        c = cc->EvalAdd(c, c_rot);
    }

    if (mask_output) {
        c = mask_row(c, row_len, 0);
    }

    return c;
}

std::vector<int32_t> get_rotation_indices(
    const size_t row_len
)
{
    // Suppose row_len is power of 2
    std::vector<int32_t> indices;

    int32_t index;
    for (size_t i = 0; i < LOG2(row_len); i++)
    {
        index = 1 << i;
        indices.push_back(index);   // sumColumns
        indices.push_back(-index);  // replicateColumn

        index = row_len * (1 << i);
        indices.push_back(index);   // sumRows
        indices.push_back(-index);  // replicateRow

        index = (row_len * (row_len - 1) / (1 << (i + 1)));
        indices.push_back(index);   // transposeColumn
        indices.push_back(-index);  // transposeRow
    }

    return indices;
}

} // namespace anns_fhe