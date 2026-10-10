#include <game/editor/map_diff.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
	MapDiff::CSnapshot Map()
	{
		MapDiff::CSnapshot Map;
		Map.m_vInfo = {{"Author", "Mapper"}};
		Map.m_vGroups.push_back({"Game", true});
		MapDiff::CLayer Layer;
		Layer.m_Kind = MapDiff::ELayerKind::GAME;
		Layer.m_Name = "Game";
		Layer.m_Width = Layer.m_Height = 16;
		Layer.m_vCells.push_back({2, 3, {1}});
		Map.m_vGroups[0].m_vLayers.push_back(Layer);
		return Map;
	}
	const MapDiff::CEntry &LayerEntry(const MapDiff::CResult &Result)
	{
		return *std::find_if(Result.m_vEntries.begin(), Result.m_vEntries.end(), [](const auto &Entry) { return Entry.m_Kind == MapDiff::EEntryKind::LAYER; });
	}
	MapDiff::CObject Object(int X)
	{
		return {{{"x", std::to_string(X)}, {"color", "255"}}, {}};
	}
}

TEST(MapDiff, EqualOwnedSnapshots)
{
	auto Before = Map();
	auto After = Before;
	EXPECT_EQ(MapDiff::Compare(Before, After).ChangeCount(), 0u);
	Before.m_vGroups[0].m_vLayers[0].m_vCells[0].m_Tile.m_Index = 2;
	EXPECT_EQ(After.m_vGroups[0].m_vLayers[0].m_vCells[0].m_Tile.m_Index, 1);
	EXPECT_EQ(LayerEntry(MapDiff::Compare(Before, After)).m_vTiles.size(), 1u);
}

TEST(MapDiff, TileCoordinatesAcrossResize)
{
	auto Before = Map();
	auto After = Before;
	After.m_vGroups[0].m_vLayers[0].m_Width = 32;
	auto Result = MapDiff::Compare(Before, After);
	EXPECT_TRUE(LayerEntry(Result).m_vTiles.empty());
	EXPECT_EQ(LayerEntry(Result).m_vProperties.size(), 1u);
	After.m_vGroups[0].m_vLayers[0].m_vCells.push_back({23, 3, {2}});
	Result = MapDiff::Compare(Before, After);
	ASSERT_EQ(LayerEntry(Result).m_vTiles.size(), 1u);
	EXPECT_EQ(LayerEntry(Result).m_vTiles[0].m_X, 23);
}

TEST(MapDiff, EveryTileParameterIsCompared)
{
	for(auto Kind : {MapDiff::ELayerKind::TILES, MapDiff::ELayerKind::GAME, MapDiff::ELayerKind::FRONT, MapDiff::ELayerKind::TELE, MapDiff::ELayerKind::SPEEDUP, MapDiff::ELayerKind::SWITCH, MapDiff::ELayerKind::TUNE})
	{
		auto Before = Map();
		Before.m_vGroups[0].m_vLayers[0].m_Kind = Kind;
		for(int Parameter = 0; Parameter < 7; ++Parameter)
		{
			auto After = Before;
			auto &Tile = After.m_vGroups[0].m_vLayers[0].m_vCells[0].m_Tile;
			switch(Parameter)
			{
			case 0: Tile.m_Index++; break;
			case 1: Tile.m_Flags++; break;
			case 2: Tile.m_Number++; break;
			case 3: Tile.m_Delay++; break;
			case 4: Tile.m_Force++; break;
			case 5: Tile.m_MaxSpeed++; break;
			case 6: Tile.m_Angle--; break;
			}
			EXPECT_EQ(LayerEntry(MapDiff::Compare(Before, After)).m_vTiles.size(), 1u);
		}
	}
}

