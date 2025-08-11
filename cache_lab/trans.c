/**
 * @file trans.c
 * @brief Contains various implementations of matrix transpose
 *
 * Each transpose function must have a prototype of the form:
 *   void trans(size_t M, size_t N, double A[N][M], double B[M][N],
 *              double tmp[TMPCOUNT]);
 *
 * All transpose functions take the following arguments:
 *
 *   @param[in]     M    Width of A, height of B
 *   @param[in]     N    Height of A, width of B
 *   @param[in]     A    Source matrix
 *   @param[out]    B    Destination matrix
 *   @param[in,out] tmp  Array that can store temporary double values
 *
 * A transpose function is evaluated by counting the number of hits and misses,
 * using the cache parameters and score computations described in the writeup.
 *
 * Programming restrictions:
 *   - No out-of-bounds references are allowed
 *   - No alterations may be made to the source array A
 *   - Data in tmp can be read or written
 *   - This file cannot contain any local or global doubles or arrays of doubles
 *   - You may not use unions, casting, global variables, or
 *     other tricks to hide array data in other forms of local or global memory.
 *
 * TODO: fill in your name and Andrew ID below.
 * @author Ziyue Huang <ziyuehua@andrew.cmu.edu>
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "cachelab.h"

/**
 * @brief Checks if B is the transpose of A.
 *
 * You can call this function inside of an assertion, if you'd like to verify
 * the correctness of a transpose function.
 *
 * @param[in]     M    Width of A, height of B
 * @param[in]     N    Height of A, width of B
 * @param[in]     A    Source matrix
 * @param[out]    B    Destination matrix
 *
 * @return True if B is the transpose of A, and false otherwise.
 */
#ifndef NDEBUG
static bool is_transpose(size_t M, size_t N, double A[N][M], double B[M][N]) {
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                fprintf(stderr,
                        "Transpose incorrect.  Fails for B[%zd][%zd] = %.3f, "
                        "A[%zd][%zd] = %.3f\n",
                        j, i, B[j][i], i, j, A[i][j]);
                return false;
            }
        }
    }
    return true;
}
#endif

/*
 * You can define additional transpose functions here. We've defined
 * some simple ones below to help you get started, which you should
 * feel free to modify or delete.
 */

/**
 * @brief A simple baseline transpose function, not optimized for the cache.
 *
 * Note the use of asserts (defined in assert.h) that add checking code.
 * These asserts are disabled when measuring cycle counts (i.e. when running
 * the ./test-trans) to avoid affecting performance.
 */
static void trans_basic(size_t M, size_t N, double A[N][M], double B[M][N],
                        double tmp[TMPCOUNT]) {
    assert(M > 0);
    assert(N > 0);

    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < M; j++) {
            B[j][i] = A[i][j];
        }
    }

    assert(is_transpose(M, N, A, B));
}

/**
 * @brief A contrived example to illustrate the use of the temporary array.
 *
 * This function uses the first four elements of tmp as a 2x2 array with
 * row-major ordering.
 */
static void trans_tmp(size_t M, size_t N, double A[N][M], double B[M][N],
                      double tmp[TMPCOUNT]) {
    assert(M > 0);
    assert(N > 0);

    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < M; j++) {
            size_t di = i % 2;
            size_t dj = j % 2;
            tmp[2 * di + dj] = A[i][j];
            B[j][i] = tmp[2 * di + dj];
        }
    }

    assert(is_transpose(M, N, A, B));
}

/**
 * @brief The solution transpose function that will be graded.
 *
 * You can call other transpose functions from here as you please.
 * It's OK to choose different functions based on array size, but
 * this function must be correct for all values of M and N.
 */
static void transpose_submit(size_t M, size_t N, double A[N][M], double B[M][N],
                             double tmp[TMPCOUNT]) {

    size_t block_size = 8;  // Block size can be tuned for cache performance
    size_t i, j, i_block, j_block;

    // Block loop to cover entire matrix
    for (i_block = 0; i_block < N; i_block += block_size) {
        for (j_block = 0; j_block < M; j_block += block_size) {
            
            // Transpose individual blocks
            for (i = i_block; i < i_block + block_size && i < N; i++) {
                for (j = j_block; j < j_block + block_size && j < M; j++) {
                    if (i != j) {
                        B[j][i] = A[i][j];
                    } else {
                        // Handle diagonal elements safely per block
                        tmp[i - i_block] = A[i][j];
                    }
                }
            }

            // Store diagonal elements after processing the block
            if (i_block == j_block) {
                for (i = i_block; i < i_block + block_size && i < N && i < M; i++) {
                    B[i][i] = tmp[i - i_block];
                }
            }
        }
    }
}





/**
 * @brief Registers all transpose functions with the driver.
 *
 * At runtime, the driver will evaluate each function registered here, and
 * and summarize the performance of each. This is a handy way to experiment
 * with different transpose strategies.
 */
void registerFunctions(void) {
    // Register the solution function. Do not modify this line!
    registerTransFunction(transpose_submit, SUBMIT_DESCRIPTION);

    // Register any additional transpose functions
    registerTransFunction(trans_basic, "Basic transpose");
    registerTransFunction(trans_tmp, "Transpose using the temporary array");
    registerTransFunction(transpose_submit, "Block size is now 8");
}
