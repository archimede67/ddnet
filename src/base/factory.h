#ifndef BASE_FACTORY_H
#define BASE_FACTORY_H

// Based on https://github.com/Xadeck/Registerer/blob/master/registerer.h

#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

template<typename T>
using has_trait_key = std::is_member_function_pointer<decltype(&T::_trait::Key)>;

template<class T>
struct type_t
{
};

template<typename T, typename Ret = T>
class CFactoryRegistry
{
public:
	static std::shared_ptr<Ret> New(const std::string &key)
	{
		auto Entry = GetEntry(key);
		if(Entry.first)
			return std::make_shared<Ret>((*Entry.second)());
		return nullptr;
	}

	template<typename C, typename std::enable_if_t<has_trait_key<C>::value, bool> = true>
	static const char *GetKeyFor(type_t<C>)
	{
		return C::_trait::Key();
	}

private:
	using FuncType = std::function<Ret()>;

	struct SRegisterer
	{
		SRegisterer(const FuncType &fnCreator, const std::string &Key)
		{
			GetRegistry().insert(std::make_pair(Key, fnCreator));
		}
	};

	static auto &GetRegistry()
	{
		static std::unordered_map<std::string, FuncType> Reg;
		return Reg;
	}

	static std::pair<bool, FuncType *> GetEntry(const std::string &key)
	{
		auto it = GetRegistry().find(key);
		auto found = (it != GetRegistry().end());
		return std::make_pair(found, found ? &(it->second) : nullptr);
	}

	template<typename Trait, typename Base, typename Derived, typename RetType>
	friend struct CTypeRegisterer;
};

template<typename Trait, typename Base, typename Derived, typename RetType>
struct CTypeRegisterer
{
	static const typename CFactoryRegistry<Base, RetType>::SRegisterer ms_Instance;
};

template<typename Trait, typename Base, typename Derived, typename RetType>
const typename CFactoryRegistry<Base, RetType>::SRegisterer CTypeRegisterer<Trait, Base, Derived, RetType>::ms_Instance([]() -> RetType { return Derived(); }, Trait::Key());

#define MACRO_FACTORY_REGISTER_FULL(KEY, TYPE, RET) \
private: \
	friend class CFactoryRegistry<TYPE, RET>; \
	struct _trait \
	{ \
		static const char *Key() { return KEY; } \
	}; \
	const void *_init() const \
	{ \
		return &CTypeRegisterer<_trait, TYPE, std::decay<decltype(*this)>::type, RET>::ms_Instance; \
	} \
\
public: \
	static_assert(true, "")

#define MACRO_FACTORY_REGISTER(KEY, TYPE) MACRO_FACTORY_REGISTER_FULL(KEY, TYPE, TYPE)

#endif