TEST(MapDiff, InsertedLayerDoesNotModifyFollowingLayers)
{
	auto Before = Map();
	MapDiff::CLayer A;
	A.m_Name = "Art";
	A.m_vCells.push_back({1, 2, {3}});
	Before.m_vGroups[0].m_vLayers.push_back(A);
	auto After = Before;
	A.m_Name = "Inserted";
	A.m_vCells[0].m_Tile.m_Index = 5;
	After.m_vGroups[0].m_vLayers.insert(After.m_vGroups[0].m_vLayers.begin(), A);
	auto Result = MapDiff::Compare(Before, After);
	int Added = 0;
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER)
		{
			if(Entry.m_Before < 0)
				++Added;
			else
				EXPECT_FALSE(Entry.Changed());
		}
	EXPECT_EQ(Added, 1);
}

TEST(MapDiff, ObjectInsertionAndRelativeOrder)
{
	auto Before = Map();
	auto &Layer = Before.m_vGroups[0].m_vLayers[0];
	Layer.m_Kind = MapDiff::ELayerKind::QUADS;
	Layer.m_vCells.clear();
	Layer.m_vObjects = {Object(0), Object(1), Object(2)};
	auto After = Before;
	auto &Objects = After.m_vGroups[0].m_vLayers[0].m_vObjects;
	Objects.insert(Objects.begin(), Object(42));
	auto Result = MapDiff::Compare(Before, After);
	ASSERT_EQ(LayerEntry(Result).m_vObjects.size(), 1u);
	EXPECT_EQ(LayerEntry(Result).m_vObjects[0].m_Before, -1);
	std::swap(Objects[1], Objects[3]);
	Result = MapDiff::Compare(Before, After);
	EXPECT_GT(LayerEntry(Result).m_vObjects.size(), 1u);
	EXPECT_TRUE(std::any_of(LayerEntry(Result).m_vObjects.begin(), LayerEntry(Result).m_vObjects.end(), [](const auto &O) { return O.m_Reordered; }));
}

TEST(MapDiff, DuplicateNamesNeverLoseData)
{
	auto Before = Map();
	auto &Layers = Before.m_vGroups[0].m_vLayers;
	Layers[0].m_Kind = MapDiff::ELayerKind::TILES;
	Layers[0].m_Name = "duplicate";
	Layers.push_back(Layers[0]);
	auto After = Before;
	After.m_vGroups[0].m_vLayers[1].m_vCells[0].m_Tile.m_Index = 7;
	auto Result = MapDiff::Compare(Before, After);
	int Tiles = 0, Ambiguous = 0;
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER)
		{
			Tiles += Entry.m_vTiles.size();
			Ambiguous += Entry.m_Ambiguous;
		}
	EXPECT_EQ(Tiles, 1);
	EXPECT_GT(Ambiguous, 0);
}

TEST(MapDiff, UniqueUnnamedContentAndPositionalProvenance)
{
	auto Before = Map();
	auto &Layers = Before.m_vGroups[0].m_vLayers;
	Layers[0].m_Name.clear();
	Layers[0].m_Kind = MapDiff::ELayerKind::TILES;
	Layers.push_back(Layers[0]);
	Layers[1].m_vCells[0].m_Tile.m_Index = 2;
	auto After = Before;
	After.m_vGroups[0].m_vLayers.insert(After.m_vGroups[0].m_vLayers.begin(), Layers[0]);
	After.m_vGroups[0].m_vLayers[0].m_vCells[0].m_Tile.m_Index = 3;
	auto Result = MapDiff::Compare(Before, After);
	int Added = 0;
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER)
		{
			EXPECT_FALSE(Entry.m_Ambiguous);
			EXPECT_FALSE(Entry.m_Positional);
			EXPECT_FALSE(Entry.m_Reordered);
			if(Entry.m_Before < 0)
				++Added;
			else
			{
				EXPECT_EQ(Entry.m_After, Entry.m_Before + 1);
				EXPECT_FALSE(Entry.Changed());
			}
		}
	EXPECT_EQ(Added, 1);
	After = Before;
	std::swap(After.m_vGroups[0].m_vLayers[0], After.m_vGroups[0].m_vLayers[1]);
	Result = MapDiff::Compare(Before, After);
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER)
		{
			EXPECT_TRUE(Entry.m_Reordered);
			EXPECT_FALSE(Entry.m_Ambiguous);
			EXPECT_FALSE(Entry.m_Positional);
			EXPECT_TRUE(Entry.m_vTiles.empty());
		}
	After = Before;
	After.m_vGroups[0].m_vLayers[0].m_vCells[0].m_Tile.m_Index = 9;
	Result = MapDiff::Compare(Before, After);
	EXPECT_TRUE(LayerEntry(Result).m_Positional);
	EXPECT_TRUE(LayerEntry(Result).m_Ambiguous);
	EXPECT_EQ(LayerEntry(Result).m_vTiles.size(), 1u);
}

