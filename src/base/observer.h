#ifndef BASE_OBSERVER_H
#define BASE_OBSERVER_H

#include <functional>
#include <unordered_set>

#include <base/system.h>

template<typename... Args>
class CObservable
{
	using TObserver = std::function<void(Args...)>;

public:
	int Subscribe(const TObserver &Observer)
	{
		m_vObservers.push_back(Observer);
		return m_vObservers.size() - 1;
	}

	void Unsubscribe(const TObserver &Observer)
	{
		m_vObservers.erase(Observer);
	}

	void Unsubscribe(size_t Index)
	{
		dbg_assert(Index < m_vObservers.size(), "Invalid observer index");
		m_vObservers.erase(m_vObservers.begin() + Index);
	}

	void Notify(Args &&...Arguments)
	{
		for(const auto &Observer : m_vObservers)
			Observer(Arguments...);
	}

private:
	std::vector<TObserver> m_vObservers;
};

#endif
