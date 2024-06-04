#ifndef BASE_TUPLE_H
#define BASE_TUPLE_H

#include "hash_combine.h"

#include <tuple>

// Based on
// https://stackoverflow.com/questions/3611951/building-an-unordered-map-with-tuples-as-keys
//
// Allows using tuples as unordered_map keys

namespace TupleHash {

// Recursive template code derived from Matthieu M.
template<class Tuple, size_t Index = std::tuple_size_v<Tuple> - 1>
struct CHashValueImpl
{
	static void Apply(size_t &Seed, Tuple const &Tpl)
	{
		CHashValueImpl<Tuple, Index - 1>::Apply(Seed, Tpl);
		HashCombine(Seed, std::get<Index>(Tpl));
	}
};

template<class Tuple>
struct CHashValueImpl<Tuple, 0>
{
	static void Apply(size_t &Seed, Tuple const &Tpl)
	{
		HashCombine(Seed, std::get<0>(Tpl));
	}
};
} // namespace TupleHash

template<typename... TT>
struct CTupleHasher
{
	size_t operator()(std::tuple<TT...> const &Tuple) const
	{
		size_t Seed = 0;
		TupleHash::CHashValueImpl<std::tuple<TT...>>::Apply(Seed, Tuple);
		return Seed;
	}
};

#endif