TEST(MapDiff, MatchingProvenanceForGroupsResourcesAndObjects)
{
	MapDiff::CSnapshot Before;
	Before.m_vGroups.push_back({"", false, {{"Offset X", "0"}}});
	Before.m_vEnvelopes.push_back({"", {{"value", "1"}}});
	auto After = Before;
	auto Result = MapDiff::Compare(Before, After);
	for(const auto &Entry : Result.m_vEntries)
		EXPECT_FALSE(Entry.m_Ambiguous || Entry.m_Positional);
	After.m_vGroups[0].m_vProperties[0].m_Value = "20";
	After.m_vEnvelopes[0].m_vProperties[0].m_Value = "2";
	Result = MapDiff::Compare(Before, After);
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::GROUP || Entry.m_Kind == MapDiff::EEntryKind::ENVELOPE)
			EXPECT_TRUE(Entry.m_Ambiguous && Entry.m_Positional && Entry.Changed());
	Before.m_vGroups.push_back(Before.m_vGroups[0]);
	Result = MapDiff::Compare(Before, Before);
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::GROUP)
		{
			EXPECT_TRUE(Entry.m_Ambiguous);
			EXPECT_FALSE(Entry.m_Positional);
		}
	Before = Map();
	Before.m_vGroups[0].m_vLayers[0].m_vObjects = {Object(0), Object(1), Object(2)};
	After = Before;
	After.m_vGroups[0].m_vLayers[0].m_vObjects[1] = Object(10);
	Result = MapDiff::Compare(Before, After);
	ASSERT_EQ(LayerEntry(Result).m_vObjects.size(), 1u);
	EXPECT_TRUE(LayerEntry(Result).m_vObjects[0].m_Positional);
	EXPECT_TRUE(LayerEntry(Result).m_vObjects[0].m_Ambiguous);
	EXPECT_EQ(LayerEntry(Result).m_vObjects[0].m_Before, 1);
	EXPECT_EQ(LayerEntry(Result).m_vObjects[0].m_After, 1);
}

TEST(MapDiff, ResourceRenumberingAndContent)
{
	auto Before = Map();
	Before.m_vImages = {{"one", {}, {1, 2, 3}}, {"two", {}, {4, 5, 6}}};
	for(auto &Resource : Before.m_vImages)
		Resource.m_ContentKey = MapDiff::ResourceContentKey(Resource);
	Before.m_vGroups[0].m_vLayers[0].m_vProperties = {{"Image", Before.m_vImages[0].m_ContentKey}};
	auto After = Before;
	std::swap(After.m_vImages[0], After.m_vImages[1]);
	EXPECT_EQ(MapDiff::Compare(Before, After).ChangeCount(), 0u);
	After.m_vImages[1].m_vData[0]++;
	After.m_vImages[1].m_ContentKey = MapDiff::ResourceContentKey(After.m_vImages[1]);
	After.m_vGroups[0].m_vLayers[0].m_vProperties[0].m_Value = After.m_vImages[1].m_ContentKey;
	auto Result = MapDiff::Compare(Before, After);
	EXPECT_GT(Result.ChangeCount(), 0u);
	EXPECT_FALSE(LayerEntry(Result).m_vProperties.empty());
	EXPECT_TRUE(LayerEntry(Result).m_vTiles.empty());
}

