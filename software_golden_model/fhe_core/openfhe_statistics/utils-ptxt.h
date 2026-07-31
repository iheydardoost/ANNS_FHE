/**
 * @file utils-ptxt.h
 * @brief OpenFHE CKKS Statistical Operations - Plaintext Operations and IO Utilities.
 * 
 * Based on the work:
 *   Paper: "Efficient Ranking, Order Statistics, and Sorting under CKKS"
 *          Federico Mazzone, Maarten Everts, Florian Hahn, Andreas Peter
 *          34th USENIX Security Symposium (USENIX Security '25)
 *          https://arxiv.org/abs/2412.15126
 *   GitHub: https://github.com/FedericoMazzone/openfhe-statistics.git
 */

#ifndef OPENFHE_STATS_UTILS_PTXT_H
#define OPENFHE_STATS_UTILS_PTXT_H

#include <iomanip>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

// Safe inline helper definitions to avoid macro collisions
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN_VEC
#define MIN_VEC(V) *std::min_element((V).begin(), (V).end())
#endif

#ifndef MAX_VEC
#define MAX_VEC(V) *std::max_element((V).begin(), (V).end())
#endif

#ifndef LOG2
#define LOG2(X) (size_t) std::ceil(std::log2((X)))
#endif

namespace openfhe_stats {

/**
 * @brief Calculates the component-wise average of a vector of vectors.
 */
std::vector<double> averageVectors(
    const std::vector<std::vector<double>>& vectors
);

/**
 * @brief Splits a vector into a specified number of equal-sized subvectors.
 */
std::vector<std::vector<double>> splitVector(
    const std::vector<double>& vec,
    const size_t numSubvectors
);

/**
 * @brief Flattens a vector of vectors into a single vector.
 */
std::vector<double> concatVectors(
    const std::vector<std::vector<double>>& vecOfVecs
);

/**
 * @brief Overloaded stream insertion operator to print a 2D matrix in aligned format.
 */
template <typename T>
std::ostream& operator<<(
    std::ostream& os,
    const std::vector<std::vector<T>>& matrix
)
{
    if (matrix.empty()) return os;

    size_t numRows = matrix.size();
    size_t numCols = matrix[0].size();

    std::vector<size_t> maxWidths(numCols, 0);
    for (size_t i = 0; i < numRows; i++)
    {
        for (size_t j = 0; j < numCols; j++)
        {
            size_t width = std::to_string(matrix[i][j]).length();
            if (width > maxWidths[j])
                maxWidths[j] = width;
        }
    }

    os << std::endl;
    for (size_t i = 0; i < numRows; ++i)
    {
        for (size_t j = 0; j < numCols; ++j)
        {
            os << std::setw(maxWidths[j]) << matrix[i][j];
            if (j != numCols - 1)
                os << "  ";
        }
        os << std::endl;
    }

    return os;
}

/**
 * @brief Converts a 1D vector into a square 2D matrix row-wise.
 */
std::vector<std::vector<double>> vector2matrix(
    const std::vector<double> &vec,
    size_t matrixSize
);

/**
 * @brief Flattens a square 2D matrix into a 1D vector row-wise.
 */
std::vector<double> matrix2vector(
    const std::vector<std::vector<double>>& matrix,
    const size_t matrixSize
);

/**
 * @brief Loads a 1D double vector from a CSV file.
 */
std::vector<double> loadPoints1D(
    const size_t vectorLength
);

} // namespace openfhe_stats

#endif // OPENFHE_STATS_UTILS_PTXT_H
