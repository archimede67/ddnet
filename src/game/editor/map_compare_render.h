#ifndef GAME_EDITOR_MAP_COMPARE_RENDER_H
#define GAME_EDITOR_MAP_COMPARE_RENDER_H

#include <algorithm>
#include <utility>

namespace MapCompareRender
{
	// Canonical cells are ordered by (y, x). Seek each row's visible columns
	// instead of walking all off-screen columns of a very wide map.
	template<typename TCells, typename F>
	void ForVisibleCells(const TCells &Cells, int MinX, int MinY, int MaxX, int MaxY, F Visit)
	{
		auto Less = [](const auto &Cell, const std::pair<int, int> &Position) {
			return Cell.m_Y < Position.first || (Cell.m_Y == Position.first && Cell.m_X < Position.second);
		};
		auto It = Cells.begin();
		for(int Y = MinY; Y <= MaxY && It != Cells.end(); ++Y)
		{
			It = std::lower_bound(It, Cells.end(), std::pair{Y, MinX}, Less);
			for(; It != Cells.end() && It->m_Y == Y && It->m_X <= MaxX; ++It)
				Visit(*It);
		}
	}
}
#endif