TEST(MapDiff, MissingContentIsNotEqual)
{
	auto Before = Map();
	Before.m_vSounds.push_back({"missing", {}, {}, true});
	auto Result = MapDiff::Compare(Before, Before);
	EXPECT_GT(Result.ChangeCount(), 0u);
	EXPECT_TRUE(std::any_of(Result.m_vEntries.begin(), Result.m_vEntries.end(), [](const auto &Entry) { return !Entry.m_vIssues.empty(); }));
}

TEST(MapDiff, IdenticalResourcesKeepNamesWhenRenumbered)
{
	auto Before = Map();
	Before.m_vImages = {{"one", {}, {1, 2, 3}}, {"two", {}, {1, 2, 3}}};
	auto After = Before;
	std::swap(After.m_vImages[0], After.m_vImages[1]);
	EXPECT_EQ(MapDiff::Compare(Before, After).ChangeCount(), 0u);
}

TEST(MapDiff, IdenticalNamedLayersStillReportDrawOrder)
{
	auto Before = Map();
	auto &Layers = Before.m_vGroups[0].m_vLayers;
	Layers[0].m_Kind = MapDiff::ELayerKind::TILES;
	Layers[0].m_Name = "one";
	Layers.push_back(Layers[0]);
	Layers[1].m_Name = "two";
	auto After = Before;
	std::swap(After.m_vGroups[0].m_vLayers[0], After.m_vGroups[0].m_vLayers[1]);
	auto Result = MapDiff::Compare(Before, After);
	int Reordered = 0;
	for(const auto &Entry : Result.m_vEntries)
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER)
		{
			EXPECT_TRUE(Entry.m_vProperties.empty());
			EXPECT_TRUE(Entry.m_vTiles.empty());
			Reordered += Entry.m_Reordered;
		}
	EXPECT_EQ(Reordered, 2);
}

TEST(MapDiff, MetadataAndDuplicateOrderedCommands)
{
	auto Before = Map();
	Before.m_vSettings = {"tune gravity 0.5", "tune gravity 1", "tune gravity 0.5"};
	auto After = Before;
	After.m_vInfo[0].m_Value = "Another author";
	std::swap(After.m_vSettings[1], After.m_vSettings[2]);
	auto Result = MapDiff::Compare(Before, After);
	EXPECT_EQ(Result.m_vEntries[0].m_vProperties.size(), 1u);
	EXPECT_EQ(Result.m_vEntries[1].m_vProperties.size(), 2u);
	Before.m_vSettings = {"(absent)"};
	After.m_vSettings.clear();
	EXPECT_EQ(MapDiff::Compare(Before, After).m_vEntries[1].m_vProperties.size(), 1u);
}

TEST(MapDiff, SettingsAlignmentPreservesLinesAndAbsence)
{
	auto Before = Map();
	Before.m_vSettings = {"prefix", "repeat", "", "repeat", "(absent)", "suffix"};
	auto After = Before;
	After.m_vSettings.insert(After.m_vSettings.begin() + 1, "inserted");
	After.m_vSettings[3] = "replacement";
	After.m_vSettings.erase(After.m_vSettings.begin() + 5);
	const auto Result = MapDiff::Compare(Before, After);
	const auto &Entry = Result.m_vEntries[1];
	ASSERT_EQ(Entry.m_vSettings.size(), 7u);
	EXPECT_EQ(Entry.m_vProperties.size(), 3u);
	EXPECT_FALSE(Entry.m_SettingsConservative);
	const int aBefore[] = {0, -1, 1, 2, 3, 4, 5};
	const int aAfter[] = {0, 1, 2, 3, 4, -1, 5};
	for(int i = 0; i < 7; ++i)
	{
		EXPECT_EQ(Entry.m_vSettings[i].m_Before, aBefore[i]);
		EXPECT_EQ(Entry.m_vSettings[i].m_After, aAfter[i]);
		EXPECT_EQ(Entry.m_vSettings[i].m_Property >= 0, i == 1 || i == 3 || i == 5);
	}
	const auto Reverse = MapDiff::Compare(After, Before);
	EXPECT_EQ(Reverse.m_vEntries[1].m_vProperties.size(), 3u);
	Before.m_vSettings = {"", "(absent)"};
	After.m_vSettings.clear();
	const auto Removed = MapDiff::Compare(Before, After);
	ASSERT_EQ(Removed.m_vEntries[1].m_vSettings.size(), 2u);
	EXPECT_EQ(Removed.m_vEntries[1].m_vSettings[0].m_Before, 0);
	EXPECT_EQ(Removed.m_vEntries[1].m_vSettings[1].m_After, -1);
	EXPECT_EQ(Removed.m_vEntries[1].m_vProperties[0].m_Before, "");
}

