#ifndef AUTOMORPHISM_H
#define AUTOMORPHISM_H

#include <stdint.h>

// Automorphism transform
// a: input array of size N
// out: output array of size N
// auto_map_64: permutation map of size N packed into N/2 uint64_t words
void automorphism(const uint64_t* a, uint64_t* out, const uint64_t* auto_map_64);

#endif
