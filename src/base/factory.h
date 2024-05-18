#ifndef BASE_FACTORY_H
#define BASE_FACTORY_H

// Based on https://github.com/Xadeck/Registerer/blob/master/registerer.h
// Modified to support key type and created object type
// Simplified (removed injectors and other unused methods)

#include <functional>
#include <memory>
#include <unordered_map>

/**
 * Factory class
 *
 * @tparam TKey The type of the key used to register objects
 * @tparam T The base class of objects stored
 * @tparam Ret (optional) The type of created objects. By default, this is equal to T
 */
template<typename TKey, typename T, typename Ret = T>
class CFactoryRegistry
{
public:
	// Type of the key
	using KeyType = TKey;

	// Base type
	using BaseType = T;

	// Base type of created objects
	using RetType = Ret;

	/**
	 * Creates an object from a specific key.
	 *
	 * @param Key The key identifying the object to create
	 *
	 * @return A smart pointer of type Ret if the key exists, nullptr otherwise.
	 */
	static std::shared_ptr<Ret> New(const TKey &Key)
	{
		auto Entry = GetEntry(Key);
		if(Entry.first)
			return std::make_shared<Ret>((*Entry.second)());
		return nullptr;
	}

	/**
	 * Gets the key for a specified class. The class requires to have a trait struct
	 * which is defined using the MACRO_FACTORY_REGISTER_FULL macro.
	 *
	 * @tparam C The class to get the key for
	 *
	 * @return The key associated to C. If the class C does not contain a key trait,
	 * this will result in a compilation error.
	 */
	template<typename C>
	static TKey KeyFor()
	{
		return C::_trait::Key();
	}

private:
	// Type of the function used to create new objects
	using FuncType = std::function<Ret()>;

	// Class used to register a new class to a key
	struct CRegisterer
	{
		CRegisterer(const FuncType &fnCreator, const TKey &Key)
		{
			// Inserts a new pair into the registry
			// This pair identifies the create method from a key which will then be used to
			// resolve the correct function when we want to create a new object from only a key
			Registry().insert(std::make_pair(Key, fnCreator));
		}
	};

	static auto &Registry()
	{
		// Registry map. The map itself is not stored as a static member inside this class
		// in order to avoid static initialization order fiasco
		static std::unordered_map<TKey, FuncType> s_Reg;
		return s_Reg;
	}

	/**
	 * Gets the entry associated to a specific key.
	 *
	 * @param Key The key
	 *
	 * @return A pair where the first element is a boolean indicating if the entry was found,
	 * and the second element is a pointer to the function used to create a new instance of the
	 * class associated to the key.
	 */
	static std::pair<bool, FuncType *> GetEntry(const TKey &Key)
	{
		auto It = Registry().find(Key);
		auto Found = (It != Registry().end());
		return std::make_pair(Found, Found ? &(It->second) : nullptr);
	}

	template<typename _TKey, typename Trait, typename Base, typename Derived, typename RetType>
	friend struct CTypeRegisterer;
};

// Type registerer based on template parameters
template<typename TKey, typename Trait, typename Base, typename Derived, typename RetType>
struct CTypeRegisterer
{
	// Instance of the registerer class. When this member is initialized, it will register
	// a new entry into the factory registry.
	static const typename CFactoryRegistry<TKey, Base, RetType>::CRegisterer ms_Instance;
};

// Declare the registerer instance. The register class takes a function and a key to register a specific type.
// The key is obtained from the trait class, which is passed as a template argument from the macro below.
// The function used to create a new instance is simply a lambda that returns a regular object of the derived type.
// In order for this member to be initialized, it needs to be used (referenced) from somewhere.
template<typename TKey, typename Trait, typename Base, typename Derived, typename RetType>
const typename CFactoryRegistry<TKey, Base, RetType>::CRegisterer
	CTypeRegisterer<TKey, Trait, Base, Derived, RetType>::ms_Instance([]() -> RetType { return Derived(); }, Trait::Key());

/**
 * Main macro to perform registration of a class into a factory.
 *
 * @param KEY The key to associate with this class
 * @param FACTORY The factory to register this class into
 *
 * @parblock
 * It declares a private block in which are defined a few things:
 * - The key type of the key being registered (TKey)
 * - The trait structure, containing a method returning the registered key (_trait::Key())
 * - An init method used to reference the static member CFactoryRegistry::ms_Instance in order for
 *   templates to be compiled and not optimized away (_init()). This method is unused but is also
 *   required because we need to access the current class type, which does not work in a static context
 *   (since we use decltype(*this) for that).
 *
 * Once all the necessary elements have been defined, it starts a new public block. This means that the
 * macro can be placed anywhere within a class/struct, although it's best to place it at the very beginning.
 *
 * @note This macro needs to be used after the declaration of the factory.
 * @note This macro does not add any additional bytes to the class it is used in.
 *
 *
 * Full example usage, with factory definition:
 * @endparblock
 *
 * @code{.cpp}
 * struct IMyBase
 * {
 *     virtual void Print() = 0;
 * };
 *
 * enum class MyType
 * {
 *     MY_FIRST_TYPE = 0,
 *     MY_SECOND_TYPE
 * };
 *
 * using MyFactory = CFactoryRegistry<MyType, IMyBase>;
 *
 * struct MyFirstClass : IMyBase
 * {
 *     MACRO_FACTORY_REGISTER(MyType::MY_FIRST_TYPE, MyFactory); // Note: semicolon required
 *
 *     void Print() override { printf("First class\n"); }
 * };
 *
 * struct MySecondClass : IMyBase
 * {
 *     MACRO_FACTORY_REGISTER(MyType::MY_SECOND_TYPE, MyFactory);
 *
 *     void Print() override { printf("Second class\n"); }
 * };
 *
 * int main()
 * {
 *     std::shared_ptr<IMyBase> pBase = MyFactory::New(MyType::MY_SECOND_TYPE);
 *     pBase->Print(); // Prints "Second class"
 * }
 * @endcode
 */
#define MACRO_FACTORY_REGISTER(KEY, FACTORY) \
private: \
	using TKey = decltype(KEY); \
	friend class CFactoryRegistry<TKey, FACTORY::BaseType, FACTORY::RetType>; \
	struct _trait \
	{ \
		static TKey Key() { return KEY; } \
	}; \
	const void *_init() const \
	{ \
		return &CTypeRegisterer<TKey, _trait, FACTORY::BaseType, std::decay<decltype(*this)>::type, FACTORY::RetType>::ms_Instance; \
	} \
\
public:

#endif