TEST(MapDiff, SettingsAlignmentBudgetAndCancellation)
{
	auto Before = Map(), After = Map();
	Before.m_vSettings.assign(4000, "before");
	After.m_vSettings.assign(4000, "after");
	Before.m_vSettings.front() = After.m_vSettings.front() = "prefix";
	Before.m_vSettings.back() = After.m_vSettings.back() = "suffix";
	const auto Result = MapDiff::Compare(Before, After);
	const auto &Entry = Result.m_vEntries[1];
	EXPECT_TRUE(Entry.m_SettingsConservative);
	EXPECT_EQ(Entry.m_vSettings.size(), 4000u);
	EXPECT_EQ(Entry.m_vProperties.size(), 3998u);
	EXPECT_EQ(Entry.m_vSettings.front().m_Property, -1);
	EXPECT_EQ(Entry.m_vSettings.back().m_Property, -1);
	int Calls = 0;
	const auto Cancelled = MapDiff::Compare(Before, After, [&] { return ++Calls == 3; });
	EXPECT_TRUE(Cancelled.m_Cancelled);
	EXPECT_TRUE(Cancelled.m_vEntries.empty());
	Before.m_vSettings.resize(900);
	After.m_vSettings.resize(900);
	Calls = 0;
	const auto CancelledLcs = MapDiff::Compare(Before, After, [&] { return ++Calls == 10; });
	EXPECT_TRUE(CancelledLcs.m_Cancelled);
	EXPECT_TRUE(CancelledLcs.m_vEntries.empty());
	Before.m_vSettings.assign(50000, "context");
	After = Before;
	After.m_vSettings.insert(After.m_vSettings.begin() + 25000, "inserted");
	const auto Large = MapDiff::Compare(Before, After);
	EXPECT_EQ(Large.m_vEntries[1].m_vSettings.size(), 50001u);
	EXPECT_EQ(Large.m_vEntries[1].m_vProperties.size(), 1u);
	EXPECT_EQ(Large.m_vEntries[1].m_vSettings[25000].m_Before, -1);
}

