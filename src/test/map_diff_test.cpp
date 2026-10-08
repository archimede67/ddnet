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
