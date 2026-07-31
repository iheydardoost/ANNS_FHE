/**
 * @file utils-matrices.h
 * @brief OpenFHE CKKS Statistical Operations - Homomorphic Matrix Operations Utilities.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_UTILS_MATRICES_H
#define OPENFHE_STATS_UTILS_MATRICES_H

#include "utils-basics.h"

namespace openfhe_stats {

using namespace lbcrypto;

/**
 * @brief Masks out everything except a specific row of a ciphertext matrix.
 * @param c The ciphertext matrix.
 * @param matrixSize Size of square matrix.
 * @param rowIndex 0-based index of row to preserve.
 * @return Masked Ciphertext matrix.
 */
Ciphertext<DCRTPoly> maskRow(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize,
    const size_t rowIndex
);

/**
 * @brief Masks out everything except a specific column of a ciphertext matrix.
 * @param c The ciphertext matrix.
 * @param matrixSize Size of square matrix.
 * @param columnIndex 0-based index of column to preserve.
 * @return Masked Ciphertext matrix.
 */
Ciphertext<DCRTPoly> maskColumn(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize,
    const size_t columnIndex
);

/**
 * @brief Replicates a single row of a ciphertext matrix to all other rows.
 * Matrix size is expected to be a power of 2.
 */
Ciphertext<DCRTPoly> replicateRow(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize
);

/**
 * @brief Replicates the first column of a ciphertext matrix to all other columns.
 * Matrix size is expected to be a power of 2.
 */
Ciphertext<DCRTPoly> replicateColumn(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize
);

/**
 * @brief Sums all rows of a ciphertext matrix into a single row.
 */
Ciphertext<DCRTPoly> sumRows(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize,
    bool maskOutput = false,
    const size_t outputRow = 0
);

/**
 * @brief Sums all columns of a ciphertext matrix into a single column.
 */
Ciphertext<DCRTPoly> sumColumns(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize,
    bool maskOutput = false
);

/**
 * @brief Transposes a ciphertext row vector into a column vector.
 */
Ciphertext<DCRTPoly> transposeRow(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize,
    bool maskOutput = false
);

/**
 * @brief Transposes a ciphertext column vector into a row vector.
 */
Ciphertext<DCRTPoly> transposeColumn(
    Ciphertext<DCRTPoly> c,
    const size_t matrixSize,
    bool maskOutput = false
);

/**
 * @brief Generates rotation indices required for homomorphic operations on a matrix of given size.
 * @param matrixSize Size of square matrix.
 * @return Vector of rotation index integers.
 */
std::vector<int32_t> getRotationIndices(
    const size_t matrixSize
);

} // namespace openfhe_stats

#endif // OPENFHE_STATS_UTILS_MATRICES_H
