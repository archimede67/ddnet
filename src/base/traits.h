#ifndef BASE_TRAITS_H
#define BASE_TRAITS_H

// Detector idiom
// From https://stackoverflow.com/a/62061759

#include <type_traits>

namespace traits {

template<template<class...> class Trait, class AlwaysVoid, class... Args>
struct detector : std::false_type
{
};

template<template<class...> class Trait, class... Args>
struct detector<Trait, std::void_t<Trait<Args...>>, Args...> : std::true_type
{
};

} // namespace traits

#endif
