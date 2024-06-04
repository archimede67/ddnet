#ifndef BASE_HASH_COMBINE_H
#define BASE_HASH_COMBINE_H

#include <type_traits>

// Adapted from https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x/54728293#54728293

template<typename T, typename... Rest>
void HashCombine(std::size_t &seed, const T &v, const Rest &...rest)
{
	seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	(HashCombine(seed, rest), ...);
}

#endif
