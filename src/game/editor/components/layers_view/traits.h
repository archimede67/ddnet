#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_TRAITS_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_TRAITS_H

#include <base/traits.h>

namespace QueryTraits {

// Traits
// template<typename T>
// using hash_method_t = decltype(std::declval<T>().Hash());
//
// template<typename T>
// using is_hashed_t = traits::detector<hash_method_t, void, T>();

template<class T, class V = void>
struct IsHashed : std::false_type
{
};

template<class T>
struct IsHashed<T, std::void_t<decltype(std::declval<T>().Hash())>> : std::true_type
{
};

} // namespace QueryTraits

#endif
