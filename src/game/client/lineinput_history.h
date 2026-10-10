#ifndef GAME_CLIENT_LINEINPUT_HISTORY_H
#define GAME_CLIENT_LINEINPUT_HISTORY_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/** Local to one focused text draft; never falls through to document history. */
class CLineInputHistory
{
public:
	class CState
	{
	public:
		std::string m_Text;
		std::size_t m_Cursor = 0;
		std::size_t m_SelectionStart = 0;
		std::size_t m_SelectionEnd = 0;
		bool operator==(const CState &) const = default;
	};

private:
	static constexpr std::size_t MAX_STATES = 128;
	std::vector<CState> m_vStates;
	std::size_t m_Cursor = 0;

	void Synchronize(const CState &Current)
	{
		if(m_vStates.empty() || m_vStates[m_Cursor].m_Text != Current.m_Text)
		{
			m_vStates = {Current};
			m_Cursor = 0;
		}
		else
			m_vStates[m_Cursor] = Current;
	}

public:
	void Clear()
	{
		std::vector<CState>().swap(m_vStates);
		m_Cursor = 0;
	}

	void Record(const CState &Before, CState After)
	{
		Synchronize(Before);
		if(Before.m_Text == After.m_Text)
		{
			m_vStates[m_Cursor] = std::move(After);
			return;
		}
		m_vStates.erase(m_vStates.begin() + m_Cursor + 1, m_vStates.end());
		m_vStates.push_back(std::move(After));
		if(m_vStates.size() > MAX_STATES)
			m_vStates.erase(m_vStates.begin());
		m_Cursor = m_vStates.size() - 1;
	}

	std::optional<CState> Undo(const CState &Current)
	{
		Synchronize(Current);
		if(m_Cursor == 0)
			return std::nullopt;
		return m_vStates[--m_Cursor];
	}

	std::optional<CState> Redo(const CState &Current)
	{
		Synchronize(Current);
		if(m_Cursor + 1 == m_vStates.size())
			return std::nullopt;
		return m_vStates[++m_Cursor];
	}
};

#endif
