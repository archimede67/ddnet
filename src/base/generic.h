#ifndef BASE_GENERIC_H
#define BASE_GENERIC_H

#include <any>
#include <memory>
#include <type_traits>

// Inheritance without pointers
// https://www.fluentcpp.com/2021/01/29/inheritance-without-pointers/

template<typename Interface>
struct CInterfaceImpl
{
public:
	template<typename ConcreteType>
	explicit CInterfaceImpl(ConcreteType &&Object) :
		m_Storage(std::forward<ConcreteType>(Object)),
		m_fnGetter{[](std::any &Storage) -> Interface & { return std::any_cast<ConcreteType &>(Storage); }},
		m_fnConvert([](std::any &Storage) -> std::shared_ptr<Interface> { return std::make_shared<ConcreteType>(std::any_cast<ConcreteType &>(Storage)); })
	{
	}

	Interface *operator->() { return &m_fnGetter(m_Storage); }

	std::shared_ptr<Interface> Convert()
	{
		return m_fnConvert(m_Storage);
	}

private:
	std::any m_Storage;
	Interface &(*m_fnGetter)(std::any &);
	std::shared_ptr<Interface> (*m_fnConvert)(std::any &);
};

#endif