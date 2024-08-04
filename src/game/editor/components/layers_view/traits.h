#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_TRAITS_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_TRAITS_H

namespace QueryTraits {

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
