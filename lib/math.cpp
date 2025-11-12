#include "math.hpp"
#include <random>
#include <stdexcept>

// Use a static generator initialized globally for the functions to use.
// Using rd() for initialization, as in the user's prompt.
std::random_device rd;
std::mt19937 generator1(rd());

/**
 * @brief Generates a pseudo-random double value in the range [0.0, 1.0).
 * * @return double A random double in [0.0, 1.0).
 */
double math::random() {
    // std::uniform_real_distribution is the standard way to get a double 
    // in a specific range, default is [0.0, 1.0).
    std::uniform_real_distribution<> distribution(0.0, 1.0);
    return distribution(generator1);
}

/**
 * @brief Generates a pseudo-random integer value in the range [1, upper].
 * * @param upper The exclusive upper bound. Must be > 0.
 * @return int A random integer in [1, upper].
 */
int math::random(int upper) {
    if (upper <= 0) {
        throw std::runtime_error("bad argument #1 to 'random' (interval is empty)");
    }
    // std::uniform_int_distribution generates integers uniformly.
    // The range is inclusive [a, b], so we use [0, upper - 1].
    std::uniform_int_distribution<> distribution(1, upper);
    return distribution(generator1);
}

/**
 * @brief Generates a pseudo-random integer value in the range [lower, upper].
 * * @param lower The inclusive lower bound.
 * @param upper The inclusive upper bound. Must be >= lower.
 * @return int A random integer in [lower, upper].
 */
int math::random(int lower, int upper) {
    if (lower > upper) {
        throw std::runtime_error("bad argument #2 to 'random' (interval is empty)");
    }
    // std::uniform_int_distribution range is inclusive [a, b].
    std::uniform_int_distribution<> distribution(lower, upper);
    return distribution(generator1);
}

/**
 * @brief Seeds the global random number generator (generator1).
 * * @param seed The integer value to seed the generator with.
 */
void math::randomseed(int seed) {
    // The mt19937 generator takes an integer seed.
    generator1.seed(seed);
}