TEST(MapDiff, SettingsInlineByteRanges)
{
	struct CCase
	{
		std::string m_Before, m_After;
		std::vector<std::string> m_aChanged[2];
	};
	const CCase aCases[] = {
		{"sv_pauseable 1", "sv_pauseable 0", {{"1"}, {"0"}}},
		{"cmd A keep B tail", "cmd X keep Y tail", {{"A", "B"}, {"X", "Y"}}},
		{"sv_name café 世界🙂 fin", "sv_name cafè 世間🙃 fin", {{"é", "界🙂"}, {"è", "間🙃"}}},
		{"same🙂", "same🙂", {{}, {}}},
		{"cmd tail", "cmd new tail", {{}, {"new "}}},
		{"cmd old tail", "cmd tail", {{"old "}, {}}},
		{"", "世🙂", {{}, {"世🙂"}}},
		{"世🙂", "", {{"世🙂"}, {}}},
		{"", "", {{}, {}}},
		{"\xE1\x80x", "\xE1\x81y", {{"\x80x"}, {"\x81y"}}},
	};
	for(const auto &Case : aCases)
	{
		auto Before = Map(), After = Map();
		Before.m_vSettings = {Case.m_Before};
		After.m_vSettings = {Case.m_After};
		const auto Result = MapDiff::Compare(Before, After);
		ASSERT_EQ(Result.m_vEntries[1].m_vSettings.size(), 1u);
		const auto &Row = Result.m_vEntries[1].m_vSettings[0];
		std::string aNeutral[2];
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto &Text = Side ? Case.m_After : Case.m_Before;
			SCOPED_TRACE(Text);
			ASSERT_EQ(Row.m_avChanged[Side].size(), Case.m_aChanged[Side].size());
			size_t End = 0;
			for(size_t i = 0; i < Row.m_avChanged[Side].size(); ++i)
			{
				const auto &Range = Row.m_avChanged[Side][i];
				ASSERT_GE(Range.m_Start, End);
				ASSERT_GT(Range.m_End, Range.m_Start);
				ASSERT_LE(Range.m_End, Text.size());
				EXPECT_EQ(Text.substr(Range.m_Start, Range.m_End - Range.m_Start), Case.m_aChanged[Side][i]);
				EXPECT_EQ(Range.m_Start, Text.find(Case.m_aChanged[Side][i], End));
				aNeutral[Side] += Text.substr(End, Range.m_Start - End);
				End = Range.m_End;
			}
			aNeutral[Side] += Text.substr(End);
		}
		EXPECT_EQ(aNeutral[0], aNeutral[1]);
	}
	// An absent row is distinct from a present empty command even when neither
	// contains a glyph that can be highlighted.
	auto Before = Map(), After = Map();
	Before.m_vSettings = {"", "(absent)", "世🙂"};
	const auto Removed = MapDiff::Compare(Before, After);
	const auto Added = MapDiff::Compare(After, Before);
	for(int i = 0; i < 3; ++i)
	{
		const auto &Old = Removed.m_vEntries[1].m_vSettings[i];
		const auto &New = Added.m_vEntries[1].m_vSettings[i];
		EXPECT_EQ(Old.m_After, -1);
		EXPECT_EQ(New.m_Before, -1);
		EXPECT_GE(Old.m_Property, 0);
		EXPECT_GE(New.m_Property, 0);
		EXPECT_EQ(Old.m_avChanged[0].size(), i ? 1u : 0u);
		EXPECT_EQ(New.m_avChanged[1].size(), i ? 1u : 0u);
		if(i)
		{
			EXPECT_EQ(Old.m_avChanged[0][0].m_Start, 0);
			EXPECT_EQ(Old.m_avChanged[0][0].m_End, Before.m_vSettings[i].size());
		}
	}
}

