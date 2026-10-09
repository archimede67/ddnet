#include <game/editor/map_compare_render.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	struct CCoordinate
	{
		int m_Value;
		size_t *m_pReads;
		operator int() const
		{
			++*m_pReads;
			return m_Value;
		}
	};
	struct CCell
	{
		CCoordinate m_X;
		CCoordinate m_Y;
	};
}

TEST(MapCompareRender, FarEdgeTraversalIsBoundedByViewport)
{
	size_t Reads = 0;
	std::vector<CCell> Cells;
	for(int Y = 0; Y < 32; ++Y)
		for(int X = 0; X < 20000; ++X)
			Cells.push_back({{X, &Reads}, {Y, &Reads}});
	std::vector<std::pair<int, int>> Visible;
	MapCompareRender::ForVisibleCells(Cells, 19994, 12, 19999, 17, [&](const auto &Cell) { Visible.emplace_back(Cell.m_X, Cell.m_Y); });
	ASSERT_EQ(Visible.size(), 36u);
	for(int Y = 12; Y <= 17; ++Y)
		for(int X = 19994; X <= 19999; ++X)
			EXPECT_EQ(Visible[(Y - 12) * 6 + X - 19994], std::make_pair(X, Y));
	EXPECT_LT(Reads, 2000u); // Reject scanning even one whole 20,000-cell row.
}

TEST(MapCompareRender, EmptyRowsAndViewportEdges)
{
	size_t Reads = 0;
	const std::vector<CCell> Cells = {{{0, &Reads}, {0, &Reads}}, {{8, &Reads}, {0, &Reads}}, {{4, &Reads}, {5, &Reads}}, {{9, &Reads}, {5, &Reads}}, {{3, &Reads}, {8, &Reads}}};
	std::vector<std::pair<int, int>> Visible;
	MapCompareRender::ForVisibleCells(Cells, 4, -2, 8, 5, [&](const auto &Cell) { Visible.emplace_back(Cell.m_X, Cell.m_Y); });
	ASSERT_EQ(Visible.size(), 2u);
	EXPECT_EQ(Visible[0], std::make_pair(8, 0));
	EXPECT_EQ(Visible[1], std::make_pair(4, 5));
	MapCompareRender::ForVisibleCells(Cells, 100, 100, 110, 110, [](const auto &) { ADD_FAILURE() << "off-screen cell visited"; });
	MapCompareRender::ForVisibleCells(std::vector<CCell>{}, 0, 0, 100, 100, [](const auto &) { ADD_FAILURE() << "empty layer visited"; });
}
