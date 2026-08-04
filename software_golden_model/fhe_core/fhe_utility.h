#ifndef FHE_UTILITY_H
#define FHE_UTILITY_H

#include "openfhe.h"
#include "fhe_config.h"
#include "utils-ptxt.h"

namespace anns_fhe
{
    /**
     * @brief Masks out everything except a specific row of a ciphertext matrix.
     * @param c The ciphertext matrix.
     * @param row_len Size of square matrix equals row_len^2.
     * @param row_index 0-based index of row to preserve.
     * @return Masked Ciphertext matrix.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> mask_row(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len,
        const size_t row_index
    );

    /**
     * @brief Masks out everything except a specific column of a ciphertext matrix.
     * @param c The ciphertext matrix.
     * @param row_len Size of square matrix equals row_len^2.
     * @param column_index 0-based index of column to preserve.
     * @return Masked Ciphertext matrix.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> mask_column(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len,
        const size_t column_index
    );

    /**
     * @brief Replicates a single row of a ciphertext matrix to all other rows.
     * row_len is expected to be a power of 2.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> replicate_row(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len
    );

    /**
     * @brief Replicates the first column of a ciphertext matrix to all other columns.
     * row_len is expected to be a power of 2.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> replicate_column(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len
    );

    /**
     * @brief Sums all rows of a ciphertext matrix into a single row.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sum_rows(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len,
        bool mask_output = false,
        const size_t output_row = 0
    );

    /**
     * @brief Sums all columns of a ciphertext matrix into a single column.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sum_columns(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len,
        bool mask_output = false,
        const size_t output_column = 0
    );

    /**
     * @brief Transposes a ciphertext row vector into a column vector.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> transpose_row(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len,
        bool mask_output = false
    );

    /**
     * @brief Transposes a ciphertext column vector into a row vector.
     */
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> transpose_column(
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> c,
        const size_t row_len,
        bool mask_output = false
    );

    /**
     * @brief Generates rotation indices required for homomorphic operations on a matrix of given size.
     * @param row_len Size of square matrix equals row_len^2.
     * @return Vector of rotation index integers.
     */
    std::vector<int32_t> get_rotation_indices(
        const size_t row_len
    );

} // namespace anns_fhe

#endif // FHE_UTILITY_H