TEST(MapDiff, SettingsInlineAggregateBudgetAndLongLineCancellation)
{
	auto Before = Map(), After = Map();
	const std::string Old = "prefix " + std::string(100, 'A') + " keep " + std::string(100, 'B') + " suffix";
	const std::string New = "prefix " + std::string(100, 'X') + " keep " + std::string(100, 'Y') + " suffix";
	Before.m_vSettings.assign(2000, Old);
	After.m_vSettings.assign(2000, New);
	int Polls = 0;
	const auto Result = MapDiff::Compare(Before, After, [&] { ++Polls; return false; });
	const auto &Rows = Result.m_vEntries[1].m_vSettings;
	ASSERT_EQ(Rows.size(), 2000u);
	EXPECT_EQ(Rows.front().m_avChanged[0].size(), 2u);
	ASSERT_EQ(Rows.back().m_avChanged[0].size(), 1u);
	EXPECT_EQ(Rows.back().m_avChanged[0][0].m_Start, 7);
	EXPECT_EQ(Rows.back().m_avChanged[0][0].m_End, Old.size() - 7);
	EXPECT_LT(Polls, 5000); // A whole-result budget, not a fresh budget per row.
	Before.m_vSettings = {"prefix " + std::string(100000, 'A') + " suffix"};
	After.m_vSettings = {"prefix " + std::string(100000, 'X') + " suffix"};
	const auto Long = MapDiff::Compare(Before, After);
	ASSERT_EQ(Long.m_vEntries[1].m_vSettings[0].m_avChanged[0].size(), 1u);
	EXPECT_EQ(Long.m_vEntries[1].m_vSettings[0].m_avChanged[0][0].m_Start, 7);
	EXPECT_EQ(Long.m_vEntries[1].m_vSettings[0].m_avChanged[0][0].m_End, 100007);
	Before.m_vSettings = {std::string(100000, 'A') + "1"};
	After.m_vSettings = {std::string(100000, 'A') + "0"};
	Polls = 0;
	const auto Cancelled = MapDiff::Compare(Before, After, [&] { return ++Polls == 8; });
	EXPECT_TRUE(Cancelled.m_Cancelled);
	EXPECT_TRUE(Cancelled.m_vEntries.empty());
	Before.m_vSettings.assign(2000, "\xE1\x80x");
	After.m_vSettings.assign(2000, "\xE1\x81y");
	Polls = 0;
	const auto CancelledMalformed = MapDiff::Compare(Before, After, [&] { return ++Polls == 8; });
	EXPECT_TRUE(CancelledMalformed.m_Cancelled);
	EXPECT_TRUE(CancelledMalformed.m_vEntries.empty());
}

TEST(MapDiff, GroupAdditionRemovalPropertiesAndOrder)
{
	auto Before = Map();
	Before.m_vGroups.push_back({"Background", false, {{"Parallax X", "50"}}});
	auto After = Before;
	std::swap(After.m_vGroups[0], After.m_vGroups[1]);
	After.m_vGroups[0].m_vProperties[0].m_Value = "25";
	After.m_vGroups.push_back({"Added", false});
	auto Result = MapDiff::Compare(Before, After);
	EXPECT_TRUE(std::any_of(Result.m_vEntries.begin(), Result.m_vEntries.end(), [](const auto &Entry) { return Entry.m_Kind == MapDiff::EEntryKind::GROUP && Entry.m_Reordered; }));
	EXPECT_TRUE(std::any_of(Result.m_vEntries.begin(), Result.m_vEntries.end(), [](const auto &Entry) { return Entry.m_Kind == MapDiff::EEntryKind::GROUP && Entry.m_Before < 0; }));
	EXPECT_GT(MapDiff::Compare(After, Before).ChangeCount(), 0u);
}

TEST(MapDiff, SparseLargeMapAndCancellation)
{
	auto Before = Map();
	Before.m_vGroups[0].m_vLayers[0].m_Width = Before.m_vGroups[0].m_vLayers[0].m_Height = 100000;
	auto After = Before;
	After.m_vGroups[0].m_vLayers[0].m_vCells.push_back({99999, 99999, {8}});
	auto Result = MapDiff::Compare(Before, After);
	ASSERT_EQ(LayerEntry(Result).m_vTiles.size(), 1u);
	EXPECT_EQ(LayerEntry(Result).m_vTiles[0].m_X, 99999);
	Result = MapDiff::Compare(Before, After, [] { return true; });
	EXPECT_TRUE(Result.m_Cancelled);
	EXPECT_TRUE(Result.m_vEntries.empty());
}

TEST(MapDiff, ExactTilePayloadsAndReverseDirection)
{
	auto Before = Map();
	auto &Cells = Before.m_vGroups[0].m_vLayers[0].m_vCells;
	const MapDiff::CTile Old{1, 2, 3, 4, 5, 6, -70}, New{8, 4, 9, 10, 11, 12, 130};
	Cells = {{2, 3, Old}, {7, 9, New}};
	auto After = Before;
	After.m_vGroups[0].m_vLayers[0].m_vCells = {{2, 3, New}, {6, 8, Old}};
	for(int Reverse = 0; Reverse < 2; ++Reverse)
	{
		auto Result = Reverse ? MapDiff::Compare(After, Before) : MapDiff::Compare(Before, After);
		const auto &Changes = LayerEntry(Result).m_vTiles;
		ASSERT_EQ(Changes.size(), 3u);
		EXPECT_EQ(Changes[0].m_X, 2);
		EXPECT_EQ(Changes[0].m_Y, 3);
		EXPECT_TRUE(Changes[0].m_Before == (Reverse ? New : Old));
		EXPECT_TRUE(Changes[0].m_After == (Reverse ? Old : New));
		EXPECT_EQ(Changes[1].m_X, 6);
		EXPECT_EQ(Changes[1].m_Y, 8);
		EXPECT_TRUE(Changes[1].m_Before == (Reverse ? Old : MapDiff::CTile{}));
		EXPECT_TRUE(Changes[1].m_After == (Reverse ? MapDiff::CTile{} : Old));
		EXPECT_EQ(Changes[2].m_X, 7);
		EXPECT_EQ(Changes[2].m_Y, 9);
		EXPECT_TRUE(Changes[2].m_Before == (Reverse ? MapDiff::CTile{} : New));
		EXPECT_TRUE(Changes[2].m_After == (Reverse ? New : MapDiff::CTile{}));
	}
}

TEST(MapDiff, CancellationAfterHashingAndEqualityBegin)
{
	auto Before = Map();
	Before.m_vImages = {{"large", {}, std::vector<uint8_t>(4 * 1024 * 1024, 42)}};
	auto After = Before;
	int Calls = 0;
	EXPECT_TRUE(MapDiff::Compare(Before, After, [&] { return ++Calls == 5; }).m_Cancelled);
	EXPECT_EQ(Calls, 5); // Cancellation during chunked resource hashing, after entry.
	for(auto *pMap : {&Before, &After})
		pMap->m_vImages[0].m_ContentKey = MapDiff::ResourceContentKey(pMap->m_vImages[0]);
	Calls = 0;
	EXPECT_TRUE(MapDiff::Compare(Before, After, [&] { return ++Calls == 8; }).m_Cancelled);
	EXPECT_EQ(Calls, 8); // Cached hashes: cancellation during exact resource bytes.
	Before.m_vImages.clear();
	auto &Objects = Before.m_vGroups[0].m_vLayers[0].m_vObjects;
	Objects.assign(12000, Object(42));
	After = Before;
	Calls = 0;
	const auto Complete = MapDiff::Compare(Before, After, [&] { ++Calls; return false; });
	EXPECT_FALSE(Complete.m_Cancelled);
	const int CancelAt = Calls - 10;
	ASSERT_GT(CancelAt, 1);
	Calls = 0;
	auto Cancelled = MapDiff::Compare(Before, After, [&] { return ++Calls >= CancelAt; });
	EXPECT_TRUE(Cancelled.m_Cancelled);
	EXPECT_TRUE(Cancelled.m_vEntries.empty());
}

TEST(MapDiff, EnvelopeAndSoundSourceProperties)
{
	auto Before = Map();
	Before.m_vEnvelopes.push_back({"movement", {{"Channels", "3"}, {"Point 0: out dx", "10"}}});
	Before.m_vGroups[0].m_vLayers[0].m_Kind = MapDiff::ELayerKind::SOUNDS;
	Before.m_vGroups[0].m_vLayers[0].m_vObjects = {Object(10)};
	auto After = Before;
	After.m_vEnvelopes[0].m_vProperties[1].m_Value = "20";
	After.m_vGroups[0].m_vLayers[0].m_vObjects[0].m_vProperties[0].m_Value = "12";
	auto Result = MapDiff::Compare(Before, After);
	EXPECT_EQ(LayerEntry(Result).m_vObjects.size(), 1u);
	EXPECT_GT(Result.ChangeCount(), 1u);
}
