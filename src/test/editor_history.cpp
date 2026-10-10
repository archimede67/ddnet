#include "test.h"

#include <base/hash.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/editor/history/edit_coordinator.h>
#include <game/editor/history/revision_history.h>
#include <game/editor/history/save_queue.h>
#include <game/editor/history/tile_plane.h>
#include <game/editor/history/tile_projection.h>
#include <game/editor/mapitems/document_export.h>
#include <game/editor/mapitems/document_graph.h>
#include <game/editor/mapitems/document_session.h>
#include <game/editor/mapitems/document_values.h>
#include <game/editor/mapitems/envelope.h>
#include <game/map/render_map.h>
#include <game/mapitems_ex.h>

#include <gtest/gtest.h>

#include <array>
#include <future>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace editor_history;

namespace
{

	class CTestTile
	{
	public:
		int m_Index = 0;
		int m_Flags = 0;
		bool operator==(const CTestTile &) const = default;
	};

	using CTestPlane = CTilePlane<CTestTile, 16>;

	class CTestDocument
	{
	public:
		int m_Value = 64;
		int m_AuthoringSetting = 0;
		CTestPlane m_Tiles = *CTestPlane::Create(64, 64);
		CSharedVector<unsigned char> m_Resource{std::vector<unsigned char>(4096, 42)};
		bool operator==(const CTestDocument &) const = default;

		void Account(CStorageUsage &Usage) const
		{
			m_Tiles.Account(Usage);
			m_Resource.Account(Usage);
		}
	};

	using CTestHistory = CRevisionHistory<CTestDocument>;

	std::unique_ptr<CTestHistory> MakeHistory(CRetentionLimits Limits = {100, 64 * 1024 * 1024})
	{
		return CTestHistory::Create(CTestDocument{}, Limits, [](const auto &Document) { return Document.m_Value >= 0; }, [](const auto &Document) {
			// Test-specific projection: real .map projection is an adapter concern.
			std::string Key = std::to_string(Document.m_Value);
			for(std::size_t Y = 0; Y < Document.m_Tiles.Height(); ++Y)
				for(std::size_t X = 0; X < Document.m_Tiles.Width(); ++X)
				{
					Key += std::to_string(Document.m_Tiles.At(X, Y).m_Index);
					Key += ',';
					Key += std::to_string(Document.m_Tiles.At(X, Y).m_Flags);
					Key += ';';
				}
			Key.append(Document.m_Resource.Read().begin(), Document.m_Resource.Read().end());
			const auto Digest = sha256(Key.data(), Key.size());
			return std::string(reinterpret_cast<const char *>(Digest.data), sizeof(Digest.data)); }, [](const auto &Document, auto &Usage) { Document.Account(Usage); });
	}

	void SetValue(CTestHistory &History, int Value, ECategory Category = ECategory::MAP)
	{
		auto Edit = History.BeginEdit("Set value", Category);
		ASSERT_TRUE(Edit);
		ASSERT_TRUE(History.Update(*Edit, [&](auto &Document) { Document.m_Value = Value; }));
		ASSERT_EQ(History.Commit(*Edit), ECommitResult::CHANGED);
	}

	void Undo(CTestHistory &History)
	{
		ASSERT_TRUE(History.RequestUndo());
		ASSERT_TRUE(History.PublishPending());
	}

	void Redo(CTestHistory &History)
	{
		ASSERT_TRUE(History.RequestRedo());
		ASSERT_TRUE(History.PublishPending());
	}

} // namespace

TEST(EditorHistoryStorage, SharedValuesCompareContentsAndDetach)
{
	CSharedVector<int> Original(std::vector<int>{1, 2, 3});
	auto Draft = Original;
	EXPECT_TRUE(Draft.SharesStorageWith(Original));
	Draft.Update([](auto &vValues) { vValues[1] = 7; });
	EXPECT_FALSE(Draft.SharesStorageWith(Original));
	EXPECT_EQ(Original.Read()[1], 2);
	EXPECT_EQ(Draft.Read()[1], 7);
	Draft.Update([](auto &vValues) { vValues[1] = 2; });
	EXPECT_EQ(Draft, Original);
	CStorageUsage Usage;
	EXPECT_TRUE(Original.Account(Usage));
	const auto Bytes = Usage.Bytes();
	auto Shared = Original;
	EXPECT_FALSE(Shared.Account(Usage));
	EXPECT_EQ(Usage.Bytes(), Bytes);
}

TEST(EditorHistory, FailedRuntimePublicationPreservesCursorAndCanBeRetried)
{
	auto History = MakeHistory();
	SetValue(*History, 80);
	SetValue(*History, 90);
	ASSERT_TRUE(History->RequestUndo());
	int PublishedValue = 90;
	EXPECT_FALSE(History->PublishPendingWith([&](const auto &Document) {
		EXPECT_EQ(Document.m_Value, 80);
		EXPECT_EQ(History->Current().m_Value, 90);
		return false;
	}));
	EXPECT_EQ(History->Cursor(), 2u);
	EXPECT_EQ(History->Current().m_Value, 90);
	EXPECT_EQ(PublishedValue, 90);
	EXPECT_TRUE(History->PublishPendingWith([&](const auto &Document) {
		PublishedValue = Document.m_Value;
		return true;
	}));
	EXPECT_EQ(History->Cursor(), 1u);
	EXPECT_EQ(History->Current().m_Value, 80);
	EXPECT_EQ(PublishedValue, 80);
	EXPECT_TRUE(History->CanRedo());
}

TEST(EditorHistory, CancellationRetainsDraftUntilRuntimeHasPublishedTheBaseline)
{
	auto History = MakeHistory();
	SetValue(*History, 80);
	SetValue(*History, 90);
	Undo(*History);
	auto Edit = History->BeginEdit("Gesture", ECategory::MAP);
	ASSERT_TRUE(Edit);
	ASSERT_TRUE(History->Update(*Edit, [](auto &Document) { Document.m_Value = 128; }));
	ASSERT_TRUE(History->RequestCancel());
	EXPECT_FALSE(History->PublishPendingWith([&](const auto &Document) {
		EXPECT_EQ(Document.m_Value, 80);
		EXPECT_TRUE(History->HasActiveEdit());
		EXPECT_EQ(History->Preview().m_Value, 128);
		return false;
	}));
	EXPECT_TRUE(History->HasActiveEdit());
	EXPECT_TRUE(History->PublishPendingWith([&](const auto &Document) {
		EXPECT_EQ(Document.m_Value, 80);
		EXPECT_EQ(History->Preview().m_Value, 128);
		return true;
	}));
	EXPECT_FALSE(History->HasActiveEdit());
	EXPECT_EQ(History->Cursor(), 1u);
	EXPECT_EQ(History->Preview().m_Value, 80);
	EXPECT_TRUE(History->CanRedo());
	EXPECT_FALSE(History->RequestCancel());
}

TEST(EditorHistorySchema, CommonLayerAndGroupValuesHaveSemanticNames)
{
	CLayerValues Layer;
	Layer.m_Type = LAYERTYPE_QUADS;
	Layer.m_Flags = LAYERFLAG_DETAIL;
	str_copy(Layer.m_aName, "foreground");
	const auto Original = Layer;
	Layer.m_aName[11] = 'x'; // Unused text capacity is not document content.
	EXPECT_EQ(Layer, Original);
	Layer.m_Type = LAYERTYPE_SOUNDS;
	EXPECT_NE(Layer, Original);
	EXPECT_EQ(Original.m_Type, LAYERTYPE_QUADS);
	EXPECT_EQ(Original.m_Flags, LAYERFLAG_DETAIL);
	EXPECT_STREQ(Original.m_aName, "foreground");

	CLayerGroupValues Group;
	str_copy(Group.m_aName, "game");
	Group.m_OffsetX = 127;
	Group.m_OffsetY = -71;
	Group.m_ParallaxX = 85;
	Group.m_ParallaxY = 113;
	Group.m_UseClipping = 1;
	Group.m_ClipX = 19;
	Group.m_ClipY = -23;
	Group.m_ClipW = 347;
	Group.m_ClipH = 251;
	Group.m_GameGroup = true;
	const auto Saved = Group;
	Group = {};
	EXPECT_NE(Group, Saved);
	Group = Saved;
	EXPECT_STREQ(Group.m_aName, "game");
	EXPECT_EQ(Group.m_OffsetX, 127);
	EXPECT_EQ(Group.m_OffsetY, -71);
	EXPECT_EQ(Group.m_ParallaxX, 85);
	EXPECT_EQ(Group.m_ParallaxY, 113);
	EXPECT_EQ(Group.m_UseClipping, 1);
	EXPECT_EQ(Group.m_ClipX, 19);
	EXPECT_EQ(Group.m_ClipY, -23);
	EXPECT_EQ(Group.m_ClipW, 347);
	EXPECT_EQ(Group.m_ClipH, 251);
	EXPECT_TRUE(Group.m_GameGroup);
}

TEST(EditorHistorySchema, TilePropertiesAndAuthoringValuesAreCopiedTogether)
{
	CLayerTilesValues Tiles;
	Tiles.m_HasGame = true;
	Tiles.m_Image = {17};
	ASSERT_TRUE(Tiles.m_Tiles.Resize(513, 257));
	Tiles.m_Color = {7, 31, 127, 191};
	Tiles.m_ColorEnv = {5};
	Tiles.m_ColorEnvOffset = 29;
	Tiles.m_FillGameTile = 3;
	Tiles.m_LiveGameTiles = true;
	Tiles.m_AutomapperConfig = 9;
	Tiles.m_AutomapperReference = 2;
	Tiles.m_Seed = 123456;
	Tiles.m_AutoAutomapper = true;
	Tiles.m_HasTele = true;
	Tiles.m_HasSpeedup = true;
	Tiles.m_HasFront = true;
	Tiles.m_HasSwitch = true;
	Tiles.m_HasTune = true;
	const auto Saved = Tiles;
	Tiles = {};
	EXPECT_NE(Tiles, Saved);
	Tiles = Saved;
	EXPECT_TRUE(Tiles.m_HasGame);
	EXPECT_EQ(Tiles.m_Image.m_Id, 17u);
	EXPECT_EQ(Tiles.Width(), 513);
	EXPECT_EQ(Tiles.Height(), 257);
	EXPECT_EQ(Tiles.m_Color, (CColor{7, 31, 127, 191}));
	EXPECT_EQ(Tiles.m_ColorEnv.m_Id, 5u);
	EXPECT_EQ(Tiles.m_ColorEnvOffset, 29);
	EXPECT_EQ(Tiles.m_FillGameTile, 3);
	EXPECT_TRUE(Tiles.m_LiveGameTiles);
	EXPECT_EQ(Tiles.m_AutomapperConfig, 9);
	EXPECT_EQ(Tiles.m_AutomapperReference, 2);
	EXPECT_EQ(Tiles.m_Seed, 123456);
	EXPECT_TRUE(Tiles.m_AutoAutomapper);
	EXPECT_TRUE(Tiles.m_HasTele);
	EXPECT_TRUE(Tiles.m_HasSpeedup);
	EXPECT_TRUE(Tiles.m_HasFront);
	EXPECT_TRUE(Tiles.m_HasSwitch);
	EXPECT_TRUE(Tiles.m_HasTune);
}

TEST(EditorHistorySchema, ResourceValuesOwnImmutableBytesAndDistinguishMissingData)
{
	std::vector<std::uint8_t> vInput{1, 2, 3, 4, 5, 6, 7, 8};
	CEditorImageValues Image;
	str_copy(Image.m_aName, "external");
	Image.m_External = 1;
	Image.m_Width = 2;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_Content.emplace(vInput);
	const auto SavedImage = Image;
	EXPECT_TRUE(Image.m_Content->SharesStorageWith(*SavedImage.m_Content));
	vInput.assign(8, 0);
	EXPECT_EQ(Image.DataSize(), 8);
	EXPECT_EQ(Image.Data()[7], 8);
	Image.m_Content.emplace(vInput);
	EXPECT_NE(Image, SavedImage);
	EXPECT_EQ(SavedImage.Data()[0], 1);
	Image = SavedImage;
	EXPECT_STREQ(Image.m_aName, "external");
	EXPECT_EQ(Image.m_External, 1);
	EXPECT_EQ(Image.m_Width, 2);
	EXPECT_EQ(Image.m_Height, 1);
	EXPECT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(Image, SavedImage);

	CEditorSoundValues Sound;
	str_copy(Sound.m_aName, "ambient");
	EXPECT_EQ(Sound.Data(), nullptr);
	EXPECT_FALSE(Sound.m_Content);
	Sound.m_Content.emplace(std::span<const std::uint8_t>{});
	EXPECT_TRUE(Sound.m_Content);
	EXPECT_EQ(Sound.DataSize(), 0);
	Sound.m_Content = SavedImage.m_Content;
	const auto SavedSound = Sound;
	Sound.m_Content.reset();
	EXPECT_NE(Sound, SavedSound);
	Sound = SavedSound;
	EXPECT_STREQ(Sound.m_aName, "ambient");
	EXPECT_EQ(Sound.DataSize(), 8);
	EXPECT_EQ(Sound.Data()[0], 1);
	CStorageUsage Usage;
	SavedImage.m_Content->Account(Usage);
	const auto Bytes = Usage.Bytes();
	Sound.m_Content->Account(Usage);
	EXPECT_EQ(Usage.Bytes(), Bytes);
}

TEST(EditorHistoryStorage, SparseEditCopiesOnlyTouchedChunks)
{
	auto Baseline = *CTestPlane::Create(1024, 1024);
	auto Draft = Baseline;
	Draft.Set(17, 19, {3, 4});
	Draft.Set(18, 19, {5, 6});
	EXPECT_EQ(Draft.SharedChunks(Baseline), Baseline.ChunkCount() - 1);
	EXPECT_EQ(Baseline.At(17, 19), CTestTile{});
	EXPECT_EQ(Draft.At(17, 19), (CTestTile{3, 4}));
	auto Frozen = Draft;
	Draft.Set(17, 19, {7, 8});
	EXPECT_EQ(Frozen.At(17, 19), (CTestTile{3, 4}));
}

TEST(EditorHistoryStorage, ResizePreservesCoordinatesAndClearsRemovedCells)
{
	auto Plane = *CTestPlane::Create(35, 21);
	Plane.Set(4, 5, {11, 2});
	Plane.Set(34, 20, {27, 3});
	const auto Baseline = Plane;
	ASSERT_TRUE(Plane.Resize(65, 40));
	EXPECT_EQ(Plane.At(34, 20), (CTestTile{27, 3}));
	EXPECT_EQ(Plane.At(35, 20), CTestTile{});
	EXPECT_EQ(Plane.SharedChunks(Baseline), Baseline.ChunkCount());
	ASSERT_TRUE(Plane.Resize(20, 18));
	ASSERT_TRUE(Plane.Resize(65, 40));
	EXPECT_EQ(Plane.At(4, 5), (CTestTile{11, 2}));
	EXPECT_EQ(Plane.At(34, 20), CTestTile{});
	EXPECT_EQ(Baseline.At(34, 20), (CTestTile{27, 3}));
}

TEST(EditorHistoryStorage, OverflowDoesNotModifyPlane)
{
	auto Plane = *CTestPlane::Create(32, 32);
	Plane.Set(1, 2, {5, 6});
	const auto Baseline = Plane;
	EXPECT_FALSE(CTestPlane::Create(std::numeric_limits<std::size_t>::max(), 1));
	EXPECT_FALSE(Plane.Resize(1, std::numeric_limits<std::size_t>::max()));
	EXPECT_EQ(Plane, Baseline);
}

TEST(EditorHistoryStorage, AbiProjectionOnlyRefreshesChangedChunks)
{
	auto Plane = *CTestPlane::Create(35, 21);
	CTileProjection<CTestTile, int, 16> Projection;
	const auto Project = [](const CTestTile &Tile) { return Tile.m_Index * 10 + Tile.m_Flags; };
	EXPECT_EQ(Projection.Refresh(Plane, Project), 6);
	EXPECT_EQ(Projection.Refresh(Plane, Project), 0);
	Plane.Set(34, 20, {7, 3});
	EXPECT_EQ(Projection.Refresh(Plane, Project), 1);
	EXPECT_EQ(Projection.Cells()[20 * 35 + 34], 73);
	Plane.Set(34, 20, {8, 2});
	EXPECT_EQ(Projection.Refresh(Plane, Project), 1);
	EXPECT_EQ(Projection.Cells()[20 * 35 + 34], 82);
	const auto BeforeExport = Plane;
	Projection.Invalidate();
	EXPECT_EQ(Projection.Refresh(Plane, Project), 6);
	EXPECT_EQ(Plane, BeforeExport);
	ASSERT_TRUE(Plane.Resize(65, 22));
	EXPECT_EQ(Projection.Refresh(Plane, Project), 10);
	EXPECT_EQ(Projection.Cells().size(), 65 * 22);
	EXPECT_EQ(Projection.Cells()[20 * 65 + 34], 82);
	EXPECT_EQ(Projection.Cells()[21 * 65 + 64], 0);
}

TEST(EditorHistory, GestureGroupingDoesNotDependOnEventCount)
{
	for(const int Samples : {1, 10, 300})
	{
		auto pHistory = MakeHistory();
		auto Edit = pHistory->BeginEdit("Drag value", ECategory::MAP);
		ASSERT_TRUE(Edit);
		for(int Sample = 1; Sample <= Samples; ++Sample)
			ASSERT_TRUE(pHistory->Update(*Edit, [&](auto &Document) { Document.m_Value = 64 + 16 * Sample / Samples; }));
		EXPECT_EQ(pHistory->Current().m_Value, 64);
		EXPECT_EQ(pHistory->Preview().m_Value, 80);
		ASSERT_EQ(pHistory->Commit(*Edit), ECommitResult::CHANGED);
		EXPECT_EQ(pHistory->Revisions().size(), 2);
		EXPECT_EQ(pHistory->Current().m_Value, 80);
		Undo(*pHistory);
		EXPECT_EQ(pHistory->Current().m_Value, 64);
		Redo(*pHistory);
		EXPECT_EQ(pHistory->Current().m_Value, 80);
	}
}

TEST(EditorHistory, InvalidInitialDocumentIsRejectedInAllBuildModes)
{
	CTestDocument Invalid;
	Invalid.m_Value = -1;
	bool Validated = false;
	const auto pHistory = CTestHistory::Create(std::move(Invalid), {100, 1024 * 1024}, [&](const auto &Document) { Validated = true; return Document.m_Value >= 0; }, [](const auto &Document) { return std::to_string(Document.m_Value); }, [](const auto &Document, auto &Usage) { Document.Account(Usage); });
	EXPECT_TRUE(Validated);
	EXPECT_EQ(pHistory, nullptr);
}

TEST(EditorHistory, NoOpAndCancellationPreserveRedoAndSaveMarkers)
{
	auto pHistory = MakeHistory();
	auto Save = pHistory->CaptureSave("map.map", ESaveKind::MANUAL);
	ASSERT_TRUE(Save);
	ASSERT_TRUE(pHistory->CompleteSave(*Save, true));
	SetValue(*pHistory, 128);
	Undo(*pHistory);
	const auto Cursor = pHistory->Cursor();
	{
		auto Edit = pHistory->BeginEdit("Cancelled drag", ECategory::MAP);
		ASSERT_TRUE(Edit);
		pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = 99; });
	}
	EXPECT_EQ(pHistory->Current().m_Value, 64);
	EXPECT_EQ(pHistory->Cursor(), Cursor);
	EXPECT_TRUE(pHistory->CanRedo());
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	auto Edit = pHistory->BeginEdit("Drag back", ECategory::MAP);
	pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = 99; });
	pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = 64; });
	EXPECT_EQ(pHistory->Commit(*Edit), ECommitResult::UNCHANGED);
	EXPECT_EQ(pHistory->Revisions().size(), 2);
	EXPECT_EQ(pHistory->Cursor(), Cursor);
	EXPECT_TRUE(pHistory->CanRedo());
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
}

TEST(EditorHistory, FailedValidationPreservesCommittedRootAndFuture)
{
	auto pHistory = MakeHistory();
	SetValue(*pHistory, 128);
	Undo(*pHistory);
	const auto pBefore = pHistory->PinCurrent();
	const auto Bytes = pHistory->RetainedBytes();
	auto Edit = pHistory->BeginEdit("Invalid edit", ECategory::MAP);
	pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = -1; });
	EXPECT_EQ(pHistory->Commit(*Edit), ECommitResult::FAILED);
	EXPECT_EQ(pHistory->PinCurrent(), pBefore);
	EXPECT_EQ(pHistory->RetainedBytes(), Bytes);
	EXPECT_TRUE(pHistory->CanRedo());
	EXPECT_FALSE(pHistory->HasActiveEdit());
}

TEST(EditorHistory, OwnersCannotJoinAnotherDocumentOrExpiredEdit)
{
	auto pFirst = MakeHistory();
	auto pSecond = MakeHistory();
	auto Owner = pFirst->BeginEdit("Owner", ECategory::MAP);
	ASSERT_TRUE(Owner);
	EXPECT_FALSE(pFirst->BeginEdit("Unrelated widget", ECategory::MAP));
	EXPECT_FALSE(pSecond->Update(*Owner, [](auto &Document) { Document.m_Value = 42; }));
	EXPECT_EQ(pSecond->Commit(*Owner), ECommitResult::INVALID_OWNER);
	Owner->Cancel();
	EXPECT_FALSE(pFirst->Update(*Owner, [](auto &Document) { Document.m_Value = 42; }));
	EXPECT_EQ(pFirst->Commit(*Owner), ECommitResult::INVALID_OWNER);
	EXPECT_EQ(pFirst->Current().m_Value, 64);
}

TEST(EditorHistory, TokenCanOutliveDocument)
{
	auto pHistory = MakeHistory();
	auto Owner = pHistory->BeginEdit("Closing document", ECategory::MAP);
	pHistory.reset();
	Owner->Cancel();
	Owner.reset();
}

TEST(EditorHistory, NavigationWaitsForSafePublication)
{
	auto pHistory = MakeHistory();
	SetValue(*pHistory, 128);
	const auto pBorrowed = pHistory->PinCurrent();
	ASSERT_TRUE(pHistory->RequestUndo());
	EXPECT_EQ(pHistory->Current().m_Value, 128);
	EXPECT_FALSE(pHistory->BeginEdit("Pending publication", ECategory::MAP));
	ASSERT_TRUE(pHistory->PublishPending());
	EXPECT_EQ(pHistory->Current().m_Value, 64);
	EXPECT_EQ(pBorrowed->m_Value, 128);
}

TEST(EditorHistory, UndoCancelsActiveEditWithoutTraversing)
{
	auto pHistory = MakeHistory();
	SetValue(*pHistory, 128);
	auto Edit = pHistory->BeginEdit("Active drag", ECategory::MAP);
	pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = 150; });
	EXPECT_FALSE(pHistory->RequestRedo());
	ASSERT_TRUE(pHistory->RequestUndo());
	EXPECT_EQ(pHistory->Preview().m_Value, 150);
	EXPECT_FALSE(pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = 170; }));
	ASSERT_TRUE(pHistory->PublishPending());
	EXPECT_EQ(pHistory->Current().m_Value, 128);
	EXPECT_EQ(pHistory->Cursor(), 1);
	EXPECT_FALSE(pHistory->HasActiveEdit());
	Undo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Value, 64);
}

TEST(EditorHistory, CategoriesShareOneChronologicalTimeline)
{
	auto pHistory = MakeHistory();
	SetValue(*pHistory, 65, ECategory::MAP);
	SetValue(*pHistory, 66, ECategory::ENVELOPE);
	SetValue(*pHistory, 67, ECategory::SETTINGS);
	ASSERT_EQ(pHistory->Revisions().size(), 4);
	ASSERT_TRUE(pHistory->RequestRevision(1));
	ASSERT_TRUE(pHistory->PublishPending());
	EXPECT_EQ(pHistory->Current().m_Value, 65);
	Redo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Value, 66);
	EXPECT_EQ(pHistory->Revisions()[pHistory->Cursor()].m_Category, ECategory::ENVELOPE);
}

TEST(EditorHistory, ChangedCommitAloneDiscardsFuture)
{
	auto pHistory = MakeHistory();
	SetValue(*pHistory, 65);
	SetValue(*pHistory, 66);
	Undo(*pHistory);
	SetValue(*pHistory, 67);
	EXPECT_FALSE(pHistory->CanRedo());
	EXPECT_EQ(pHistory->Revisions().size(), 3);
	Undo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Value, 65);
	Redo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Value, 67);
}

TEST(EditorHistory, ObjectIdsAreNeverReusedAfterCancelUndoOrClear)
{
	auto pHistory = MakeHistory();
	const auto First = pHistory->AllocateObjectId();
	auto Edit = pHistory->BeginEdit("Cancelled creation", ECategory::MAP);
	const auto Cancelled = pHistory->AllocateObjectId();
	Edit->Cancel();
	SetValue(*pHistory, 65);
	Undo(*pHistory);
	ASSERT_TRUE(pHistory->Clear());
	const auto Last = pHistory->AllocateObjectId();
	ASSERT_TRUE(First);
	ASSERT_TRUE(Cancelled);
	ASSERT_TRUE(Last);
	EXPECT_LT(*First, *Cancelled);
	EXPECT_LT(*Cancelled, *Last);
}

TEST(EditorHistory, LoadedBaselineStartsCleanAndReservesImportedIdentities)
{
	auto pHistory = MakeHistory();
	ASSERT_TRUE(pHistory->InitializeLoadedBaseline(1000));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::AUTOMATIC));
	EXPECT_EQ(pHistory->AllocateObjectId(), 1001);
	SetValue(*pHistory, 65);
	EXPECT_FALSE(pHistory->InitializeLoadedBaseline(0));
	EXPECT_TRUE(pHistory->Dirty(ESaveKind::MANUAL));
	Undo(*pHistory);
	ASSERT_TRUE(pHistory->Clear());
	EXPECT_FALSE(pHistory->InitializeLoadedBaseline(0));
	EXPECT_EQ(pHistory->AllocateObjectId(), 1002);
}

TEST(EditorHistory, SavedContentSurvivesClearAndAuthoringOnlyEdits)
{
	auto pHistory = MakeHistory();
	auto Save = pHistory->CaptureSave("map.map", ESaveKind::MANUAL);
	ASSERT_TRUE(pHistory->CompleteSave(*Save, true));
	auto Edit = pHistory->BeginEdit("Automapper preference", ECategory::MAP);
	pHistory->Update(*Edit, [](auto &Document) { Document.m_AuthoringSetting = 27; });
	EXPECT_EQ(pHistory->Commit(*Edit), ECommitResult::CHANGED);
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	SetValue(*pHistory, 65);
	EXPECT_TRUE(pHistory->Dirty(ESaveKind::MANUAL));
	Undo(*pHistory);
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	ASSERT_TRUE(pHistory->Clear());
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	EXPECT_EQ(pHistory->Current().m_AuthoringSetting, 27);
}

TEST(EditorHistory, SaveCompletionUsesCapturedContentAndDocumentLifetime)
{
	auto pFirst = MakeHistory();
	auto OldSave = pFirst->CaptureSave("same.map", ESaveKind::MANUAL);
	SetValue(*pFirst, 65);
	ASSERT_TRUE(pFirst->CompleteSave(*OldSave, true));
	EXPECT_TRUE(pFirst->Dirty(ESaveKind::MANUAL));
	Undo(*pFirst);
	EXPECT_FALSE(pFirst->Dirty(ESaveKind::MANUAL));
	Redo(*pFirst);
	auto Failed = pFirst->CaptureSave("same.map", ESaveKind::MANUAL);
	EXPECT_FALSE(pFirst->CompleteSave(*Failed, false));
	EXPECT_TRUE(pFirst->Dirty(ESaveKind::MANUAL));
	auto pSecond = MakeHistory();
	EXPECT_FALSE(pSecond->CompleteSave(*OldSave, true));
	EXPECT_TRUE(pSecond->Dirty(ESaveKind::MANUAL));
	pFirst.reset();
	EXPECT_FALSE(pSecond->CompleteSave(*OldSave, true));
	EXPECT_EQ(OldSave->m_pRoot->m_Value, 64);
}

TEST(EditorHistory, SaveKindsAreIndependentAndOlderCompletionCannotReplaceMarker)
{
	auto pHistory = MakeHistory();
	auto Old = pHistory->CaptureSave("manual.map", ESaveKind::MANUAL);
	SetValue(*pHistory, 65);
	auto New = pHistory->CaptureSave("manual.map", ESaveKind::MANUAL);
	ASSERT_TRUE(pHistory->CompleteSave(*New, true));
	EXPECT_FALSE(pHistory->CompleteSave(*Old, true));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	EXPECT_TRUE(pHistory->Dirty(ESaveKind::AUTOMATIC));
	auto Automatic = pHistory->CaptureSave("auto.map", ESaveKind::AUTOMATIC);
	ASSERT_TRUE(pHistory->CompleteSave(*Automatic, true));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::AUTOMATIC));
	auto Edit = pHistory->BeginEdit("Live input", ECategory::MAP);
	EXPECT_FALSE(pHistory->CaptureSave("auto.map", ESaveKind::AUTOMATIC));
	EXPECT_FALSE(pHistory->CaptureSave("manual.map", ESaveKind::MANUAL));
}

TEST(EditorHistorySave, CompletionPublishesPreparedImmutableMarkerWithoutAllocation)
{
	CSaveState<int> Saves;
	const std::string Key(4096, 'k'); // force heap-backed keys, not short-string storage
	auto Ticket = Saves.Capture(std::make_shared<const int>(7), Key, "complete.map", ESaveKind::MANUAL);
	ASSERT_TRUE(Ticket);
	static_assert(noexcept(Saves.Complete(*Ticket, true)));
	const auto Copy = *Ticket;
	Ticket.reset(); // queued job can be the last owner of its prepared key
	EXPECT_TRUE(Saves.Complete(Copy, true));
	EXPECT_FALSE(Saves.Dirty(Key, ESaveKind::MANUAL));
	EXPECT_TRUE(Saves.Dirty(Key, ESaveKind::AUTOMATIC));
	EXPECT_FALSE(Saves.Complete(Copy, true));
	auto Automatic = Saves.Capture(std::make_shared<const int>(8), Key + "auto", "auto.map", ESaveKind::AUTOMATIC);
	ASSERT_TRUE(Automatic);
	EXPECT_TRUE(Saves.Complete(*Automatic, true));
	Automatic.reset();
	EXPECT_FALSE(Saves.Dirty(Key + "auto", ESaveKind::AUTOMATIC));
	EXPECT_FALSE(Saves.Dirty(Key, ESaveKind::MANUAL));
}

TEST(EditorHistorySave, RuntimeAndRevisionMarkersUseTheSameLifetimeService)
{
	auto pHistory = MakeHistory();
	auto pSaves = pHistory->Saves();
	const auto Ticket = pHistory->CaptureSave("saved.map", ESaveKind::MANUAL);
	ASSERT_TRUE(Ticket);
	EXPECT_TRUE(pSaves->Owns(*Ticket));
	EXPECT_EQ(Ticket->Kind(), ESaveKind::MANUAL);
	EXPECT_TRUE(pHistory->Dirty(ESaveKind::MANUAL));
	ASSERT_TRUE(pSaves->Complete(*Ticket, true));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	SetValue(*pHistory, 80);
	EXPECT_TRUE(pHistory->Dirty(ESaveKind::MANUAL));
	Undo(*pHistory);
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	EXPECT_TRUE(pHistory->Clear());
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));

	CSaveState<CTestDocument> Reopened;
	EXPECT_FALSE(Reopened.Owns(*Ticket));
	EXPECT_FALSE(Reopened.Complete(*Ticket, true));
	EXPECT_EQ(Ticket->m_pRoot->m_Value, 64);
}

TEST(EditorHistorySave, DifferentDestinationsAndKindsDoNotAcceptStaleMarkers)
{
	CSaveState<CTestDocument> State;
	ASSERT_TRUE(State.InitializeLoaded("loaded"));
	auto pRoot = std::make_shared<const CTestDocument>();
	const auto First = State.Capture(pRoot, "first", "first.map", ESaveKind::MANUAL);
	const auto Second = State.Capture(pRoot, "second", "renamed.map", ESaveKind::MANUAL);
	const auto Automatic = State.Capture(pRoot, "automatic", "auto.map", ESaveKind::AUTOMATIC);
	ASSERT_TRUE(First && Second && Automatic);
	EXPECT_FALSE(State.InitializeLoaded("later"));
	EXPECT_FALSE(State.Complete(*Second, false));
	EXPECT_FALSE(State.Dirty("loaded", ESaveKind::MANUAL));
	EXPECT_TRUE(State.Complete(*Second, true));
	EXPECT_FALSE(State.Complete(*First, true));
	EXPECT_FALSE(State.Dirty("second", ESaveKind::MANUAL));
	EXPECT_FALSE(State.Dirty("loaded", ESaveKind::AUTOMATIC));
	EXPECT_TRUE(State.Complete(*Automatic, true));
	EXPECT_FALSE(State.Dirty("automatic", ESaveKind::AUTOMATIC));
	EXPECT_FALSE(State.Dirty("second", ESaveKind::MANUAL));
	const auto Copy = State.Capture(pRoot, "copy contents", "copy.map", ESaveKind::COPY);
	ASSERT_TRUE(Copy);
	EXPECT_FALSE(State.Complete(*Copy, true));
	EXPECT_FALSE(State.Dirty("second", ESaveKind::MANUAL));
	EXPECT_FALSE(State.Dirty("automatic", ESaveKind::AUTOMATIC));
}

TEST(EditorHistorySave, SameDestinationWaitsAndCompletedPredecessorsAreReleased)
{
	CSaveQueue Queue;
	auto pFirst = Queue.Register("same.map");
	auto pSecond = Queue.Register("same.map");
	auto pOther = Queue.Register("other.map");
	const std::weak_ptr<CSaveQueue::CTurn> Previous = pFirst;
	std::promise<void> Started;
	std::atomic<bool> Entered = false;
	auto Worker = std::async(std::launch::async, [pSecond, &Started, &Entered] {
		Started.set_value();
		const CSaveQueue::CGuard Guard(pSecond);
		Entered = true;
	});
	Started.get_future().wait();
	EXPECT_FALSE(Entered.load());
	{
		const CSaveQueue::CGuard Other(pOther);
		EXPECT_FALSE(Entered.load());
	}
	// Failure returns through the same guard, so the next save still gets its turn.
	{
		const CSaveQueue::CGuard FailedWrite(pFirst);
	}
	pFirst.reset();
	Worker.get();
	EXPECT_TRUE(Entered.load());
	EXPECT_TRUE(Previous.expired());
	const CSaveQueue::CGuard Third(Queue.Register("same.map"));
}

TEST(EditorHistory, ResourceReplacementRetainsExactBytesWithoutReopeningInput)
{
	auto pHistory = MakeHistory();
	const auto Baseline = pHistory->PinCurrent();
	auto Edit = pHistory->BeginEdit("Replace imported resource", ECategory::MAP);
	std::vector<unsigned char> vInput{1, 0, 4, 255, 8};
	pHistory->Update(*Edit, [&](auto &Document) { Document.m_Resource = CSharedVector<unsigned char>(vInput); });
	ASSERT_EQ(pHistory->Commit(*Edit), ECommitResult::CHANGED);
	vInput.assign(100, 77);
	Undo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Resource.Read().size(), 4096);
	EXPECT_EQ(pHistory->Current().m_Resource.Read()[0], 42);
	Redo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Resource.Read(), (std::vector<unsigned char>{1, 0, 4, 255, 8}));
	EXPECT_EQ(Baseline->m_Resource.Read()[0], 42);
}

TEST(EditorHistory, EntryLimitRetainsValidBaselineAndLatestUndo)
{
	auto pHistory = MakeHistory({2, 64 * 1024 * 1024});
	for(int Value = 65; Value <= 70; ++Value)
		SetValue(*pHistory, Value);
	EXPECT_EQ(pHistory->Revisions().size(), 3);
	Undo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Value, 69);
	Undo(*pHistory);
	EXPECT_EQ(pHistory->Current().m_Value, 68);
	EXPECT_FALSE(pHistory->CanUndo());
}

TEST(EditorHistory, OversizedEditsKeepRecoverablePairWithoutAccumulating)
{
	auto pHistory = MakeHistory({100, 1});
	for(int Value = 65; Value < 72; ++Value)
	{
		SetValue(*pHistory, Value);
		EXPECT_TRUE(pHistory->Oversized());
		EXPECT_GT(pHistory->RetainedBytes(), 1);
		ASSERT_EQ(pHistory->Revisions().size(), 2);
		Undo(*pHistory);
		EXPECT_EQ(pHistory->Current().m_Value, Value - 1);
		Redo(*pHistory);
	}
}

TEST(EditorHistory, RandomizedTimelineMatchesIndependentValueModel)
{
	constexpr unsigned Seed = 0xDDAA67;
	std::mt19937 Random(Seed);
	auto pHistory = MakeHistory({1000, std::numeric_limits<std::size_t>::max()});
	std::vector<int> vReference{64};
	std::size_t Cursor = 0;
	for(int Iteration = 0; Iteration < 250; ++Iteration)
	{
		SCOPED_TRACE("Seed " + std::to_string(Seed) + ", iteration " + std::to_string(Iteration));
		const auto Operation = Random() % 5;
		if(Operation == 0 && Cursor > 0)
		{
			Undo(*pHistory);
			--Cursor;
		}
		else if(Operation == 1 && Cursor + 1 < vReference.size())
		{
			Redo(*pHistory);
			++Cursor;
		}
		else
		{
			const int Value = static_cast<int>(Random() % 100);
			auto Edit = pHistory->BeginEdit("Random edit", ECategory::MAP);
			ASSERT_TRUE(Edit);
			pHistory->Update(*Edit, [&](auto &Document) { Document.m_Value = Value; });
			if(Operation == 2)
				Edit->Cancel();
			else if(Value == vReference[Cursor])
				EXPECT_EQ(pHistory->Commit(*Edit), ECommitResult::UNCHANGED);
			else
			{
				ASSERT_EQ(pHistory->Commit(*Edit), ECommitResult::CHANGED);
				vReference.resize(Cursor + 1);
				vReference.push_back(Value);
				++Cursor;
			}
		}
		EXPECT_EQ(pHistory->Current().m_Value, vReference[Cursor]);
		EXPECT_EQ(pHistory->Cursor(), Cursor);
		EXPECT_EQ(pHistory->Revisions().size(), vReference.size());
	}
}

TEST(EditorHistory, NewRecordFieldNeedsNoHistoryMapping)
{
	class CExtendedRecord
	{
	public:
		int m_Existing = 0;
		std::array<int, 3> m_NewField{};
		bool operator==(const CExtendedRecord &) const = default;
	};
	auto pHistory = CRevisionHistory<CExtendedRecord>::Create({}, {10, 1024 * 1024}, [](const auto &) { return true; }, [](const auto &Document) { return std::to_string(Document.m_Existing); }, [](const auto &, auto &) {});
	ASSERT_NE(pHistory, nullptr);
	auto &History = *pHistory;
	auto Edit = History.BeginEdit("New editing tool", ECategory::MAP);
	History.Update(*Edit, [](auto &Document) { Document.m_NewField = {5, 9, 17}; });
	ASSERT_EQ(History.Commit(*Edit), ECommitResult::CHANGED);
	ASSERT_TRUE(History.RequestUndo());
	ASSERT_TRUE(History.PublishPending());
	EXPECT_EQ(History.Current().m_NewField, (std::array<int, 3>{}));
	ASSERT_TRUE(History.RequestRedo());
	ASSERT_TRUE(History.PublishPending());
	EXPECT_EQ(History.Current().m_NewField, (std::array<int, 3>{5, 9, 17}));
}

#if defined(__cpp_exceptions)
namespace
{
	struct CAllocatingTestTile
	{
		inline static bool ms_Fail = false;
		int m_Index = 0;
		CAllocatingTestTile()
		{
			if(ms_Fail)
				throw std::bad_alloc();
		}
		explicit CAllocatingTestTile(int Index) :
			m_Index(Index) {}
		CAllocatingTestTile(const CAllocatingTestTile &Other) :
			m_Index(Other.m_Index)
		{
			if(ms_Fail)
				throw std::bad_alloc();
		}
		CAllocatingTestTile &operator=(const CAllocatingTestTile &) = default;
		bool operator==(const CAllocatingTestTile &) const = default;
	};
}

TEST(EditorHistoryStorage, FailedChunkAllocationDoesNotPublishIncompletePaths)
{
	using CPlane = CTilePlane<CAllocatingTestTile, 16>;
	for(int Scenario = 0; Scenario < 3; ++Scenario)
	{
		auto Plane = *CPlane::Create(128, 128);
		auto Expected = *CPlane::Create(128, 128);
		const CAllocatingTestTile One(1), Two(2);
		(void)Plane.At(0, 0); // Construct the shared empty cells before injecting failure.
		if(Scenario != 0)
		{
			Plane.Set(0, 0, One);
			Expected.Set(0, 0, One);
		}
		std::optional<CPlane> Pinned;
		if(Scenario == 2)
			Pinned = Plane;
		CStorageUsage Before;
		Plane.Account(Before);
		CAllocatingTestTile::ms_Fail = true;
		EXPECT_THROW(Plane.Set(Scenario == 2 ? 0 : 127, Scenario == 2 ? 0 : 127, Two), std::bad_alloc);
		CAllocatingTestTile::ms_Fail = false;
		EXPECT_EQ(Plane, Expected);
		CStorageUsage After;
		Plane.Account(After);
		EXPECT_EQ(After.Bytes(), Before.Bytes());
		Plane.VisitChangedChunks(nullptr, [](auto, auto, auto, auto, auto Cells) { EXPECT_GE(Cells[0].m_Index, 0); });
		if(Pinned)
			EXPECT_EQ(Plane.VisitChangedChunks(&*Pinned, [](auto...) { ADD_FAILURE(); }), 1u);
		Plane.Set(127, 127, Two);
		EXPECT_EQ(Plane.At(127, 127), Two);
	}
}

TEST(EditorHistory, AccountingFailureRollsBackPartialAdditionsAndRemovals)
{
	int FailOnCall = 0, Calls = 0;
	auto pHistory = CTestHistory::Create({}, {10, 1024 * 1024}, [](const auto &) { return true; }, [](const auto &Document) { return std::to_string(Document.m_Value); }, [&](const auto &Document, auto &Usage) {
		Document.m_Tiles.Account(Usage);
		if(FailOnCall && ++Calls == FailOnCall)
			throw std::bad_alloc();
		Document.m_Resource.Account(Usage); });
	ASSERT_NE(pHistory, nullptr);
	auto &History = *pHistory;
	for(int Value : {65, 66, 67})
	{
		auto Edit = History.BeginEdit("Seed", ECategory::MAP);
		ASSERT_TRUE(Edit);
		ASSERT_TRUE(History.Update(*Edit, [&](auto &Document) { Document.m_Value = Value; Document.m_Tiles.Set(Value - 65, 0, {Value, 0}); }));
		ASSERT_EQ(History.Commit(*Edit), ECommitResult::CHANGED);
	}
	Undo(History);
	Undo(History);
	const auto Revisions = History.Revisions();
	const auto Cursor = History.Cursor(), Retained = History.RetainedBytes();
	for(const int FailAt : {1, 2, 3})
	{
		auto Edit = History.BeginEdit("Failed accounting", ECategory::MAP);
		ASSERT_TRUE(Edit);
		ASSERT_TRUE(History.Update(*Edit, [](auto &Document) { Document.m_Value = 99; Document.m_Tiles.Set(40, 40, {99, 0}); }));
		Calls = 0;
		FailOnCall = FailAt;
		EXPECT_EQ(History.Commit(*Edit), ECommitResult::FAILED);
		FailOnCall = 0;
		EXPECT_EQ(History.Cursor(), Cursor);
		EXPECT_EQ(History.RetainedBytes(), Retained);
		ASSERT_EQ(History.Revisions().size(), Revisions.size());
		for(std::size_t Index = 0; Index < Revisions.size(); ++Index)
			EXPECT_EQ(History.Revisions()[Index].m_pRoot, Revisions[Index].m_pRoot);
		EXPECT_TRUE(History.CanRedo());
		CStorageUsage Fresh;
		History.AccountRetainedPayload(Fresh);
		EXPECT_EQ(History.RetainedPayloadBytes(), Fresh.Bytes());
	}
	SetValue(History, 100);
	EXPECT_FALSE(History.CanRedo());
	CStorageUsage Fresh;
	History.AccountRetainedPayload(Fresh);
	EXPECT_EQ(History.RetainedPayloadBytes(), Fresh.Bytes());
}

TEST(EditorHistory, RecoverableAllocationFailureDoesNotBranchOrEvict)
{
	bool Fail = false;
	auto pHistory = CRevisionHistory<CTestDocument>::Create({}, {2, 1024 * 1024}, [](const auto &) { return true; }, [&](const auto &Document) {
			if(Fail)
				throw std::bad_alloc();
		return std::to_string(Document.m_Value); }, [](const auto &Document, auto &Usage) { Document.Account(Usage); });
	ASSERT_NE(pHistory, nullptr);
	auto &History = *pHistory;
	SetValue(History, 65);
	Undo(History);
	const auto pBefore = History.PinCurrent();
	auto Edit = History.BeginEdit("Allocation failure", ECategory::MAP);
	History.Update(*Edit, [](auto &Document) { Document.m_Value = 99; });
	Fail = true;
	EXPECT_EQ(History.Commit(*Edit), ECommitResult::FAILED);
	EXPECT_EQ(History.PinCurrent(), pBefore);
	EXPECT_TRUE(History.CanRedo());
	EXPECT_FALSE(History.HasActiveEdit());
}
#endif
TEST(EditorHistorySchema, TileCellsExcludeOnlyDerivedAndReservedBytes)
{
	CEditorTilePlane<CTileValues> Tiles(2, 1);
	Tiles.Set(0, CTile{37, TILEFLAG_XFLIP | TILEFLAG_ROTATE | TILEFLAG_OPAQUE, 71, 199});
	const auto Before = Tiles;
	Tiles.Set(0, CTile{37, TILEFLAG_XFLIP | TILEFLAG_ROTATE, 0, 0});
	EXPECT_EQ(Tiles, Before);
	EXPECT_EQ(Tiles.Plane().SharedChunks(Before.Plane()), 1u);
	EXPECT_EQ(Tiles[0].m_Index, 37);
	EXPECT_EQ(Tiles[0].m_Flags, TILEFLAG_XFLIP | TILEFLAG_ROTATE);
	const CTile Exported = Tiles[0];
	EXPECT_EQ(Exported.m_Skip, 0);
	EXPECT_EQ(Exported.m_MustBe0, 0);
	Tiles.Update(0, [](auto &Tile) { Tile.m_Flags |= TILEFLAG_OPAQUE; });
	EXPECT_EQ(Tiles, Before);

	CEditorTilePlane<CSpeedupTileValues> Speedup(1, 1);
	Speedup.Set(0, CSpeedupTile{71, 93, 29, 191, -137});
	const auto OriginalSpeedup = Speedup;
	Speedup.Set(0, CSpeedupTile{71, 93, 29, 0, -137});
	EXPECT_EQ(Speedup, OriginalSpeedup);
	EXPECT_EQ(Speedup[0].m_Force, 71);
	EXPECT_EQ(Speedup[0].m_MaxSpeed, 93);
	EXPECT_EQ(Speedup[0].m_Type, 29);
	EXPECT_EQ(Speedup[0].m_Angle, -137);
	const CSpeedupTile ExportedSpeedup = Speedup[0];
	EXPECT_EQ(ExportedSpeedup.m_MustBe0, 0);
}

TEST(EditorHistorySchema, AuxiliaryPlanesKeepEveryPayloadIndependentOfBaseTiles)
{
	CLayerTeleValues Tele;
	CLayerSpeedupValues Speedup;
	CLayerSwitchValues Switch;
	CLayerTuneValues Tune;
	ASSERT_TRUE(Tele.m_TeleTiles.Resize(33, 35));
	ASSERT_TRUE(Speedup.m_SpeedupTiles.Resize(33, 35));
	ASSERT_TRUE(Switch.m_SwitchTiles.Resize(33, 35));
	ASSERT_TRUE(Tune.m_TuneTiles.Resize(33, 35));
	const std::size_t Index = 34 * 33 + 32;
	Tele.m_TeleTiles.Set(Index, CTeleTile{219, 83});
	Speedup.m_SpeedupTiles.Set(Index, CSpeedupTile{101, 127, 67, 0, -291});
	Switch.m_SwitchTiles.Set(Index, CSwitchTile{231, 199, 13, 87});
	Tune.m_TuneTiles.Set(Index, CTuneTile{241, 69});
	const auto SavedTele = Tele;
	const auto SavedSpeedup = Speedup;
	const auto SavedSwitch = Switch;
	const auto SavedTune = Tune;
	Tele.m_TeleTiles.Update(Index, [](auto &Tile) { Tile.m_Number = 1; });
	Speedup.m_SpeedupTiles.Update(Index, [](auto &Tile) { Tile.m_Angle = 12; });
	Switch.m_SwitchTiles.Update(Index, [](auto &Tile) { Tile.m_Delay = 3; });
	Tune.m_TuneTiles.Update(Index, [](auto &Tile) { Tile.m_Number = 2; });
	EXPECT_NE(Tele, SavedTele);
	EXPECT_NE(Speedup, SavedSpeedup);
	EXPECT_NE(Switch, SavedSwitch);
	EXPECT_NE(Tune, SavedTune);
	EXPECT_EQ(SavedTele.m_TeleTiles[Index].m_Number, 219);
	EXPECT_EQ(SavedTele.m_TeleTiles[Index].m_Type, 83);
	EXPECT_EQ(SavedSpeedup.m_SpeedupTiles[Index].m_Force, 101);
	EXPECT_EQ(SavedSpeedup.m_SpeedupTiles[Index].m_MaxSpeed, 127);
	EXPECT_EQ(SavedSpeedup.m_SpeedupTiles[Index].m_Type, 67);
	EXPECT_EQ(SavedSpeedup.m_SpeedupTiles[Index].m_Angle, -291);
	EXPECT_EQ(SavedSwitch.m_SwitchTiles[Index].m_Number, 231);
	EXPECT_EQ(SavedSwitch.m_SwitchTiles[Index].m_Type, 199);
	EXPECT_EQ(SavedSwitch.m_SwitchTiles[Index].m_Flags, 13);
	EXPECT_EQ(SavedSwitch.m_SwitchTiles[Index].m_Delay, 87);
	EXPECT_EQ(SavedTune.m_TuneTiles[Index].m_Number, 241);
	EXPECT_EQ(SavedTune.m_TuneTiles[Index].m_Type, 69);
}

TEST(EditorHistorySchema, RectangularTileTransformsPreserveCoordinatesAndSnapshots)
{
	CEditorTilePlane<CTileValues> Tiles(3, 2);
	for(std::size_t Index = 0; Index < Tiles.Size(); ++Index)
		Tiles.Set(Index, CTile{static_cast<unsigned char>(Index + 1), TILEFLAG_YFLIP});
	const auto Original = Tiles;
	Tiles.RotateClockwise();
	EXPECT_EQ(Tiles.Width(), 2);
	EXPECT_EQ(Tiles.Height(), 3);
	const std::array<unsigned char, 6> aRotated{4, 1, 5, 2, 6, 3};
	for(std::size_t Index = 0; Index < aRotated.size(); ++Index)
	{
		EXPECT_EQ(Tiles[Index].m_Index, aRotated[Index]);
		EXPECT_EQ(Tiles[Index].m_Flags, TILEFLAG_YFLIP);
		EXPECT_EQ(Original[Index].m_Index, Index + 1);
	}
	Tiles = Original;
	Tiles.FlipX();
	const std::array<unsigned char, 6> aFlippedX{3, 2, 1, 6, 5, 4};
	for(std::size_t Index = 0; Index < aFlippedX.size(); ++Index)
		EXPECT_EQ(Tiles[Index].m_Index, aFlippedX[Index]);
	Tiles = Original;
	Tiles.FlipY();
	const std::array<unsigned char, 6> aFlippedY{4, 5, 6, 1, 2, 3};
	for(std::size_t Index = 0; Index < aFlippedY.size(); ++Index)
		EXPECT_EQ(Tiles[Index].m_Index, aFlippedY[Index]);
	Tiles = Original;
	Tiles.Shift(1, -1);
	const std::array<unsigned char, 6> aShifted{0, 4, 5, 0, 0, 0};
	for(std::size_t Index = 0; Index < aShifted.size(); ++Index)
		EXPECT_EQ(Tiles[Index].m_Index, aShifted[Index]);
	ASSERT_TRUE(Tiles.Resize(5, 4));
	EXPECT_EQ(Tiles[1].m_Index, 4);
	EXPECT_EQ(Tiles[2].m_Index, 5);
	EXPECT_EQ(Tiles[19].m_Index, 0);
	ASSERT_TRUE(Tiles.Resize(1, 1));
	ASSERT_TRUE(Tiles.Resize(5, 4));
	EXPECT_EQ(Tiles[1].m_Index, 0);
	EXPECT_EQ(Tiles[2].m_Index, 0);
	EXPECT_FALSE(Tiles.Resize(-1, 4));
	EXPECT_EQ(Tiles.Width(), 5);
}

TEST(EditorHistorySchema, SpeedupRotationsKeepIndependentAnglesAndPayloads)
{
	CEditorTilePlane<CSpeedupTileValues> Original(3, 2);
	const std::array<short, 6> aInitialAngles{5, 30, 95, 180, 270, 350};
	for(std::size_t Index = 0; Index < Original.Size(); ++Index)
		Original.Set(Index, CSpeedupTile{static_cast<unsigned char>(Index + 1), static_cast<unsigned char>(Index + 101), 29, 0, aInitialAngles[Index]});
	const std::array<std::array<unsigned char, 6>, 4> aaExpectedForce{{{1, 2, 3, 4, 5, 6}, {4, 1, 5, 2, 6, 3}, {6, 5, 4, 3, 2, 1}, {3, 6, 2, 5, 1, 4}}};
	const std::array<std::array<short, 6>, 4> aaExpectedAngles{{{5, 30, 95, 180, 270, 350}, {270, 95, 0, 120, 80, 185}, {170, 90, 0, 275, 210, 185}, {5, 260, 300, 180, 275, 90}}};
	for(int Rotation = 0; Rotation < 4; ++Rotation)
	{
		auto Tiles = Original;
		if(Rotation == 1 || Rotation == 3)
			RotateSpeedupTilesClockwise(Tiles);
		if(Rotation == 2 || Rotation == 3)
		{
			FlipSpeedupTilesX(Tiles);
			FlipSpeedupTilesY(Tiles);
		}
		EXPECT_EQ(Tiles.Width(), Rotation % 2 == 0 ? 3 : 2);
		EXPECT_EQ(Tiles.Height(), Rotation % 2 == 0 ? 2 : 3);
		for(std::size_t Index = 0; Index < Tiles.Size(); ++Index)
		{
			EXPECT_EQ(Tiles[Index].m_Force, aaExpectedForce[Rotation][Index]);
			EXPECT_EQ(Tiles[Index].m_MaxSpeed, aaExpectedForce[Rotation][Index] + 100);
			EXPECT_EQ(Tiles[Index].m_Type, 29);
			EXPECT_EQ(Tiles[Index].m_Angle, aaExpectedAngles[Rotation][Index]);
			EXPECT_EQ(Original[Index].m_Angle, aInitialAngles[Index]);
		}
	}
}

TEST(EditorHistorySchema, RenderProjectionRefreshesOpacityWithoutChangingDocument)
{
	CEditorTilePlane<CTileValues> Tiles(65, 1);
	Tiles.Set(0, CTile{7, TILEFLAG_ROTATE});
	Tiles.Set(64, CTile{8, TILEFLAG_XFLIP});
	const auto Original = Tiles;
	CEditorTileProjection Projection;
	std::array<unsigned char, 256> aFlags{};
	aFlags[7] = TILEFLAG_OPAQUE;
	EXPECT_EQ(Projection.Refresh(Tiles, aFlags), 3u);
	EXPECT_EQ(Projection.Cells()[0].m_Flags, TILEFLAG_ROTATE | TILEFLAG_OPAQUE);
	EXPECT_EQ(Projection.Cells()[64].m_Flags, TILEFLAG_XFLIP);
	EXPECT_EQ(Projection.Refresh(Tiles, aFlags), 0u);
	aFlags[7] = 0;
	aFlags[8] = TILEFLAG_OPAQUE;
	EXPECT_EQ(Projection.Refresh(Tiles, aFlags), 3u);
	EXPECT_EQ(Projection.Cells()[0].m_Flags, TILEFLAG_ROTATE);
	EXPECT_EQ(Projection.Cells()[64].m_Flags, TILEFLAG_XFLIP | TILEFLAG_OPAQUE);
	EXPECT_EQ(Tiles, Original);
	Tiles.Update(64, [](auto &Tile) { Tile.m_Index = 9; });
	EXPECT_EQ(Projection.Refresh(Tiles, aFlags), 1u);
	EXPECT_EQ(Projection.Cells()[64].m_Flags, TILEFLAG_XFLIP);
	EXPECT_EQ(Original[64].m_Index, 8);
}

TEST(EditorHistorySchema, TileImportOwnsNormalizedBytes)
{
	std::array<CTile, 2> aInput{{{19, TILEFLAG_XFLIP | TILEFLAG_OPAQUE, 17, 93}, {31, TILEFLAG_ROTATE, 71, 11}}};
	CEditorTilePlane<CTileValues> Tiles(2, 1);
	Tiles.Assign(std::span<const CTile>(aInput));
	aInput[0] = {};
	EXPECT_EQ(Tiles[0].m_Index, 19);
	EXPECT_EQ(Tiles[0].m_Flags, TILEFLAG_XFLIP);
	EXPECT_EQ(Tiles[1].m_Index, 31);
	EXPECT_EQ(Tiles[1].m_Flags, TILEFLAG_ROTATE);
	const auto Original = Tiles;
	Tiles.Update(1, [](auto &Tile) { Tile.m_Index = 43; });
	EXPECT_EQ(Original[1].m_Index, 31);
}
TEST(EditorHistorySchema, TileFlagRotationHasExpectedOrientationForEveryCombination)
{
	const std::array<unsigned char, 8> aBefore{0, 1, 2, 3, 8, 9, 10, 11};
	const std::array<unsigned char, 8> aAfter{8, 9, 10, 11, 3, 2, 1, 0};
	for(std::size_t Index = 0; Index < aBefore.size(); ++Index)
	{
		CTileValues Tile(CTile{37, aBefore[Index]});
		RotateTileFlagsClockwise(Tile);
		EXPECT_EQ(Tile.m_Index, 37);
		EXPECT_EQ(Tile.m_Flags, aAfter[Index]);
	}
}

TEST(EditorHistorySchema, SaveProjectionChangesOnlyOutputFlags)
{
	CEditorTilePlane<CTileValues> Tiles(2, 1);
	Tiles.Set(0, CTile{13, TILEFLAG_XFLIP | TILEFLAG_ROTATE});
	Tiles.Set(1, CTile{19, TILEFLAG_YFLIP});
	const auto Original = Tiles;
	std::array<unsigned char, 256> aFlags{};
	aFlags[13] = TILEFLAG_OPAQUE;
	const auto vExport = ExportTilePlane(Tiles, aFlags);
	ASSERT_EQ(vExport.size(), 2u);
	EXPECT_EQ(vExport[0].m_Index, 13);
	EXPECT_EQ(vExport[0].m_Flags, TILEFLAG_XFLIP | TILEFLAG_ROTATE | TILEFLAG_OPAQUE);
	EXPECT_EQ(vExport[1].m_Index, 19);
	EXPECT_EQ(vExport[1].m_Flags, TILEFLAG_YFLIP);
	for(const auto &Tile : vExport)
	{
		EXPECT_EQ(Tile.m_Skip, 0);
		EXPECT_EQ(Tile.m_MustBe0, 0);
	}
	EXPECT_EQ(Tiles, Original);
	EXPECT_EQ(Tiles.Plane().SharedChunks(Original.Plane()), 1u);
	aFlags = {};
	const auto vTransparentExport = ExportTilePlane(Tiles, aFlags);
	EXPECT_EQ(vTransparentExport[0].m_Flags, TILEFLAG_XFLIP | TILEFLAG_ROTATE);
	EXPECT_EQ(Tiles, Original);
}

namespace
{
	CEditorDocumentValues MakeCompleteDocument()
	{
		CDocumentIdentityAllocator Ids;
		const auto NextId = [&] { return *Ids.Allocate(); };
		CEditorDocumentValues Document;
		str_copy(Document.m_MapInfo.m_Author.Buffer(), "mapper");
		str_copy(Document.m_MapInfo.m_Version.Buffer(), "2.3");
		str_copy(Document.m_MapInfo.m_Credits.Buffer(), "contributors");
		str_copy(Document.m_MapInfo.m_License.Buffer(), "CC0");
		Document.m_vSettings.emplace_back("sv_test_cmd 1");
		Document.m_vSettings.emplace_back("sv_test_cmd 2");

		CEditorImageValues Image;
		Image.m_Id = NextId();
		str_copy(Image.m_aName, "retained image");
		Image.m_External = 1;
		Image.m_Width = 2;
		Image.m_Height = 2;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_Content.emplace(std::vector<std::uint8_t>(16, 31));
		Document.m_Images.Update([&](auto &vImages) { vImages.push_back(Image); });
		CEditorSoundValues Sound;
		Sound.m_Id = NextId();
		str_copy(Sound.m_aName, "retained sound");
		Sound.m_Content.emplace(std::vector<std::uint8_t>{1, 7, 9, 3});
		Document.m_Sounds.Update([&](auto &vSounds) { vSounds.push_back(Sound); });
		for(const auto Type : {CEnvelopeValues::EType::POSITION, CEnvelopeValues::EType::COLOR, CEnvelopeValues::EType::SOUND})
		{
			CEnvelopeValues Envelope;
			Envelope.m_Id = NextId();
			Envelope.m_Type = Type;
			Envelope.m_Synchronized = false;
			str_copy(Envelope.m_aName, "envelope");
			for(int Index = 0; Index < 2; ++Index)
			{
				CEnvelopePointValues Point;
				Point.m_Id = NextId();
				Point.m_Time = CFixedTime(Index * 2000);
				Point.m_Curvetype = Index == 0 ? CURVETYPE_BEZIER : CURVETYPE_SMOOTH;
				for(int Channel = 0; Channel < 4; ++Channel)
				{
					Point.m_aValues[Channel] = 100 + Index * 10 + Channel;
					Point.m_Bezier.m_aInTangentDeltaX[Channel] = CFixedTime(200 + Channel);
					Point.m_Bezier.m_aInTangentDeltaY[Channel] = 300 + Channel;
					Point.m_Bezier.m_aOutTangentDeltaX[Channel] = CFixedTime(400 + Channel);
					Point.m_Bezier.m_aOutTangentDeltaY[Channel] = 500 + Channel;
				}
				Envelope.m_vPoints.push_back(Point);
			}
			Document.m_Envelopes.Update([&](auto &vEnvelopes) { vEnvelopes.emplace_back(Envelope); });
		}
		CGroupDocumentValues Group;
		Group.m_Id = NextId();
		Group.m_GameGroup = true;
		str_copy(Group.m_aName, "Game");
		Group.m_OffsetX = 47;
		Group.m_OffsetY = -13;
		Group.m_ParallaxX = 87;
		Group.m_ParallaxY = 91;
		Group.m_UseClipping = 1;
		Group.m_ClipX = 5;
		Group.m_ClipY = 9;
		Group.m_ClipW = 400;
		Group.m_ClipH = 600;
		for(int Role = 0; Role < 7; ++Role)
		{
			CLayerDocumentValues Layer;
			Layer.m_Id = NextId();
			Layer.m_Type = LAYERTYPE_TILES;
			Layer.m_Flags = LAYERFLAG_DETAIL;
			str_copy(Layer.m_aName, "tiles");
			CTileLayerDocumentValues Tiles;
			Tiles.m_Tiles.Resize(2, 2);
			Tiles.m_Tiles.Set(3, CTileValues(CTile{17, TILEFLAG_XFLIP, 0, 0}));
			Tiles.m_HasGame = Role == 0;
			Tiles.m_HasFront = Role == 1;
			Tiles.m_HasTele = Role == 2;
			Tiles.m_HasSpeedup = Role == 3;
			Tiles.m_HasSwitch = Role == 4;
			Tiles.m_HasTune = Role == 5;
			Tiles.m_Image = {Image.m_Id};
			Tiles.m_Color = CColor{11, 22, 33, 44};
			Tiles.m_ColorEnv = Document.EnvelopeReference(1);
			Tiles.m_ColorEnvOffset = 321;
			Tiles.m_AutomapperConfig = 3;
			Tiles.m_AutomapperReference = 4;
			Tiles.m_AutoAutomapper = true;
			Tiles.m_Seed = 73;
			Tiles.m_LiveGameTiles = true;
			Tiles.m_FillGameTile = TILE_DEATH;
			if(Role == 2)
			{
				CLayerTeleValues Auxiliary;
				Auxiliary.m_TeleTiles.Resize(2, 2);
				Auxiliary.m_TeleTiles.Set(3, CTeleTileValues(CTeleTile{9, TILE_TELEIN}));
				Tiles.m_Auxiliary = Auxiliary;
			}
			else if(Role == 3)
			{
				CLayerSpeedupValues Auxiliary;
				Auxiliary.m_SpeedupTiles.Resize(2, 2);
				Auxiliary.m_SpeedupTiles.Set(3, CSpeedupTileValues(CSpeedupTile{12, 34, TILE_SPEED_BOOST, 0, 267}));
				Tiles.m_Auxiliary = Auxiliary;
			}
			else if(Role == 4)
			{
				CLayerSwitchValues Auxiliary;
				Auxiliary.m_SwitchTiles.Resize(2, 2);
				Auxiliary.m_SwitchTiles.Set(3, CSwitchTileValues(CSwitchTile{8, TILE_SWITCHOPEN, TILEFLAG_ROTATE, 15}));
				Tiles.m_Auxiliary = Auxiliary;
			}
			else if(Role == 5)
			{
				CLayerTuneValues Auxiliary;
				Auxiliary.m_TuneTiles.Resize(2, 2);
				Auxiliary.m_TuneTiles.Set(3, CTuneTileValues(CTuneTile{10, TILE_TUNE}));
				Tiles.m_Auxiliary = Auxiliary;
			}
			Layer.m_Data = std::move(Tiles);
			Group.m_Layers.Update([&](auto &vLayers) { vLayers.emplace_back(Layer); });
		}
		CLayerDocumentValues QuadLayer;
		QuadLayer.m_Id = NextId();
		QuadLayer.m_Type = LAYERTYPE_QUADS;
		CLayerQuadsValues Quads;
		Quads.m_Image = {Image.m_Id};
		CQuadValues Quad;
		Quad.m_Id = NextId();
		for(int Index = 0; Index < 5; ++Index)
			Quad.m_aPoints[Index] = CPoint{1000 + Index, 2000 + Index};
		for(int Index = 0; Index < 4; ++Index)
		{
			Quad.m_aColors[Index] = CColor{10 + Index, 20 + Index, 30 + Index, 40 + Index};
			Quad.m_aTexcoords[Index] = CPoint{3000 + Index, 4000 + Index};
		}
		Quad.m_PosEnv = Document.EnvelopeReference(0);
		Quad.m_PosEnvOffset = 125;
		Quad.m_ColorEnv = Document.EnvelopeReference(1);
		Quad.m_ColorEnvOffset = -75;
		Quads.m_vQuads.push_back(Quad);
		QuadLayer.m_Data = Quads;
		Group.m_Layers.Update([&](auto &vLayers) { vLayers.emplace_back(QuadLayer); });
		CLayerDocumentValues SoundLayer;
		SoundLayer.m_Id = NextId();
		SoundLayer.m_Type = LAYERTYPE_SOUNDS;
		CLayerSoundsValues Sources;
		Sources.m_Sound = {Sound.m_Id};
		for(int Shape = 0; Shape < CSoundShape::NUM_SHAPES; ++Shape)
		{
			CSoundSourceValues Source;
			Source.m_Id = NextId();
			Source.m_Position = CPoint{81, -23};
			Source.m_Loop = 1;
			Source.m_Pan = 1;
			Source.m_TimeDelay = 12;
			Source.m_Falloff = 56;
			Source.m_PosEnv = Document.EnvelopeReference(0);
			Source.m_PosEnvOffset = 234;
			Source.m_SoundEnv = Document.EnvelopeReference(2);
			Source.m_SoundEnvOffset = 345;
			Source.m_Shape.m_Type = Shape;
			if(Shape == CSoundShape::SHAPE_RECTANGLE)
				Source.m_Shape.m_Rectangle = {456, 567};
			else
				Source.m_Shape.m_Circle.m_Radius = 678;
			Sources.m_vSources.push_back(Source);
		}
		SoundLayer.m_Data = Sources;
		Group.m_Layers.Update([&](auto &vLayers) { vLayers.emplace_back(SoundLayer); });
		Document.m_Groups.Update([&](auto &vGroups) { vGroups.emplace_back(Group); });
		return Document;
	}

	template<typename F>
	void UpdateDocumentLayer(CEditorDocumentValues &Document, std::size_t Index, F Function)
	{
		Document.m_Groups.Update([&](auto &vGroups) {
			vGroups[0].Update([&](auto &Group) {
				Group.m_Layers.Update([&](auto &vLayers) { vLayers[Index].Update(Function); });
			});
		});
	}

	const CLayerDocumentValues &DocumentLayer(const CEditorDocumentValues &Document, std::size_t Index)
	{
		return Document.m_Groups.Read()[0].Read().m_Layers.Read()[Index].Read();
	}
}

TEST(EditorDocumentSchema, MetadataAndSettingsAreValuesAndIgnoreUnusedText)
{
	auto Document = MakeCompleteDocument();
	const auto Before = Document;
	EXPECT_STREQ(Document.m_MapInfo.m_Author.Buffer(), "mapper");
	EXPECT_STREQ(Document.m_MapInfo.m_Version.Buffer(), "2.3");
	EXPECT_STREQ(Document.m_MapInfo.m_Credits.Buffer(), "contributors");
	EXPECT_STREQ(Document.m_MapInfo.m_License.Buffer(), "CC0");
	ASSERT_EQ(Document.m_vSettings.size(), 2u);
	EXPECT_STREQ(Document.m_vSettings[0].m_Command.Buffer(), "sv_test_cmd 1");
	EXPECT_STREQ(Document.m_vSettings[1].m_Command.Buffer(), "sv_test_cmd 2");
	Document.m_MapInfo.m_Author.Buffer()[31] = 'x';
	Document.m_vSettings[0].m_Command.Buffer()[255] = 'z';
	EXPECT_EQ(Document, Before);
	str_copy(Document.m_MapInfo.m_Credits.Buffer(), "different");
	std::swap(Document.m_vSettings[0], Document.m_vSettings[1]);
	EXPECT_NE(Document, Before);
	EXPECT_STREQ(Before.m_MapInfo.m_Credits.Buffer(), "contributors");
	EXPECT_STREQ(Before.m_vSettings[0].m_Command.Buffer(), "sv_test_cmd 1");
}

TEST(EditorDocumentSchema, MixedCapacityNamesAndTextInitializeWithinTheirBounds)
{
	const auto CheckBounds = []<typename T>() {
		alignas(T) std::array<unsigned char, sizeof(T) + 1024> aStorage;
		aStorage.fill(0xa5);
		auto *pValue = std::construct_at(reinterpret_cast<T *>(aStorage.data() + 16));
		for(std::size_t Index = 0; Index < aStorage.size(); ++Index)
			if(Index < 16 || Index >= 16 + sizeof(T))
			{
				EXPECT_EQ(aStorage[Index], 0xa5) << "constructor wrote beyond its record at " << Index;
			}
		std::destroy_at(pValue);
	};
	CheckBounds.operator()<CNamedDocumentValues<IO_MAX_PATH_LENGTH>>();
	CheckBounds.operator()<CNamedDocumentValues<12>>();
	CheckBounds.operator()<CDocumentText<128>>();
	CheckBounds.operator()<CDocumentText<16>>();
	class CGuardedNames
	{
	public:
		std::uint64_t m_Before = 0x0123456789abcdef;
		CNamedDocumentValues<12> m_Small;
		std::uint64_t m_Middle = 0xfedcba9876543210;
		CNamedDocumentValues<IO_MAX_PATH_LENGTH> m_Large;
		std::uint64_t m_After = 0x123456789abcdef0;
		CDocumentText<16> m_Text;
		std::uint64_t m_Last = 0x23456789abcdef01;
	};
	CGuardedNames Names;
	EXPECT_EQ(Names.m_Before, 0x0123456789abcdefu);
	EXPECT_EQ(Names.m_Middle, 0xfedcba9876543210u);
	EXPECT_EQ(Names.m_After, 0x123456789abcdef0u);
	EXPECT_EQ(Names.m_Last, 0x23456789abcdef01u);
	for(char Byte : Names.m_Small.m_aName)
		EXPECT_EQ(Byte, 0);
	for(char Byte : Names.m_Large.m_aName)
		EXPECT_EQ(Byte, 0);
	for(char Byte : Names.m_Text.Buffer())
		EXPECT_EQ(Byte, 0);
}

TEST(EditorDocumentSchema, QuadValuesRetainEveryCoordinateColorAndBinding)
{
	static_assert(sizeof(CQuad) == 152);
	const auto Document = MakeCompleteDocument();
	const auto &Quads = std::get<CLayerQuadsValues>(DocumentLayer(Document, 7).m_Data);
	EXPECT_EQ(Quads.m_Image.m_Id, 1u);
	ASSERT_EQ(Quads.m_vQuads.size(), 1u);
	const auto &Quad = Quads.m_vQuads[0];
	EXPECT_NE(Quad.m_Id, 0u);
	for(int Index = 0; Index < 5; ++Index)
	{
		EXPECT_EQ(Quad.m_aPoints[Index].x, 1000 + Index);
		EXPECT_EQ(Quad.m_aPoints[Index].y, 2000 + Index);
		auto Changed = Quad;
		Changed.m_aPoints[Index].y++;
		EXPECT_NE(Changed, Quad);
	}
	for(int Index = 0; Index < 4; ++Index)
	{
		EXPECT_EQ(Quad.m_aColors[Index], (CColor{10 + Index, 20 + Index, 30 + Index, 40 + Index}));
		EXPECT_EQ(Quad.m_aTexcoords[Index], (CPoint{3000 + Index, 4000 + Index}));
		auto Changed = Quad;
		Changed.m_aColors[Index].a++;
		EXPECT_NE(Changed, Quad);
		Changed = Quad;
		Changed.m_aTexcoords[Index].x++;
		EXPECT_NE(Changed, Quad);
	}
	EXPECT_EQ(Quad.m_PosEnv.m_Id, 3u);
	EXPECT_EQ(Quad.m_PosEnvOffset, 125);
	EXPECT_EQ(Quad.m_ColorEnv.m_Id, 6u);
	EXPECT_EQ(Quad.m_ColorEnvOffset, -75);
	for(auto Member : {&CQuadValues::m_PosEnvOffset, &CQuadValues::m_ColorEnvOffset})
	{
		auto Changed = Quad;
		Changed.*Member += 1;
		EXPECT_NE(Changed, Quad);
	}
	for(auto Member : {&CQuadValues::m_PosEnv, &CQuadValues::m_ColorEnv})
	{
		auto Changed = Quad;
		(Changed.*Member).m_Id++;
		EXPECT_NE(Changed, Quad);
	}
	const CQuad Exported = Quad.Export([&](CDocumentReference Reference) { return Document.EnvelopeIndex(Reference); });
	const auto Imported = CQuadValues::Import(Exported, [&](int Index) { return Document.EnvelopeReference(Index); });
	auto Expected = Quad;
	Expected.m_Id = 0;
	EXPECT_EQ(Expected, Imported);
	EXPECT_EQ(Exported.m_PosEnv, 0);
	EXPECT_EQ(Exported.m_ColorEnv, 1);
	EXPECT_EQ(Imported.m_Id, 0u); // ABI projections never carry internal identity.
}

TEST(EditorDocumentSchema, SoundSourcesRetainBothShapesAndAllPlaybackAndEnvelopeValues)
{
	static_assert(sizeof(CSoundSource) == 52);
	const auto Document = MakeCompleteDocument();
	const auto &Sounds = std::get<CLayerSoundsValues>(DocumentLayer(Document, 8).m_Data);
	EXPECT_EQ(Sounds.m_Sound.m_Id, 2u);
	ASSERT_EQ(Sounds.m_vSources.size(), 2u);
	for(const auto &Source : Sounds.m_vSources)
	{
		EXPECT_NE(Source.m_Id, 0u);
		EXPECT_EQ(Source.m_Position, (CPoint{81, -23}));
		EXPECT_EQ(Source.m_Loop, 1);
		EXPECT_EQ(Source.m_Pan, 1);
		EXPECT_EQ(Source.m_TimeDelay, 12);
		EXPECT_EQ(Source.m_Falloff, 56);
		EXPECT_EQ(Source.m_PosEnv.m_Id, 3u);
		EXPECT_EQ(Source.m_PosEnvOffset, 234);
		EXPECT_EQ(Source.m_SoundEnv.m_Id, 9u);
		EXPECT_EQ(Source.m_SoundEnvOffset, 345);
		for(auto Member : {&CSoundSourceValues::m_Loop, &CSoundSourceValues::m_Pan, &CSoundSourceValues::m_TimeDelay, &CSoundSourceValues::m_Falloff, &CSoundSourceValues::m_PosEnvOffset, &CSoundSourceValues::m_SoundEnvOffset})
		{
			auto Changed = Source;
			Changed.*Member += 1;
			EXPECT_NE(Changed, Source);
		}
		for(auto Member : {&CSoundSourceValues::m_PosEnv, &CSoundSourceValues::m_SoundEnv})
		{
			auto Changed = Source;
			(Changed.*Member).m_Id++;
			EXPECT_NE(Changed, Source);
		}
		const CSoundSource Exported = Source.Export([&](CDocumentReference Reference) { return Document.EnvelopeIndex(Reference); });
		auto Expected = Source;
		Expected.m_Id = 0;
		EXPECT_EQ(Expected, CSoundSourceValues::Import(Exported, [&](int Index) { return Document.EnvelopeReference(Index); }));
		EXPECT_EQ(Exported.m_PosEnv, 0);
		EXPECT_EQ(Exported.m_SoundEnv, 2);
	}
	const auto &Rectangle = Sounds.m_vSources[0];
	EXPECT_EQ(Rectangle.m_Shape.m_Type, CSoundShape::SHAPE_RECTANGLE);
	EXPECT_EQ(Rectangle.m_Shape.m_Rectangle.m_Width, 456);
	EXPECT_EQ(Rectangle.m_Shape.m_Rectangle.m_Height, 567);
	auto ChangedRectangle = Rectangle;
	ChangedRectangle.m_Shape.m_Rectangle.m_Height++;
	EXPECT_NE(ChangedRectangle, Rectangle);
	const auto &Circle = Sounds.m_vSources[1];
	EXPECT_EQ(Circle.m_Shape.m_Type, CSoundShape::SHAPE_CIRCLE);
	EXPECT_EQ(Circle.m_Shape.m_Circle.m_Radius, 678);
	auto ChangedCircle = Circle;
	ChangedCircle.m_Shape.m_Circle.m_Radius++;
	EXPECT_NE(ChangedCircle, Circle);
}

TEST(EditorDocumentSchema, CanonicalReferencesSurviveResourceReorderAndExportByCurrentOrder)
{
	auto Document = MakeCompleteDocument();
	const auto Baseline = Document;
	const auto Quad = std::get<CLayerQuadsValues>(DocumentLayer(Document, 7).m_Data).m_vQuads[0];
	Document.m_Envelopes.Update([](auto &vEnvelopes) { std::reverse(vEnvelopes.begin(), vEnvelopes.end()); });
	std::string Error;
	ASSERT_TRUE(Document.Validate(Error)) << Error;
	EXPECT_EQ(std::get<CLayerQuadsValues>(DocumentLayer(Document, 7).m_Data).m_vQuads[0], Quad);
	EXPECT_EQ(Document.EnvelopeIndex(Quad.m_PosEnv), 2);
	EXPECT_EQ(Document.EnvelopeIndex(Quad.m_ColorEnv), 1);
	EXPECT_EQ(Baseline.EnvelopeIndex(Quad.m_PosEnv), 0);
	const auto Exported = Quad.Export([&](CDocumentReference Reference) { return Document.EnvelopeIndex(Reference); });
	EXPECT_EQ(Exported.m_PosEnv, 2);
	EXPECT_EQ(Exported.m_ColorEnv, 1);
	const auto Imported = CQuadValues::Import(Exported, [&](int Index) { return Document.EnvelopeReference(Index); });
	EXPECT_EQ(Imported.m_PosEnv.m_Id, 3u);
	EXPECT_EQ(Imported.m_ColorEnv.m_Id, 6u);
	EXPECT_FALSE(Document.EnvelopeReference(-1).IsSet());
	EXPECT_FALSE(Document.EnvelopeReference(3).IsSet());
	EXPECT_EQ(Document.EnvelopeIndex({99999}), -1);
	// A valid identity from the wrong object family is still an invalid binding.
	UpdateDocumentLayer(Document, 7, [](auto &Layer) { std::get<CLayerQuadsValues>(Layer.m_Data).m_vQuads[0].m_PosEnv = {1}; });
	EXPECT_FALSE(Document.Validate(Error));
	EXPECT_TRUE(Baseline.Validate(Error));
}

TEST(EditorDocumentExport, PersistedContentIgnoresInternalIdsAndHistoryOnlySettings)
{
	const auto Baseline = MakeCompleteDocument();
	const auto Key = EditorDocumentPersistedKey(Baseline);
	ASSERT_EQ(Key.size(), 64u);
	auto Changed = Baseline;
	const auto Remap = [](CDocumentReference &Reference) { if(Reference.IsSet()) Reference.m_Id += 1000; };
	Changed.m_Images.Update([](auto &vImages) { for(auto &Image : vImages) Image.m_Id += 1000; });
	Changed.m_Sounds.Update([](auto &vSounds) { for(auto &Sound : vSounds) Sound.m_Id += 1000; });
	Changed.m_Envelopes.Update([](auto &vEnvelopes) {
		for(auto &Record : vEnvelopes)
			Record.Update([](auto &Envelope) { Envelope.m_Id += 1000; for(auto &Point : Envelope.m_vPoints) Point.m_Id += 1000; });
	});
	Changed.m_Groups.Update([&](auto &vGroups) {
		for(auto &GroupRecord : vGroups)
			GroupRecord.Update([&](auto &Group) {
				Group.m_Id += 1000;
				Group.m_Layers.Update([&](auto &vLayers) {
					for(auto &LayerRecord : vLayers)
						LayerRecord.Update([&](auto &Layer) {
							Layer.m_Id += 1000;
							std::visit([&](auto &Data) {
								using T = std::decay_t<decltype(Data)>;
								if constexpr(std::is_same_v<T, CTileLayerDocumentValues>)
								{
									Remap(Data.m_Image);
									Remap(Data.m_ColorEnv);
									Data.m_LiveGameTiles = !Data.m_LiveGameTiles;
									Data.m_FillGameTile = TILE_SOLID;
									Data.m_AutomapperReference++;
								}
								else if constexpr(std::is_same_v<T, CLayerQuadsValues>)
								{
									Remap(Data.m_Image);
									for(auto &Quad : Data.m_vQuads)
									{
										Quad.m_Id += 1000;
										Remap(Quad.m_PosEnv);
										Remap(Quad.m_ColorEnv);
									}
								}
								else
								{
									Remap(Data.m_Sound);
									for(auto &Source : Data.m_vSources)
									{
										Source.m_Id += 1000;
										Remap(Source.m_PosEnv);
										Remap(Source.m_SoundEnv);
									}
								}
							},
								Layer.m_Data);
						});
				});
			});
	});
	EXPECT_NE(Changed, Baseline);
	EXPECT_EQ(EditorDocumentPersistedKey(Changed), Key);
	str_copy(Changed.m_MapInfo.m_Credits.Buffer(), "new credits");
	EXPECT_NE(EditorDocumentPersistedKey(Changed), Key);
	EXPECT_EQ(EditorDocumentPersistedKey(Baseline), Key);
}

TEST(EditorDocumentExport, FingerprintIncludesOnlyExportedBezierTangents)
{
	const auto Baseline = MakeCompleteDocument();
	const auto Key = EditorDocumentPersistedKey(Baseline);
	auto Changed = Baseline;
	Changed.m_Envelopes.Update([](auto &vEnvelopes) {
		vEnvelopes[0].Update([](auto &Envelope) {
			Envelope.m_vPoints[0].m_Bezier.m_aInTangentDeltaY[1] += 29;
			Envelope.m_vPoints[1].m_Bezier.m_aOutTangentDeltaY[1] += 41;
		});
	});
	EXPECT_NE(Changed, Baseline);
	EXPECT_EQ(EditorDocumentPersistedKey(Changed), Key);
	Changed.m_Envelopes.Update([](auto &vEnvelopes) {
		vEnvelopes[0].Update([](auto &Envelope) { Envelope.m_vPoints[0].m_Bezier.m_aOutTangentDeltaY[1] += 2; });
	});
	EXPECT_NE(EditorDocumentPersistedKey(Changed), Key);
	Changed = Baseline;
	Changed.m_Envelopes.Update([](auto &vEnvelopes) {
		vEnvelopes[0].Update([](auto &Envelope) { Envelope.m_vPoints[1].m_Bezier.m_aInTangentDeltaY[1] += 3; });
	});
	EXPECT_NE(EditorDocumentPersistedKey(Changed), Key);
}

TEST(EditorDocumentExport, PureNativeMapProjectionPreservesAllLayerVariants)
{
	const auto Document = MakeCompleteDocument();
	const auto Baseline = Document;
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::string Error;
	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(pStorage.get(), "roundtrip.map"));
		ASSERT_TRUE(WriteEditorDocument(Document, Writer, Error)) << Error;
		ASSERT_TRUE(Writer.Finish());
	}
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "roundtrip.map", IStorage::TYPE_SAVE));
	const auto *pInfo = static_cast<const CMapItemInfoSettings *>(Reader.FindItem(MAPITEMTYPE_INFO, 0));
	ASSERT_NE(pInfo, nullptr);
	EXPECT_STREQ(Reader.GetDataString(pInfo->m_Author), "mapper");
	EXPECT_STREQ(Reader.GetDataString(pInfo->m_Credits), "contributors");
	const auto *pSettings = static_cast<const char *>(Reader.GetData(pInfo->m_Settings));
	ASSERT_NE(pSettings, nullptr);
	EXPECT_STREQ(pSettings, "sv_test_cmd 1");
	EXPECT_STREQ(pSettings + str_length(pSettings) + 1, "sv_test_cmd 2");
	const std::array<int, 7> aRoles{TILESLAYERFLAG_GAME, TILESLAYERFLAG_FRONT, TILESLAYERFLAG_TELE, TILESLAYERFLAG_SPEEDUP, TILESLAYERFLAG_SWITCH, TILESLAYERFLAG_TUNE, 0};
	for(int Index = 0; Index < 7; ++Index)
	{
		const auto *pTiles = static_cast<const CMapItemLayerTilemap *>(Reader.FindItem(MAPITEMTYPE_LAYER, Index));
		ASSERT_NE(pTiles, nullptr);
		EXPECT_EQ(pTiles->m_Width, 2);
		EXPECT_EQ(pTiles->m_Height, 2);
		EXPECT_EQ(pTiles->m_Image, 0);
		EXPECT_EQ(pTiles->m_ColorEnv, 1);
		EXPECT_EQ(pTiles->m_ColorEnvOffset, 321);
		EXPECT_EQ(pTiles->m_Flags, aRoles[Index]);
		if(Index == 2)
		{
			const auto *pTele = static_cast<const CTeleTile *>(Reader.GetData(pTiles->m_Tele));
			ASSERT_NE(pTele, nullptr);
			EXPECT_EQ(pTele[3].m_Number, 9);
			EXPECT_EQ(pTele[3].m_Type, TILE_TELEIN);
		}
		else if(Index == 3)
		{
			const auto *pSpeedup = static_cast<const CSpeedupTile *>(Reader.GetData(pTiles->m_Speedup));
			ASSERT_NE(pSpeedup, nullptr);
			EXPECT_EQ(pSpeedup[3].m_Force, 12);
			EXPECT_EQ(pSpeedup[3].m_MaxSpeed, 34);
			EXPECT_EQ(pSpeedup[3].m_Angle, 267);
		}
		else if(Index == 4)
		{
			const auto *pSwitch = static_cast<const CSwitchTile *>(Reader.GetData(pTiles->m_Switch));
			ASSERT_NE(pSwitch, nullptr);
			EXPECT_EQ(pSwitch[3].m_Number, 8);
			EXPECT_EQ(pSwitch[3].m_Type, TILE_SWITCHOPEN);
			EXPECT_EQ(pSwitch[3].m_Delay, 15);
			EXPECT_EQ(pSwitch[3].m_Flags, TILEFLAG_ROTATE);
		}
		else if(Index == 5)
		{
			const auto *pTune = static_cast<const CTuneTile *>(Reader.GetData(pTiles->m_Tune));
			ASSERT_NE(pTune, nullptr);
			EXPECT_EQ(pTune[3].m_Number, 10);
			EXPECT_EQ(pTune[3].m_Type, TILE_TUNE);
		}
		else
		{
			const auto *pData = static_cast<const CTile *>(Reader.GetData(Index == 1 ? pTiles->m_Front : pTiles->m_Data));
			ASSERT_NE(pData, nullptr);
			EXPECT_EQ(pData[3].m_Index, 17);
			EXPECT_EQ(pData[3].m_Flags, TILEFLAG_XFLIP);
			EXPECT_EQ(pData[3].m_Skip, 0);
		}
	}
	const auto *pQuadLayer = static_cast<const CMapItemLayerQuads *>(Reader.FindItem(MAPITEMTYPE_LAYER, 7));
	ASSERT_NE(pQuadLayer, nullptr);
	const auto *pQuads = static_cast<const CQuad *>(Reader.GetDataSwapped(pQuadLayer->m_Data));
	ASSERT_NE(pQuads, nullptr);
	EXPECT_EQ(pQuads[0].m_aPoints[4], (CPoint{1004, 2004}));
	EXPECT_EQ(pQuads[0].m_PosEnv, 0);
	EXPECT_EQ(pQuads[0].m_ColorEnv, 1);
	const auto *pSoundLayer = static_cast<const CMapItemLayerSounds *>(Reader.FindItem(MAPITEMTYPE_LAYER, 8));
	ASSERT_NE(pSoundLayer, nullptr);
	const auto *pSources = static_cast<const CSoundSource *>(Reader.GetDataSwapped(pSoundLayer->m_Data));
	ASSERT_NE(pSources, nullptr);
	EXPECT_EQ(pSources[0].m_Shape.m_Rectangle.m_Width, 456);
	EXPECT_EQ(pSources[1].m_Shape.m_Circle.m_Radius, 678);
	EXPECT_EQ(pSources[1].m_SoundEnv, 2);
	const auto *pBezier = static_cast<const CEnvPointBezier *>(Reader.FindItem(MAPITEMTYPE_ENVPOINTS_BEZIER, 0));
	ASSERT_NE(pBezier, nullptr);
	EXPECT_EQ(pBezier[0].m_aInTangentDeltaY[0], 0);
	EXPECT_EQ(pBezier[0].m_aOutTangentDeltaY[0], 500);
	EXPECT_EQ(pBezier[1].m_aInTangentDeltaY[0], 300);
	EXPECT_EQ(pBezier[1].m_aOutTangentDeltaY[0], 0);
	EXPECT_EQ(Document, Baseline);
	Reader.Close();
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorDocumentExport, MissingResourceRejectsSaveWithoutChangingEditableDocument)
{
	auto Document = MakeCompleteDocument();
	Document.m_Sounds.Update([](auto &vSounds) { vSounds[0].m_Content.reset(); });
	const auto Baseline = Document;
	CDataFileWriter Writer;
	std::string Error;
	EXPECT_FALSE(WriteEditorDocument(Document, Writer, Error));
	EXPECT_FALSE(Error.empty());
	EXPECT_EQ(Document, Baseline);
	EXPECT_EQ(EditorDocumentPersistedKey(Document).size(), 64u);
}

TEST(EditorDocumentSchema, EnvelopeValuesKeepInactiveBezierTangentsAndPointIdentity)
{
	static_assert(sizeof(CEnvPoint_runtime) == 88);
	const auto Document = MakeCompleteDocument();
	ASSERT_EQ(Document.m_Envelopes.Read().size(), 3u);
	for(std::size_t Type = 0; Type < 3; ++Type)
	{
		const auto &Envelope = Document.m_Envelopes.Read()[Type].Read();
		EXPECT_EQ(Envelope.m_Type, static_cast<CEnvelopeValues::EType>(Type));
		EXPECT_FALSE(Envelope.m_Synchronized);
		EXPECT_STREQ(Envelope.m_aName, "envelope");
		ASSERT_EQ(Envelope.m_vPoints.size(), 2u);
		for(int Index = 0; Index < 2; ++Index)
		{
			const auto &Point = Envelope.m_vPoints[Index];
			EXPECT_EQ(Point.m_Time, CFixedTime(Index * 2000));
			EXPECT_EQ(Point.m_Curvetype, Index == 0 ? CURVETYPE_BEZIER : CURVETYPE_SMOOTH);
			for(int Channel = 0; Channel < 4; ++Channel)
			{
				EXPECT_EQ(Point.m_aValues[Channel], 100 + Index * 10 + Channel);
				EXPECT_EQ(Point.m_Bezier.m_aInTangentDeltaX[Channel], CFixedTime(200 + Channel));
				EXPECT_EQ(Point.m_Bezier.m_aInTangentDeltaY[Channel], 300 + Channel);
				EXPECT_EQ(Point.m_Bezier.m_aOutTangentDeltaX[Channel], CFixedTime(400 + Channel));
				EXPECT_EQ(Point.m_Bezier.m_aOutTangentDeltaY[Channel], 500 + Channel);
				for(auto Member : {&CEnvPointBezier::m_aInTangentDeltaY, &CEnvPointBezier::m_aOutTangentDeltaY})
				{
					auto Changed = Point;
					(Changed.m_Bezier.*Member)[Channel]++;
					EXPECT_NE(Changed, Point);
				}
				for(auto Member : {&CEnvPointBezier::m_aInTangentDeltaX, &CEnvPointBezier::m_aOutTangentDeltaX})
				{
					auto Changed = Point;
					(Changed.m_Bezier.*Member)[Channel] += CFixedTime(1);
					EXPECT_NE(Changed, Point);
				}
			}
		}
		auto Reordered = Envelope;
		const auto FirstId = Reordered.m_vPoints[0].m_Id;
		Reordered.m_vPoints[0].m_Time = CFixedTime(3000);
		std::sort(Reordered.m_vPoints.begin(), Reordered.m_vPoints.end());
		EXPECT_EQ(Reordered.m_vPoints[1].m_Id, FirstId);
		EXPECT_EQ(Envelope.m_vPoints[0].m_Time, CFixedTime(0));
	}
}

TEST(EditorDocumentSchema, CompleteGraphValidatesAndRejectsBrokenIdentityReferencesAndPayloads)
{
	const auto Baseline = MakeCompleteDocument();
	std::string Error;
	ASSERT_TRUE(Baseline.Validate(Error)) << Error;
	auto Changed = Baseline;
	UpdateDocumentLayer(Changed, 1, [&](auto &Layer) { Layer.m_Id = DocumentLayer(Baseline, 0).m_Id; });
	EXPECT_FALSE(Changed.Validate(Error));
	EXPECT_NE(Error.find("identity"), std::string::npos);
	Changed = Baseline;
	UpdateDocumentLayer(Changed, 7, [](auto &Layer) { std::get<CLayerQuadsValues>(Layer.m_Data).m_vQuads[0].m_PosEnv = {999999}; });
	EXPECT_FALSE(Changed.Validate(Error));
	Changed = Baseline;
	UpdateDocumentLayer(Changed, 2, [](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Auxiliary = std::monostate{}; });
	EXPECT_FALSE(Changed.Validate(Error));
	Changed = Baseline;
	UpdateDocumentLayer(Changed, 3, [](auto &Layer) { std::get<CLayerSpeedupValues>(std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Auxiliary).m_SpeedupTiles.Resize(3, 2); });
	EXPECT_FALSE(Changed.Validate(Error));
	Changed = Baseline;
	UpdateDocumentLayer(Changed, 1, [](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Resize(3, 2); });
	EXPECT_FALSE(Changed.Validate(Error));
	Changed = Baseline;
	UpdateDocumentLayer(Changed, 8, [](auto &Layer) { std::get<CLayerSoundsValues>(Layer.m_Data).m_vSources[0].m_Shape.m_Type = 19; });
	EXPECT_FALSE(Changed.Validate(Error));
	Changed = Baseline;
	Changed.m_Envelopes.Update([](auto &vEnvelopes) { vEnvelopes[0].Update([](auto &Envelope) { std::swap(Envelope.m_vPoints[0], Envelope.m_vPoints[1]); }); });
	EXPECT_FALSE(Changed.Validate(Error));
	EXPECT_TRUE(Baseline.Validate(Error));
	EXPECT_TRUE(Error.empty());
}

TEST(EditorDocumentSchema, GraphAcceptsEveryPixelFormatAndUnavailableContent)
{
	for(const auto Format : {CImageInfo::FORMAT_RGB, CImageInfo::FORMAT_RGBA, CImageInfo::FORMAT_R, CImageInfo::FORMAT_RA})
	{
		auto Document = MakeCompleteDocument();
		Document.m_Images.Update([&](auto &vImages) {
			vImages[0].m_Format = Format;
			vImages[0].m_Content.emplace(std::vector<std::uint8_t>(4 * CImageInfo::PixelSize(Format), 17));
		});
		std::string Error;
		EXPECT_TRUE(Document.Validate(Error)) << Error;
		Document.m_Images.Update([](auto &vImages) { vImages[0].m_Content.emplace(std::vector<std::uint8_t>{1}); });
		EXPECT_FALSE(Document.Validate(Error));
		Document.m_Images.Update([](auto &vImages) { vImages[0].m_Content.reset(); });
		EXPECT_TRUE(Document.Validate(Error)) << Error;
	}
}

TEST(EditorDocumentSchema, DocumentHistoryRestoresCompleteGraphAndSharesUnchangedRecords)
{
	using CDocumentHistory = CRevisionHistory<CEditorDocumentValues>;
	const auto Baseline = MakeCompleteDocument();
	auto History = CDocumentHistory::Create(Baseline, {10, 1024 * 1024}, [](const auto &Document) {
		std::string Error;
		return Document.Validate(Error); }, [](const auto &) { return std::string("test projection"); }, [](const auto &Document, auto &Usage) { Document.Account(Usage); });
	ASSERT_TRUE(History);
	auto Edit = History->BeginEdit("mixed graph edit", ECategory::MAP);
	ASSERT_TRUE(Edit);
	History->Update(*Edit, [](auto &Document) {
		str_copy(Document.m_MapInfo.m_Author.Buffer(), "new author");
		Document.m_vSettings.erase(Document.m_vSettings.begin());
		UpdateDocumentLayer(Document, 7, [](auto &Layer) { std::get<CLayerQuadsValues>(Layer.m_Data).m_vQuads[0].m_aPoints[4].x = 9000; });
		Document.m_Envelopes.Update([](auto &vEnvelopes) { vEnvelopes[0].Update([](auto &Envelope) { Envelope.m_vPoints[1].m_Bezier.m_aOutTangentDeltaY[3] = 8000; }); });
	});
	EXPECT_EQ(History->Commit(*Edit), ECommitResult::CHANGED);
	const auto Changed = History->PinCurrent();
	EXPECT_STREQ(Changed->m_MapInfo.m_Author.Buffer(), "new author");
	EXPECT_EQ(std::get<CLayerQuadsValues>(DocumentLayer(*Changed, 7).m_Data).m_vQuads[0].m_aPoints[4].x, 9000);
	EXPECT_EQ(Changed->m_Envelopes.Read()[0].Read().m_vPoints[1].m_Bezier.m_aOutTangentDeltaY[3], 8000);
	EXPECT_TRUE(Changed->m_Images.SharesStorageWith(Baseline.m_Images));
	EXPECT_TRUE(Changed->m_Sounds.SharesStorageWith(Baseline.m_Sounds));
	EXPECT_TRUE(Changed->m_Groups.Read()[0].Read().m_Layers.Read()[0].SharesStorageWith(Baseline.m_Groups.Read()[0].Read().m_Layers.Read()[0]));
	EXPECT_FALSE(Changed->m_Groups.Read()[0].Read().m_Layers.Read()[7].SharesStorageWith(Baseline.m_Groups.Read()[0].Read().m_Layers.Read()[7]));
	ASSERT_TRUE(History->RequestUndo());
	ASSERT_TRUE(History->PublishPending());
	EXPECT_EQ(History->Current(), Baseline);
	EXPECT_EQ(std::get<CLayerQuadsValues>(DocumentLayer(History->Current(), 7).m_Data).m_vQuads[0].m_aPoints[4].x, 1004);
	EXPECT_EQ(History->Current().m_Envelopes.Read()[0].Read().m_vPoints[1].m_Bezier.m_aOutTangentDeltaY[3], 503);
	ASSERT_TRUE(History->RequestRedo());
	ASSERT_TRUE(History->PublishPending());
	EXPECT_EQ(History->Current(), *Changed);
	CStorageUsage Usage;
	Baseline.Account(Usage);
	const auto FirstBytes = Usage.Bytes();
	Baseline.Account(Usage);
	EXPECT_EQ(Usage.Bytes(), FirstBytes);
	Changed->Account(Usage);
	EXPECT_GT(Usage.Bytes(), FirstBytes);
}

TEST(EditorDocumentSchema, IdentityAllocationNeverRewindsWithCopiedValues)
{
	CDocumentIdentityAllocator Ids;
	CQuadValues Original;
	Original.m_Id = *Ids.Allocate();
	const auto Snapshot = Original;
	Original.m_Id = *Ids.Allocate();
	Original = Snapshot;
	EXPECT_EQ(Original.m_Id, 1u);
	EXPECT_EQ(*Ids.Allocate(), 3u);
	EXPECT_TRUE(Ids.ReserveThrough(100));
	EXPECT_EQ(*Ids.Allocate(), 101u);
	EXPECT_TRUE(Ids.ReserveThrough(2));
	EXPECT_EQ(*Ids.Allocate(), 102u);
	EXPECT_FALSE(Ids.ReserveThrough(std::numeric_limits<std::uint64_t>::max()));
	EXPECT_EQ(*Ids.Allocate(), 103u);
}

TEST(EditorDocumentSchema, WrapperOwnershipAndSelectionResolutionUseValuesAndIdentities)
{
	static_assert(!std::is_copy_constructible_v<CEnvelope>);
	static_assert(!std::is_copy_assignable_v<CEnvelope>);
	static_assert(std::is_copy_constructible_v<CEnvelopeValues>);
	static_assert(std::is_copy_assignable_v<CEnvelopeValues>);
	std::vector<CQuadValues> vQuads(3);
	vQuads[0].m_Id = 10;
	vQuads[1].m_Id = 20;
	vQuads[2].m_Id = 30;
	const auto Identity = [](const auto &Quad) { return Quad.m_Id; };
	EXPECT_EQ(DocumentIndexById(vQuads, 20, Identity), 1);
	std::swap(vQuads[0], vQuads[1]);
	EXPECT_EQ(DocumentIndexById(vQuads, 20, Identity), 0);
	vQuads.erase(vQuads.begin());
	EXPECT_EQ(DocumentIndexById(vQuads, 20, Identity), -1);
	EXPECT_EQ(DocumentIndexById(vQuads, 0, Identity), -1);
	EXPECT_EQ(DocumentIndexById(vQuads, 30, Identity), 1);
	CDocumentSessionValues Session;
	Session.m_Layers[10].m_Visible = false;
	Session.m_Layers[10].m_Readonly = true;
	Session.m_Groups[40].m_Collapse = true;
	EXPECT_FALSE(Session.m_Layers[10].m_Visible);
	EXPECT_TRUE(Session.m_Layers[10].m_Readonly);
	EXPECT_TRUE(Session.m_Groups[40].m_Collapse);
	EXPECT_TRUE(Session.m_Layers[50].m_Visible);
	EXPECT_FALSE(Session.m_Layers[50].m_Readonly);
	Session.Prune({10, 40});
	EXPECT_TRUE(Session.m_Layers.contains(10));
	EXPECT_FALSE(Session.m_Layers.contains(50));
	EXPECT_TRUE(Session.m_Groups.contains(40));
	Session.Prune({});
	EXPECT_TRUE(Session.m_Layers.empty());
	EXPECT_TRUE(Session.m_Groups.empty());
}

TEST(EditorDocumentExport, FingerprintReusesUnchangedProjectionAndRecomputesOneChunk)
{
	auto Document = MakeCompleteDocument();
	Document.m_Images.Update([](auto &vImages) { vImages[0].m_External = 0; });
	UpdateDocumentLayer(Document, 6, [](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Resize(65, 33); });
	CEditorDocumentFingerprint Cache;
	const auto BaselineKey = Cache.Key(Document);
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 2u);
	EXPECT_EQ(Cache.Statistics().m_ImagesAnalyzed, 1u);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 12u);
	EXPECT_EQ(BaselineKey, EditorDocumentPersistedKey(Document));
	EXPECT_EQ(Cache.Key(Document), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 0u);
	EXPECT_EQ(Cache.Statistics().m_ImagesAnalyzed, 0u);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 0u);

	auto Changed = Document;
	str_copy(Changed.m_MapInfo.m_Author.Buffer(), "another mapper");
	EXPECT_NE(Cache.Key(Changed), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 0u);
	EXPECT_EQ(Cache.Statistics().m_ImagesAnalyzed, 0u);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 0u);
	UpdateDocumentLayer(Changed, 6, [](auto &Layer) {
		std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Update(32 * 65 + 64, [](auto &Tile) { Tile.m_Index = 13; });
	});
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 1u);
	EXPECT_EQ(Cache.Key(Document), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 1u);
}

TEST(EditorDocumentExport, FingerprintResourceIdentityIsOnlyACacheOptimization)
{
	auto Document = MakeCompleteDocument();
	Document.m_Images.Update([](auto &vImages) { vImages[0].m_External = 0; });
	CEditorDocumentFingerprint Cache;
	const auto BaselineKey = Cache.Key(Document);
	auto Independent = Document;
	Independent.m_Images.Update([](auto &vImages) {
		const auto Bytes = vImages[0].m_Content->Bytes();
		vImages[0].m_Content.emplace(Bytes);
	});
	EXPECT_EQ(Cache.Key(Independent), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 1u);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 0u);
	EXPECT_EQ(Cache.Key(Document), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 0u);

	Independent.m_Images.Update([](auto &vImages) { vImages[0].m_Content.emplace(std::vector<std::uint8_t>(16, 255)); });
	EXPECT_NE(Cache.Key(Independent), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 1u);
	EXPECT_EQ(Cache.Statistics().m_ImagesAnalyzed, 1u);
	EXPECT_EQ(Cache.Key(Independent), EditorDocumentPersistedKey(Independent));
}

TEST(EditorDocumentExport, FingerprintOpacityChangesOnlyRelevantTileProjection)
{
	auto Document = MakeCompleteDocument();
	Document.m_Images.Update([](auto &vImages) {
		vImages[0].m_Width = vImages[0].m_Height = 16;
		vImages[0].m_Content.emplace(std::vector<std::uint8_t>(16 * 16 * 4, 255));
	});
	UpdateDocumentLayer(Document, 6, [](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Color.a = 255; });
	CEditorDocumentFingerprint Cache;
	const auto BaselineKey = Cache.Key(Document);
	auto Changed = Document;
	Changed.m_Images.Update([](auto &vImages) {
		std::vector<std::uint8_t> vPixels(vImages[0].m_Content->Bytes().begin(), vImages[0].m_Content->Bytes().end());
		vPixels[17 * 4 + 3] = 0;
		vImages[0].m_Content.emplace(vPixels);
	});
	EXPECT_NE(Cache.Key(Changed), BaselineKey);
	EXPECT_EQ(Cache.Statistics().m_ImagesAnalyzed, 1u);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 1u);
	EXPECT_EQ(Cache.Statistics().m_TileLeafHashes, 0u);
	EXPECT_EQ(Cache.Statistics().m_TileBranchHashes, 0u);
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	Changed.m_Images.Update([](auto &vImages) { str_copy(vImages[0].m_aName, "renamed"); });
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ImagesAnalyzed, 0u);
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 0u);
}

TEST(EditorDocumentExport, FingerprintRebindsPlaneTypesAndDimensionsAfterReorder)
{
	auto Document = MakeCompleteDocument();
	CEditorDocumentFingerprint Cache;
	const auto BaselineKey = Cache.Key(Document);
	Document.m_Groups.Update([](auto &vGroups) {
		vGroups[0].Update([](auto &Group) {
			Group.m_Layers.Update([](auto &vLayers) { std::reverse(vLayers.begin(), vLayers.end()); });
		});
	});
	EXPECT_NE(Cache.Key(Document), BaselineKey);
	EXPECT_EQ(Cache.Key(Document), EditorDocumentPersistedKey(Document));
	UpdateDocumentLayer(Document, 2, [](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Resize(33, 17); });
	EXPECT_EQ(Cache.Key(Document), EditorDocumentPersistedKey(Document));
	EXPECT_EQ(Cache.Statistics().m_TileChunks, 2u);
}

TEST(EditorDocumentExport, FingerprintWeakResourceEntriesDoNotRetainRemovedSound)
{
	CEditorDocumentFingerprint Cache;
	std::weak_ptr<const void> Identity;
	{
		auto Document = MakeCompleteDocument();
		Identity = Document.m_Sounds.Read()[0].m_Content->StorageIdentity();
		EXPECT_FALSE(Cache.Key(Document).empty());
		EXPECT_FALSE(Identity.expired());
	}
	EXPECT_TRUE(Identity.expired());
	Cache.Clear();
	const auto Document = MakeCompleteDocument();
	EXPECT_EQ(Cache.Key(Document), EditorDocumentPersistedKey(Document));
	EXPECT_EQ(Cache.Statistics().m_BlobScans, 1u);
}

TEST(EditorDocumentExport, FingerprintReportsPinnedRuntimePayloadSeparately)
{
	const auto Document = MakeCompleteDocument();
	CEditorDocumentFingerprint Cache;
	Cache.Key(Document);
	editor_history::CStorageUsage Standalone;
	Cache.Account(Standalone);
	EXPECT_GT(Standalone.Bytes(), 0u);
	editor_history::CStorageUsage Combined;
	Document.Account(Combined);
	const auto Before = Combined.Bytes();
	Cache.Account(Combined);
	EXPECT_GT(Combined.Bytes(), Before);
	EXPECT_LT(Combined.Bytes() - Before, Standalone.Bytes());
}

TEST(EditorDocumentExport, ObjectBlockCacheUsesImmutableOwnersAndEnvelopeOrder)
{
	const auto Baseline = MakeCompleteDocument();
	CEditorDocumentFingerprint Cache;
	const auto Key = Cache.Key(Baseline);
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 2u);
	EXPECT_EQ(Cache.Key(Baseline), Key);
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 0u);
	auto Changed = Baseline;
	str_copy(Changed.m_MapInfo.m_Author.Buffer(), "changed author");
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 0u);
	UpdateDocumentLayer(Changed, 6, [](auto &Layer) {
		std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Update(0, [](auto &Tile) { ++Tile.m_Index; });
	});
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 0u);
	Changed.m_Envelopes.Update([](auto &vEnvelopes) {
		vEnvelopes[0].Update([](auto &Envelope) { ++Envelope.m_vPoints[0].m_aValues[0]; });
	});
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 0u);
	Changed.m_Envelopes.Update([](auto &vEnvelopes) { std::reverse(vEnvelopes.begin(), vEnvelopes.end()); });
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 2u);
	EXPECT_EQ(Cache.Key(Baseline), Key);
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 2u);
	Changed = Baseline;
	UpdateDocumentLayer(Changed, 7, [](auto &Layer) { ++std::get<CLayerQuadsValues>(Layer.m_Data).m_vQuads[0].m_aPoints[0].x; });
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 1u);
	UpdateDocumentLayer(Changed, 8, [](auto &Layer) { ++std::get<CLayerSoundsValues>(Layer.m_Data).m_vSources[0].m_Position.x; });
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 1u);
	Changed.m_Groups.Update([](auto &vGroups) {
		vGroups[0].Update([](auto &Group) {
			Group.m_Layers.Update([](auto &vLayers) { std::swap(vLayers[7], vLayers[8]); });
		});
	});
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 2u);
	EXPECT_EQ(Cache.Key(Changed), EditorDocumentPersistedKey(Changed));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 0u);
}

TEST(EditorDocumentExport, ObjectBlockCacheDoesNotRetainLayerLifetime)
{
	CEditorDocumentFingerprint Cache;
	std::weak_ptr<const CLayerDocumentValues> Owner;
	{
		const auto Document = MakeCompleteDocument();
		Owner = Document.m_Groups.Read()[0].Read().m_Layers.Read()[7].Pin();
		EXPECT_FALSE(Cache.Key(Document).empty());
	}
	EXPECT_TRUE(Owner.expired());
	auto Document = MakeCompleteDocument();
	UpdateDocumentLayer(Document, 7, [](auto &Layer) { std::get<CLayerQuadsValues>(Layer.m_Data).m_vQuads.clear(); });
	UpdateDocumentLayer(Document, 8, [](auto &Layer) { std::get<CLayerSoundsValues>(Layer.m_Data).m_vSources.clear(); });
	EXPECT_EQ(Cache.Key(Document), EditorDocumentPersistedKey(Document));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 2u);
	Cache.Clear();
	EXPECT_EQ(Cache.Key(Document), EditorDocumentPersistedKey(Document));
	EXPECT_EQ(Cache.Statistics().m_ObjectBlocks, 2u);
}

TEST(EditorDocumentExport, ObjectProjectionKeepsNativeReferencesAndEmptyCompatibilityPayloads)
{
	class CObjectOutput : public IEditorDocumentOutput
	{
	public:
		std::vector<unsigned char> m_Bytes;
		int AddItem(int, int, std::size_t, const void *) override { return 0; }
		int AddData(std::size_t Size, const void *pData) override
		{
			const auto *pBytes = static_cast<const unsigned char *>(pData);
			m_Bytes.assign(pBytes, pBytes + Size);
			return 0;
		}
		int AddDataSwapped(std::size_t Size, const void *pData) override { return AddData(Size, pData); }
	} Output;
	auto Document = MakeCompleteDocument();
	Document.m_Envelopes.Update([](auto &vEnvelopes) { std::reverse(vEnvelopes.begin(), vEnvelopes.end()); });
	Output.AddLayerObjects(Document.m_Groups.Read()[0].Read().m_Layers.Read()[7], Document);
	ASSERT_EQ(Output.m_Bytes.size(), sizeof(CQuad));
	CQuad Quad;
	mem_copy(&Quad, Output.m_Bytes.data(), sizeof(Quad));
	EXPECT_EQ(Quad.m_PosEnv, 2);
	EXPECT_EQ(Quad.m_ColorEnv, 1);
	EXPECT_EQ(Quad.m_aPoints[0].x, 1000);
	Output.AddLayerObjects(Document.m_Groups.Read()[0].Read().m_Layers.Read()[8], Document);
	ASSERT_EQ(Output.m_Bytes.size(), 2 * sizeof(CSoundSource));
	CSoundSource Source;
	mem_copy(&Source, Output.m_Bytes.data(), sizeof(Source));
	EXPECT_EQ(Source.m_PosEnv, 2);
	EXPECT_EQ(Source.m_SoundEnv, 0);
	EXPECT_EQ(Source.m_Position.x, 81);
	UpdateDocumentLayer(Document, 7, [](auto &Layer) { std::get<CLayerQuadsValues>(Layer.m_Data).m_vQuads.clear(); });
	UpdateDocumentLayer(Document, 8, [](auto &Layer) { std::get<CLayerSoundsValues>(Layer.m_Data).m_vSources.clear(); });
	Output.AddLayerObjects(Document.m_Groups.Read()[0].Read().m_Layers.Read()[7], Document);
	EXPECT_EQ(Output.m_Bytes, std::vector<unsigned char>(sizeof(CQuad)));
	Output.AddLayerObjects(Document.m_Groups.Read()[0].Read().m_Layers.Read()[8], Document);
	EXPECT_EQ(Output.m_Bytes, std::vector<unsigned char>(sizeof(CSoundSource)));
}

TEST(EditorHistory, FailedTraversalCanBeAbandonedWithoutChangingTimeline)
{
	auto History = MakeHistory();
	SetValue(*History, 91);
	ASSERT_TRUE(History->RequestUndo());
	EXPECT_TRUE(History->HasPendingPublication());
	EXPECT_FALSE(History->PublishPendingWith([](const auto &) { return false; }));
	EXPECT_TRUE(History->AbandonPendingTraversal());
	EXPECT_FALSE(History->HasPendingPublication());
	EXPECT_EQ(History->Current().m_Value, 91);
	EXPECT_EQ(History->Cursor(), 1u);
	auto Edit = History->BeginEdit("Edit after failure", ECategory::MAP);
	ASSERT_TRUE(Edit);
	ASSERT_TRUE(History->RequestCancel());
	EXPECT_FALSE(History->AbandonPendingTraversal());
	EXPECT_TRUE(History->PublishPending());
	EXPECT_TRUE(History->Clear());
}

namespace
{
	using CTestCoordinator = CEditCoordinator<CTestDocument>;
	class CCoordinatorFixture
	{
	public:
		CTestDocument m_Runtime;
		int m_Captures = 0;
		int m_Restores = 0;
		int m_Begins = 0;
		int m_Errors = 0;
		bool m_FailCapture = false;
		bool m_FailRestore = false;
		std::function<void()> m_OnError;
		std::function<void()> m_OnRestore;
		std::unique_ptr<CTestCoordinator> m_pCoordinator = std::make_unique<CTestCoordinator>(MakeHistory(), [this](const auto &, std::string &) -> std::optional<CTestDocument> {
				++m_Captures;
				return m_FailCapture ? std::nullopt : std::optional(m_Runtime); }, [this](const auto &Document) {
				++m_Restores;
				if(m_OnRestore)
					m_OnRestore();
				if(m_FailRestore)
					return false;
				m_Runtime = Document;
				return true; }, [this] { ++m_Begins; }, [this](const char *) {
				++m_Errors;
				if(m_OnError)
					m_OnError(); });
	};
}

TEST(EditorEditCoordinator, RetainsBaselineBeforeMutationAndCapturesOnlyCompletion)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	auto Owner = Edits.BeginEdit("Drag value", ECategory::MAP);
	ASSERT_TRUE(Owner);
	EXPECT_EQ(Fixture.m_Begins, 1);
	EXPECT_EQ(Fixture.m_Captures, 0);
	ASSERT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 99; }));
	ASSERT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 80; }));
	EXPECT_EQ(Fixture.m_Captures, 0);
	EXPECT_EQ(Edits.History().Current().m_Value, 64);
	EXPECT_EQ(Edits.Complete(*Owner, EEditCompletion::POINTER_RELEASE), ECommitResult::CHANGED);
	EXPECT_FALSE(Owner->Active());
	EXPECT_EQ(Fixture.m_Captures, 1);
	EXPECT_EQ(Fixture.m_Restores, 0);
	ASSERT_EQ(Edits.History().Revisions().size(), 2u);
	EXPECT_EQ(Edits.History().Current().m_Value, 80);
	EXPECT_EQ(Edits.History().Revisions()[0].m_pRoot->m_Value, 64);
	EXPECT_EQ(Edits.LastCompletion(), EEditCompletion::POINTER_RELEASE);
	EXPECT_TRUE(Edits.RequestUndo());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 80);
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
	EXPECT_EQ(Fixture.m_Restores, 1);
}

TEST(EditorEditCoordinator, NestedHelpersExplicitlyJoinAndUnrelatedOwnersCannotInterleave)
{
	CCoordinatorFixture Fixture;
	CCoordinatorFixture OtherFixture;
	auto &Edits = *Fixture.m_pCoordinator;
	auto Owner = Edits.BeginEdit("Stroke and dependent effects", ECategory::MAP);
	ASSERT_TRUE(Owner);
	EXPECT_FALSE(Edits.BeginEdit("Unrelated widget", ECategory::ENVELOPE));
	EXPECT_FALSE(OtherFixture.m_pCoordinator->Update(*Owner, [] {}));
	EXPECT_TRUE(Edits.Update(*Owner, [&] {
		Fixture.m_Runtime.m_Value = 70;
		EXPECT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_AuthoringSetting = 17; }));
		EXPECT_FALSE(Edits.RequestUndo());
		EXPECT_FALSE(Edits.PublishAtSafePoint());
		EXPECT_EQ(Edits.Complete(*Owner, EEditCompletion::ACCEPT), ECommitResult::INVALID_OWNER);
	}));
	EXPECT_EQ(Edits.Complete(*Owner, EEditCompletion::POINTER_RELEASE), ECommitResult::CHANGED);
	EXPECT_EQ(Edits.History().Current().m_AuthoringSetting, 17);
	EXPECT_EQ(Edits.History().Revisions().size(), 2u);
}

TEST(EditorEditCoordinator, ActiveUndoCancelsOnlyTheDraftAndNoopPreservesRedo)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	ASSERT_EQ(Edits.Edit("First", ECategory::MAP, [&] { Fixture.m_Runtime.m_Value = 71; }), ECommitResult::CHANGED);
	ASSERT_TRUE(Edits.RequestUndo());
	ASSERT_TRUE(Edits.PublishAtSafePoint());
	auto Owner = Edits.BeginEdit("Preview", ECategory::MAP);
	ASSERT_TRUE(Owner);
	ASSERT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 200; }));
	EXPECT_FALSE(Edits.RequestRedo());
	EXPECT_FALSE(Edits.CaptureSave("automatic.map", ESaveKind::AUTOMATIC));
	EXPECT_TRUE(Edits.RequestUndo());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 200);
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
	EXPECT_EQ(Edits.History().Cursor(), 0u);
	EXPECT_TRUE(Edits.History().CanRedo());
	EXPECT_EQ(Edits.LastCancellation(), EEditCancellation::UNDO);
	EXPECT_EQ(Edits.Edit("Unchanged gesture", ECategory::MAP, [&] {
		Fixture.m_Runtime.m_Value = 120;
		Fixture.m_Runtime.m_Value = 64;
	}),
		ECommitResult::UNCHANGED);
	EXPECT_TRUE(Edits.History().CanRedo());
	EXPECT_TRUE(Edits.RequestRedo());
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 71);
}

TEST(EditorEditCoordinator, InterruptionAndAbandonedOwnerCancelAtSafePoint)
{
	for(const auto Reason : {EEditCancellation::ESCAPE, EEditCancellation::CAPTURE_LOST, EEditCancellation::FOCUS_LOST, EEditCancellation::INVALID_INPUT})
	{
		CCoordinatorFixture Fixture;
		auto &Edits = *Fixture.m_pCoordinator;
		auto Owner = Edits.BeginEdit("Interrupted", ECategory::MAP);
		ASSERT_TRUE(Owner);
		ASSERT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 123; }));
		ASSERT_TRUE(Edits.CancelActive(Reason));
		Owner.reset();
		EXPECT_EQ(Edits.LastCancellation(), Reason);
		EXPECT_EQ(Fixture.m_Runtime.m_Value, 123);
		EXPECT_TRUE(Edits.PublishAtSafePoint());
		EXPECT_FALSE(Edits.PublishAtSafePoint());
		EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
		EXPECT_EQ(Edits.History().Revisions().size(), 1u);
		EXPECT_EQ(Fixture.m_Captures, 0);
	}
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	{
		auto Owner = Edits.BeginEdit("Abandoned", ECategory::MAP);
		ASSERT_TRUE(Owner);
		EXPECT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 88; }));
	}
	EXPECT_EQ(Edits.LastCancellation(), EEditCancellation::OWNER_DESTROYED);
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
}

TEST(EditorEditCoordinator, CaptureAndValidationFailuresRestoreTheRetainedBaseline)
{
	for(const bool FailCapture : {false, true})
	{
		CCoordinatorFixture Fixture;
		auto &Edits = *Fixture.m_pCoordinator;
		Fixture.m_FailCapture = FailCapture;
		EXPECT_EQ(Edits.Edit("Rejected", ECategory::MAP, [&] { Fixture.m_Runtime.m_Value = -1; }), ECommitResult::FAILED);
		EXPECT_EQ(Edits.History().Current().m_Value, 64);
		EXPECT_EQ(Edits.History().Revisions().size(), 1u);
		EXPECT_EQ(Fixture.m_Runtime.m_Value, -1);
		EXPECT_EQ(Fixture.m_Errors, 1);
		EXPECT_TRUE(Edits.HasPendingPublication());
		EXPECT_FALSE(Edits.BeginEdit("Cannot interleave", ECategory::MAP));
		Fixture.m_FailRestore = true;
		EXPECT_FALSE(Edits.PublishAtSafePoint());
		EXPECT_FALSE(Edits.Clear());
		EXPECT_FALSE(Edits.AbandonPendingTraversal());
		Fixture.m_FailRestore = false;
		EXPECT_TRUE(Edits.PublishAtSafePoint());
		EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
		EXPECT_TRUE(Edits.Clear());
	}
}

TEST(EditorEditCoordinator, PublicationBlocksLifecycleReentryUntilRuntimeBindingSucceeds)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	ASSERT_EQ(Edits.Edit("First", ECategory::MAP, [&] { Fixture.m_Runtime.m_Value = 90; }), ECommitResult::CHANGED);
	auto Owner = Edits.BeginEdit("Preview", ECategory::MAP);
	ASSERT_TRUE(Owner);
	ASSERT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 100; }));
	ASSERT_TRUE(Owner->Cancel(EEditCancellation::ESCAPE));
	Fixture.m_OnRestore = [&] {
		EXPECT_TRUE(Edits.Publishing());
		EXPECT_EQ(Edits.History().Cursor(), 1u);
		EXPECT_FALSE(Edits.BeginEdit("Callback", ECategory::MAP));
		EXPECT_FALSE(Edits.Update(*Owner, [] {}));
		EXPECT_EQ(Edits.CompleteActive(EEditCompletion::VALID_BLUR), ECommitResult::INVALID_OWNER);
		EXPECT_FALSE(Owner->Cancel(EEditCancellation::OWNER_DESTROYED));
		EXPECT_FALSE(Edits.RequestUndo());
		EXPECT_FALSE(Edits.RequestRedo());
		EXPECT_FALSE(Edits.RequestRevision(0));
		EXPECT_FALSE(Edits.AbandonPendingTraversal());
		EXPECT_FALSE(Edits.Clear());
		EXPECT_FALSE(Edits.PublishAtSafePoint());
		EXPECT_FALSE(Edits.CaptureSave("reentrant.map", ESaveKind::MANUAL));
	};
	Fixture.m_FailRestore = true;
	EXPECT_FALSE(Edits.PublishAtSafePoint());
	EXPECT_TRUE(Edits.HasActiveEdit());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 100);
	Fixture.m_FailRestore = false;
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_FALSE(Edits.HasActiveEdit());
	EXPECT_FALSE(Edits.Publishing());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 90);
	EXPECT_EQ(Edits.LastCancellation(), EEditCancellation::ESCAPE);
}

TEST(EditorEditCoordinator, FailedTraversalCanBeDismissedAndOwnersAreDocumentBound)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	ASSERT_EQ(Edits.Edit("First", ECategory::MAP, [&] { Fixture.m_Runtime.m_Value = 90; }), ECommitResult::CHANGED);
	ASSERT_TRUE(Edits.RequestUndo());
	Fixture.m_FailRestore = true;
	EXPECT_FALSE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 90);
	EXPECT_TRUE(Edits.AbandonPendingTraversal());
	EXPECT_FALSE(Edits.HasPendingPublication());
	EXPECT_EQ(Edits.History().Cursor(), 1u);
	EXPECT_TRUE(Edits.CaptureSave("after-failure.map", ESaveKind::MANUAL));
	Fixture.m_FailRestore = false;
	ASSERT_TRUE(Edits.RequestUndo()); // a later explicit request retries normally
	ASSERT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
	ASSERT_EQ(Edits.Edit("Resumed edit", ECategory::MAP, [&] { Fixture.m_Runtime.m_Value = 77; }), ECommitResult::CHANGED);
	EXPECT_FALSE(Edits.History().CanRedo());
	auto Owner = Edits.BeginEdit("New owner", ECategory::MAP);
	ASSERT_TRUE(Owner);
	Fixture.m_pCoordinator.reset();
	EXPECT_FALSE(Owner->Active());
	EXPECT_FALSE(Owner->Cancel(EEditCancellation::ESCAPE));
	CCoordinatorFixture Replacement;
	EXPECT_FALSE(Replacement.m_pCoordinator->Update(*Owner, [] {}));
}

TEST(EditorEditCoordinator, ManualSaveSettlesOwnerAndCompletionStaysBoundToCapturedContent)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	auto Owner = Edits.BeginEdit("Typed metadata", ECategory::MAP);
	ASSERT_TRUE(Owner);
	ASSERT_TRUE(Edits.Update(*Owner, [&] { Fixture.m_Runtime.m_Value = 128; }));
	EXPECT_FALSE(Edits.CaptureSave("manual.map", ESaveKind::MANUAL));
	EXPECT_EQ(Edits.CompleteActive(EEditCompletion::MANUAL_SAVE), ECommitResult::CHANGED);
	auto Ticket = Edits.CaptureSave("manual.map", ESaveKind::MANUAL);
	ASSERT_TRUE(Ticket);
	EXPECT_EQ(Ticket->m_pRoot->m_Value, 128);
	ASSERT_EQ(Edits.Edit("Later edit", ECategory::SETTINGS, [&] { Fixture.m_Runtime.m_Value = 129; }), ECommitResult::CHANGED);
	EXPECT_TRUE(Edits.CompleteSave(*Ticket, true));
	EXPECT_TRUE(Edits.History().Dirty(ESaveKind::MANUAL));
	EXPECT_TRUE(Edits.RequestUndo());
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_FALSE(Edits.History().Dirty(ESaveKind::MANUAL));
	EXPECT_TRUE(Edits.History().Dirty(ESaveKind::AUTOMATIC));
	CCoordinatorFixture Replacement;
	EXPECT_FALSE(Replacement.m_pCoordinator->CompleteSave(*Ticket, true));
}

#if defined(__cpp_exceptions)
TEST(EditorEditCoordinator, DiagnosticExceptionsCannotPreventRequiredRollback)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	Fixture.m_OnError = [] { throw std::runtime_error("notification failed"); };
	Fixture.m_FailCapture = true;
	EXPECT_EQ(Edits.Edit("Rejected edit", ECategory::MAP, [&] { Fixture.m_Runtime.m_Value = 100; }), ECommitResult::FAILED);
	EXPECT_EQ(Fixture.m_Errors, 1);
	EXPECT_TRUE(Edits.HasPendingPublication());
	EXPECT_EQ(Edits.History().Current().m_Value, 64);
	Fixture.m_OnRestore = [] { throw std::runtime_error("binding failed"); };
	EXPECT_FALSE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Errors, 2);
	EXPECT_TRUE(Edits.HasPendingPublication());
	Fixture.m_OnRestore = {};
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
}

TEST(EditorEditCoordinator, InnerFailureCancelsWholeOwnerAndRestoreExceptionCanBeRetried)
{
	CCoordinatorFixture Fixture;
	auto &Edits = *Fixture.m_pCoordinator;
	auto Owner = Edits.BeginEdit("Dependent failure", ECategory::MAP);
	ASSERT_TRUE(Owner);
	EXPECT_FALSE(Edits.Update(*Owner, [&] {
		Fixture.m_Runtime.m_Value = 777;
		EXPECT_FALSE(Edits.Update(*Owner, [] { throw std::bad_alloc(); }));
	}));
	EXPECT_EQ(Fixture.m_Errors, 1);
	EXPECT_TRUE(Edits.HasPendingPublication());
	Fixture.m_OnRestore = [] { throw std::bad_alloc(); };
	EXPECT_FALSE(Edits.PublishAtSafePoint());
	EXPECT_FALSE(Edits.Publishing());
	EXPECT_EQ(Fixture.m_Errors, 2);
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 777);
	EXPECT_EQ(Edits.History().Current().m_Value, 64);
	Fixture.m_OnRestore = {};
	EXPECT_TRUE(Edits.PublishAtSafePoint());
	EXPECT_EQ(Fixture.m_Runtime.m_Value, 64);
}
#endif

TEST(EditorHistory, LiveLimitsPreserveUndoPairSaveMarkersAndExistingRedo)
{
	auto pHistory = MakeHistory();
	ASSERT_TRUE(pHistory->InitializeLoadedBaseline(0));
	for(int Value = 65; Value < 75; ++Value)
		SetValue(*pHistory, Value);
	ASSERT_TRUE(pHistory->SetRetentionLimits({2, 64 * 1024 * 1024}));
	EXPECT_EQ(pHistory->Revisions().size(), 3u);
	ASSERT_TRUE(pHistory->RequestUndo());
	ASSERT_TRUE(pHistory->PublishPending());
	const auto Before = pHistory->PinCurrent();
	ASSERT_TRUE(pHistory->SetRetentionLimits({1, 1}));
	EXPECT_EQ(pHistory->PinCurrent(), Before);
	EXPECT_TRUE(pHistory->CanUndo());
	EXPECT_TRUE(pHistory->CanRedo());
	EXPECT_TRUE(pHistory->Oversized());
	EXPECT_TRUE(pHistory->Dirty(ESaveKind::MANUAL));
	SetValue(*pHistory, 100);
	EXPECT_EQ(pHistory->Revisions().size(), 2u);
	EXPECT_FALSE(pHistory->CanRedo());
	EXPECT_TRUE(pHistory->Oversized());
	ASSERT_TRUE(pHistory->RequestUndo());
	ASSERT_TRUE(pHistory->PublishPending());
	EXPECT_EQ(pHistory->Current(), *Before);
}

TEST(EditorHistory, PreferenceChangesWaitForActiveOwnerAndPreserveSharedSaveService)
{
	auto pHistory = MakeHistory();
	auto pSaves = std::make_shared<CSaveState<CTestDocument>>();
	ASSERT_TRUE(pHistory->UseSaveState(pSaves));
	auto Edit = pHistory->BeginEdit("Active", ECategory::MAP);
	ASSERT_TRUE(Edit);
	EXPECT_FALSE(pHistory->SetRetentionLimits({1, 1}));
	EXPECT_FALSE(pHistory->UseSaveState(std::make_shared<CSaveState<CTestDocument>>()));
	ASSERT_TRUE(pHistory->Update(*Edit, [](auto &Document) { Document.m_Value = 128; }));
	ASSERT_EQ(pHistory->Commit(*Edit), ECommitResult::CHANGED);
	auto Ticket = pHistory->CaptureSave("test.map", ESaveKind::MANUAL);
	ASSERT_TRUE(Ticket);
	ASSERT_TRUE(pSaves->Complete(*Ticket, true));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
	ASSERT_TRUE(pHistory->SetRetentionLimits({1, 1}));
	EXPECT_FALSE(pHistory->Dirty(ESaveKind::MANUAL));
}

TEST(EditorHistoryStorage, SparseHierarchyAndFiftyRevisionsHaveBoundedWork)
{
	for(const std::size_t Edge : {256u, 4096u, 32768u})
	{
		CTestDocument Initial;
		Initial.m_Tiles = *CTestPlane::Create(Edge, Edge);
		CStorageUsage Empty;
		Initial.m_Tiles.Account(Empty);
		EXPECT_EQ(Empty.Bytes(), 0u);
		auto History = CTestHistory::Create(std::move(Initial), {50, 64 * 1024 * 1024}, [](const auto &) { return true; }, [](const auto &Document) { return std::to_string(Document.m_Value); }, [](const auto &Document, auto &Usage) { Document.Account(Usage); });
		ASSERT_NE(History, nullptr);
		for(int Revision = 0; Revision < 50; ++Revision)
		{
			auto Edit = History->BeginEdit("Sparse edit", ECategory::MAP);
			ASSERT_TRUE(Edit);
			ASSERT_TRUE(History->Update(*Edit, [&](auto &Document) {
				Document.m_Value = Revision;
				Document.m_Tiles.Set(Edge - 1, Edge - 1, {Revision + 1, 0});
			}));
			ASSERT_EQ(History->Commit(*Edit), ECommitResult::CHANGED);
			EXPECT_LT(History->AccountingVisits(), 150u);
			CStorageUsage Fresh;
			History->AccountRetainedPayload(Fresh);
			EXPECT_EQ(History->RetainedPayloadBytes(), Fresh.Bytes());
			std::size_t Changed = 0;
			const auto &Previous = History->Revisions()[History->Cursor() - 1].m_pRoot->m_Tiles;
			const auto Visits = History->Current().m_Tiles.VisitChangedChunks(&Previous, [&](auto...) { ++Changed; });
			EXPECT_EQ(Changed, 1u);
			EXPECT_LE(Visits, 1u + 16u * History->Current().m_Tiles.TreeDepth());
		}
		EXPECT_LT(History->RetainedBytes(), 512u * 1024u);
		auto Save = History->CaptureSave("pinned.map", ESaveKind::MANUAL);
		ASSERT_TRUE(Save);
		ASSERT_TRUE(History->SetRetentionLimits({1, 1}));
		EXPECT_EQ(History->Revisions().size(), 2u);
		EXPECT_LT(History->AccountingVisits(), 8000u);
		EXPECT_EQ(Save->m_pRoot->m_Tiles.At(Edge - 1, Edge - 1).m_Index, 50);
	}
}

TEST(EditorHistoryStorage, IncrementalUnionMatchesFreshAccountingAndRollsBack)
{
	auto First = *CTestPlane::Create(32768, 32768);
	First.Set(0, 0, {1, 2});
	auto Second = First;
	Second.Set(32767, 32767, {3, 4});
	CStorageUsage Incremental;
	First.Account(Incremental);
	const auto FirstBytes = Incremental.Bytes();
	{
		CStorageUsage::CTransaction Transaction(Incremental);
		Second.Account(Incremental);
		CStorageUsage Fresh;
		First.Account(Fresh);
		Second.Account(Fresh);
		EXPECT_EQ(Incremental.Bytes(), Fresh.Bytes());
		Incremental.Removing(true);
		First.Account(Incremental);
		CStorageUsage Remaining;
		Second.Account(Remaining);
		EXPECT_EQ(Incremental.Bytes(), Remaining.Bytes());
		// Simulates a later allocation failure: destruction rolls back both paths.
	}
	EXPECT_EQ(Incremental.Bytes(), FirstBytes);
	{
		CStorageUsage::CTransaction Transaction(Incremental);
		Second.Account(Incremental);
		Incremental.Removing(true);
		First.Account(Incremental);
		Transaction.Commit();
	}
	CStorageUsage Remaining;
	Second.Account(Remaining);
	EXPECT_EQ(Incremental.Bytes(), Remaining.Bytes());
	{
		CStorageUsage::CTransaction Transaction(Incremental);
		Incremental.Removing(true);
		Second.Account(Incremental);
		Transaction.Commit();
	}
	EXPECT_EQ(Incremental.Bytes(), 0u);
	EXPECT_EQ(Incremental.Allocations(), 0u);
}

TEST(EditorHistoryStorage, BulkImportNormalizesPartialChunksAndResizeStride)
{
	CEditorTilePlane<CTileValues> Plane(67, 35);
	std::vector<CTile> vCells(Plane.Size());
	for(std::size_t Index = 0; Index < vCells.size(); ++Index)
		vCells[Index] = {static_cast<unsigned char>(Index % 239), static_cast<unsigned char>(Index % 16), 99, 77};
	Plane.Assign(std::span<const CTile>(vCells));
	for(std::size_t Index = 0; Index < vCells.size(); ++Index)
		EXPECT_EQ(Plane[Index], CTileValues(vCells[Index]));
	const auto Original = Plane;
	ASSERT_TRUE(Plane.Resize(34, 33));
	ASSERT_TRUE(Plane.Resize(101, 65));
	for(int Y = 0; Y < Plane.Height(); ++Y)
		for(int X = 0; X < Plane.Width(); ++X)
			EXPECT_EQ(Plane[Y * Plane.Width() + X], X < 34 && Y < 33 ? Original[Y * 67 + X] : CTileValues{});
}

TEST(EditorDocumentExport, MassiveSparseFingerprintHashesOnlyChangedTreePaths)
{
	for(const int Edge : {256, 4096, 32768})
	{
		auto Document = MakeCompleteDocument();
		UpdateDocumentLayer(Document, 6, [&](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles = CEditorTilePlane<CTileValues>(Edge, Edge); });
		CEditorDocumentFingerprint Cache;
		const auto InitialKey = Cache.Key(Document);
		EXPECT_LT(Cache.Statistics().m_TileLeafHashes, 20u);
		EXPECT_EQ(Cache.Key(Document), InitialKey);
		EXPECT_EQ(Cache.Statistics().m_TileLeafHashes, 0u);
		EXPECT_EQ(Cache.Statistics().m_TileBranchHashes, 0u);
		for(int Revision = 1; Revision <= 50; ++Revision)
		{
			UpdateDocumentLayer(Document, 6, [&](auto &Layer) {
				std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Set(static_cast<std::size_t>(Edge) * Edge - 1, CTile{static_cast<unsigned char>(Revision), 0, 0, 0});
			});
			const auto Key = Cache.Key(Document);
			EXPECT_EQ(Cache.Statistics().m_TileLeafHashes, 1u);
			EXPECT_LE(Cache.Statistics().m_TileBranchHashes, 5u);
			EXPECT_NE(Key, InitialKey);
			EXPECT_EQ(Key, EditorDocumentPersistedKey(Document));
		}
		UpdateDocumentLayer(Document, 6, [&](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Set(static_cast<std::size_t>(Edge) * Edge - 1, {}); });
		EXPECT_EQ(Cache.Key(Document), InitialKey);
	}
}

TEST(EditorDocumentExport, ObjectFingerprintChecksGenerationOfMutableStorage)
{
	auto Document = MakeCompleteDocument();
	CEditorDocumentFingerprint Cache;
	const auto Before = Cache.Key(Document);
	// No external revision pins the layer; a weak owner alone would miss this write.
	Document.m_Groups.Update([](auto &vGroups) {
		vGroups[0].Update([](auto &Group) {
			Group.m_Layers.Update([](auto &vLayers) {
				for(auto &Layer : vLayers)
					if(std::holds_alternative<CLayerQuadsValues>(Layer.Read().m_Data))
						Layer.Update([](auto &Value) { std::get<CLayerQuadsValues>(Value.m_Data).m_vQuads[0].m_aPoints[0].x += 7; });
			});
		});
	});
	EXPECT_NE(Cache.Key(Document), Before);
	EXPECT_EQ(Cache.Key(Document), EditorDocumentPersistedKey(Document));
}

TEST(EditorHistoryStorage, EmptyRawRowsPreserveConversionAndIgnoreNonsemanticBytes)
{
	CEditorTilePlane<CSpeedupTileValues> Speedup(65, 33);
	std::vector<CSpeedupTile> vSpeedup(Speedup.Size());
	Speedup.Assign(std::span<const CSpeedupTile>(vSpeedup));
	editor_history::CStorageUsage Empty;
	Speedup.Account(Empty);
	EXPECT_EQ(Empty.Bytes(), 0u);
	for(auto &Cell : vSpeedup)
		Cell.m_MustBe0 = 255;
	Speedup.Assign(std::span<const CSpeedupTile>(vSpeedup));
	Speedup.Account(Empty);
	EXPECT_EQ(Empty.Bytes(), 0u);
	vSpeedup.back() = {7, 19, TILE_SPEED_BOOST, 255, -135};
	Speedup.Assign(std::span<const CSpeedupTile>(vSpeedup));
	for(std::size_t Index = 0; Index < Speedup.Size(); ++Index)
		EXPECT_EQ(Speedup[Index], CSpeedupTileValues(vSpeedup[Index]));
	CEditorTilePlane<CTileValues> Tiles(65, 33);
	std::vector<CTile> vTiles(Tiles.Size(), CTile{0, TILEFLAG_OPAQUE, 255, 255});
	Tiles.Assign(std::span<const CTile>(vTiles));
	Tiles.Account(Empty);
	EXPECT_EQ(Empty.Bytes(), 0u);

	struct CInput
	{
	};
	struct CValue
	{
		int m_Value = 0;
		CValue() = default;
		CValue(CInput) :
			m_Value(1) {}
		bool operator==(const CValue &) const = default;
	};
	CEditorTilePlane<CValue> NonzeroDefault(1, 1);
	const CInput Input{};
	NonzeroDefault.Assign(std::span(&Input, 1));
	EXPECT_EQ(NonzeroDefault[0].m_Value, 1);
}

TEST(EditorHistoryStorage, EmptyDimensionsSkipBulkImport)
{
	using CPlane = editor_history::CTilePlane<CTileValues, 32>;
	const auto Large = std::numeric_limits<std::size_t>::max() / 2;
	for(const auto &Size : {std::pair<std::size_t, std::size_t>{0, Large}, {Large, 0}})
	{
		auto Plane = CPlane::Create(Size.first, Size.second);
		ASSERT_TRUE(Plane.has_value());
		std::size_t Rows = 0;
		Plane->AssignRows([&](auto, auto, auto) { ++Rows; return 0u; });
		EXPECT_EQ(Rows, 0u);
		editor_history::CStorageUsage Storage;
		Plane->Account(Storage);
		EXPECT_EQ(Storage.Bytes(), 0u);
	}
}

TEST(EditorHistoryStorage, SparseDerivedPlanesVisitStoredChunksAndNormalizeEdges)
{
	const auto Convert = [](const CSwitchTileValues &Cell) {
		return Cell.m_Type == TILE_SWITCHOPEN ? CTileValues(CTile{Cell.m_Type, Cell.m_Flags, 0, 0}) : CTileValues{};
	};
	for(const int Edge : {67, 32768})
	{
		CEditorTilePlane<CSwitchTileValues> Source(Edge, Edge);
		Source.Set(0, CSwitchTile{7, TILE_SWITCHOPEN, 255, 3});
		Source.Set(Source.Size() - 1, CSwitchTile{8, TILE_SWITCHOPEN, TILEFLAG_ROTATE, 5});
		CEditorTilePlane<CTileValues> Derived(Edge, Edge);
		Derived.Set(Edge + 1, CTile{TILE_SOLID, 0, 0, 0});
		std::size_t Visits = 0;
		Derived.AssignSparse(Source, [&](const auto &Cell) { ++Visits; return Convert(Cell); });
		EXPECT_LE(Visits, 2u * 32u * 32u);
		EXPECT_EQ(Derived[0], CTileValues(CTile{TILE_SWITCHOPEN, TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE, 0, 0}));
		EXPECT_EQ(Derived[Edge + 1], CTileValues{});
		EXPECT_EQ(Derived[Source.Size() - 1], CTileValues(CTile{TILE_SWITCHOPEN, TILEFLAG_ROTATE, 0, 0}));
		editor_history::CStorageUsage Storage;
		Derived.Account(Storage);
		EXPECT_LT(Storage.Bytes(), 16384u);
	}
	CEditorTilePlane<CSwitchTileValues> Source(67, 35);
	std::vector<CSwitchTile> vInput(Source.Size());
	for(std::size_t Index = 0; Index < vInput.size(); ++Index)
		vInput[Index] = {static_cast<unsigned char>(Index), static_cast<unsigned char>(Index % 3 == 0 ? TILE_SWITCHOPEN : 255), static_cast<unsigned char>(Index), 17};
	Source.Assign(std::span<const CSwitchTile>(vInput));
	CEditorTilePlane<CTileValues> Derived(67, 35);
	Derived.AssignSparse(Source, Convert);
	for(std::size_t Index = 0; Index < vInput.size(); ++Index)
		EXPECT_EQ(Derived[Index], Index % 3 == 0 ? CTileValues(CTile{TILE_SWITCHOPEN, vInput[Index].m_Flags, 0, 0}) : CTileValues{});
	ASSERT_TRUE(Derived.Resize(69, 37));
	EXPECT_EQ(Derived[35 * 69 + 66], CTileValues{});
	EXPECT_EQ(Derived[34 * 69 + 67], CTileValues{});
}

TEST(EditorDocumentExport, PlatformDigestMatchesPortableSha256AcrossBlocksAndRepeatedKeys)
{
	auto Document = MakeCompleteDocument();
	CEditorDocumentFingerprint Platform;
	CEditorDocumentFingerprint Portable(CEditorDocumentFingerprint::EHashBackend::PORTABLE);
	for(const std::size_t Size : {0u, 1u, 55u, 64u, 65u, 4096u, 1024u * 1024u})
	{
		std::vector<std::uint8_t> vBytes(Size);
		for(std::size_t Index = 0; Index < Size; ++Index)
			vBytes[Index] = static_cast<std::uint8_t>(Index * 37 + 19);
		Document.m_Sounds.Update([&](auto &vSounds) { vSounds[0].m_Content.emplace(vBytes); });
		UpdateDocumentLayer(Document, 6, [&](auto &Layer) {
			auto &Tiles = std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles;
			Tiles.Resize(65, 33);
			Tiles.Set(32 * 65 + 64, CTile{static_cast<unsigned char>(Size % 251 + 1), TILEFLAG_ROTATE, 0, 0});
		});
		EXPECT_EQ(Platform.Key(Document), Portable.Key(Document));
		EXPECT_EQ(Platform.Key(Document), Portable.Key(Document));
		Portable.Clear();
		EXPECT_EQ(Platform.Key(Document), Portable.Key(Document));
	}
}

TEST(EditorDocumentExport, UnusedOpacityCannotAffectPartialChunkOrTreePadding)
{
	auto Document = MakeCompleteDocument();
	Document.m_Images.Update([](auto &vImages) {
		vImages[0].m_External = 1;
		vImages[0].m_Width = vImages[0].m_Height = 16;
		vImages[0].m_Content.emplace(std::vector<std::uint8_t>(16 * 16 * 4, 255));
	});
	UpdateDocumentLayer(Document, 6, [](auto &Layer) {
		auto &Tiles = std::get<CTileLayerDocumentValues>(Layer.m_Data);
		Tiles.m_Color.a = 255;
		Tiles.m_Tiles.Resize(33, 17);
		const std::vector<CTile> vTiles(Tiles.m_Tiles.Size(), CTile{17, 0, 0, 0});
		Tiles.m_Tiles.Assign(std::span<const CTile>(vTiles));
	});
	CEditorDocumentFingerprint Cache;
	const auto InitialKey = Cache.Key(Document);
	Document.m_Images.Update([](auto &vImages) {
		std::vector<std::uint8_t> vPixels(16 * 16 * 4, 255);
		vPixels[3] = 0; // Only tile zero, which appears solely in padding.
		vImages[0].m_Content.emplace(vPixels);
	});
	EXPECT_EQ(Cache.Key(Document), InitialKey);
	EXPECT_EQ(EditorDocumentPersistedKey(Document), InitialKey);
	UpdateDocumentLayer(Document, 6, [](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Set(0, {}); });
	const auto TransparentZero = Cache.Key(Document);
	Document.m_Images.Update([](auto &vImages) { vImages[0].m_Content.emplace(std::vector<std::uint8_t>(16 * 16 * 4, 255)); });
	EXPECT_NE(Cache.Key(Document), TransparentZero);
}

TEST(EditorDocumentExport, SparseNativePlanesPreserveEveryCellAndRejectOversizedBlocks)
{
	class CNativeOutput : public IEditorDocumentOutput
	{
	public:
		std::vector<unsigned char> m_vBytes;
		int AddItem(int, int, std::size_t, const void *) override { return 0; }
		int AddData(std::size_t Size, const void *pData) override
		{
			if(Size == 0 || pData == nullptr)
				return -1;
			const auto *pBytes = static_cast<const unsigned char *>(pData);
			m_vBytes.assign(pBytes, pBytes + Size);
			return 0;
		}
		int AddDataSwapped(std::size_t Size, const void *pData) override { return AddData(Size, pData); }
	} Output;
	const auto Check = [&]<typename TValue, typename TNative>(TValue Value, TNative) {
		CEditorTilePlane<TValue> Plane(65, 33);
		Plane.Set(0, Value);
		Plane.Set(32 * 65 + 64, Value);
		std::array<unsigned char, 256> aFlags{};
		aFlags[0] = TILEFLAG_OPAQUE;
		aFlags[1] = TILEFLAG_OPAQUE;
		ASSERT_EQ(Output.AddTilePlane(&Plane, aFlags), 0);
		ASSERT_EQ(Output.m_vBytes.size(), Plane.Size() * sizeof(TNative));
		for(std::size_t Index = 0; Index < Plane.Size(); ++Index)
		{
			auto Expected = static_cast<TNative>(Plane[Index]);
			if constexpr(std::is_same_v<TValue, CTileValues>)
				Expected.m_Flags |= aFlags[Expected.m_Index];
			EXPECT_EQ(memcmp(&Expected, Output.m_vBytes.data() + Index * sizeof(TNative), sizeof(TNative)), 0) << Index;
		}
	};
	Check(CTileValues(CTile{1, TILEFLAG_ROTATE, 0, 0}), CTile{});
	Check(CTeleTileValues(CTeleTile{3, TILE_TELEIN}), CTeleTile{});
	Check(CSpeedupTileValues(CSpeedupTile{12, 34, TILE_SPEED_BOOST, 0, 267}), CSpeedupTile{});
	Check(CSwitchTileValues(CSwitchTile{8, TILE_SWITCHOPEN, TILEFLAG_ROTATE, 15}), CSwitchTile{});
	Check(CTuneTileValues(CTuneTile{10, TILE_TUNE}), CTuneTile{});
	ASSERT_EQ(Output.AddEmptyTiles(65 * 33), 0);
	EXPECT_EQ(Output.m_vBytes, std::vector<unsigned char>(65 * 33 * sizeof(CTile)));
	const CEditorTilePlane<CTileValues> Huge(100000, 100000);
	EXPECT_EQ(Output.AddTilePlane(&Huge), -1);
	EXPECT_EQ(Output.AddEmptyTiles(static_cast<std::size_t>(std::numeric_limits<int>::max())), -1);
}

TEST(EditorDocumentExport, CacheObservationPinsImmutableSourcesAndMatchesFreshUnion)
{
	auto Document = MakeCompleteDocument();
	Document.m_vSettings.reserve(Document.m_vSettings.size() + 100);
	CEditorDocumentFingerprint Cache;
	EXPECT_FALSE(Cache.Key(Document).empty());
	editor_history::CStorageUsage Fresh;
	Document.Account(Fresh);
	Cache.Account(Fresh);
	editor_history::CStorageObservation Observation;
	Observation.Observe(Document);
	Cache.Observe(Observation);
	std::size_t ObservedBytes = 0;
	std::promise<void> Mutated;
	auto Ready = Mutated.get_future();
	std::thread Worker([&] {
		Ready.wait();
		editor_history::CStorageUsage Usage;
		Observation.Account(Usage);
		ObservedBytes = Usage.Bytes();
	});
	UpdateDocumentLayer(Document, 6, [](auto &Layer) {
		std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Set(0, CTileValues(CTile{TILE_DEATH, 0, 0, 0}));
	});
	EXPECT_FALSE(Cache.Key(Document).empty());
	Document.m_vSettings.reserve(Document.m_vSettings.capacity() + 10000);
	Cache.Clear();
	editor_history::CStorageUsage Changed;
	Document.Account(Changed);
	EXPECT_GT(Changed.Bytes(), Fresh.Bytes());
	Document = {}; // the observation now outlives both source graphs
	Mutated.set_value();
	Worker.join();
	EXPECT_EQ(ObservedBytes, Fresh.Bytes());
}

TEST(EditorDocumentSchema, DraftObservationCountsOriginalSettingsCapacityAndIdentity)
{
	using CDocumentHistory = CRevisionHistory<CEditorDocumentValues>;
	auto History = CDocumentHistory::Create(MakeCompleteDocument(), {10, 1024 * 1024}, [](const auto &Document) {
		std::string Error;
		return Document.Validate(Error); }, [](const auto &) { return std::string("test projection"); }, [](const auto &Document, auto &Usage) { Document.Account(Usage); });
	ASSERT_TRUE(History);
	auto Edit = History->BeginEdit("reserved settings", ECategory::SETTINGS);
	ASSERT_TRUE(Edit);
	ASSERT_TRUE(History->Update(*Edit, [](auto &Draft) { Draft.m_vSettings.reserve(1000); }));
	editor_history::CStorageUsage Fresh;
	History->AccountRetainedPayload(Fresh);
	History->AccountDraft(Fresh);
	editor_history::CStorageObservation Observation;
	History->ObserveDraft(Observation);
	editor_history::CStorageUsage Observed;
	History->AccountRetainedPayload(Observed);
	Observation.Account(Observed);
	EXPECT_EQ(Observed.Bytes(), Fresh.Bytes());
	// Applying the observation to the direct union must not count a copied
	// settings buffer, even though its size is much smaller than its capacity.
	Observation.Account(Fresh);
	EXPECT_EQ(Observed.Bytes(), Fresh.Bytes());
}

TEST(EditorRenderSpans, SparseRowsMatchScalarCellsIncludingPartialChunksAndRestoredPlanes)
{
	constexpr int Width = 137, Height = 69;
	CEditorTilePlane<CTileValues> Plane(Width, Height);
	std::vector<CTile> vScalar(Width * Height);
	std::minstd_rand Random(719);
	for(int Index = 0; Index < Width * Height; ++Index)
	{
		if(Random() % 19 == 0)
		{
			vScalar[Index] = {static_cast<unsigned char>(Random() % 255 + 1), static_cast<unsigned char>(Random() & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE)), 0, 0};
			Plane.Set(Index, vScalar[Index]);
		}
	}
	const auto Original = Plane;
	const auto Check = [&](const auto &Cells, const auto &vExpected) {
		CRenderTileSource<CTile> Source(Cells);
		CTileChunkCursor Cursor;
		std::array<CTile, 13> aBuffer; // Force spans smaller than a chunk too.
		for(int Y = 0; Y < Cells.Height(); ++Y)
			for(const int StartX : {0, 1, 31, 32, 63, 128})
				for(std::size_t X = StartX; X < static_cast<std::size_t>(Cells.Width());)
				{
					const auto Row = Source.ReadRow(X, Y, Cells.Width(), aBuffer, &Cursor);
					ASSERT_GT(Row.m_Length, 0u);
					ASSERT_LE(Row.m_Length, Cells.Width() - X);
					for(std::size_t Offset = 0; Offset < Row.m_Length; ++Offset)
					{
						const CTile Actual = Row.m_Cells.empty() ? CTile{} : Row.m_Cells[Offset];
						const auto Expected = vExpected[Y * Width + X + Offset];
						EXPECT_EQ(Actual.m_Index, Expected.m_Index);
						EXPECT_EQ(Actual.m_Flags, Expected.m_Flags);
						EXPECT_EQ(Actual.m_Skip, 0);
						EXPECT_EQ(Actual.m_MustBe0, 0);
					}
					X += Row.m_Length;
				}
		EXPECT_LE(Cursor.m_TreeLookups, 15u);
	};
	Check(Plane, vScalar);
	Plane.Set(32 * Width + 31, CTile{99, TILEFLAG_ROTATE, 0, 0});
	Check(Original, vScalar);
	Plane = Original;
	ASSERT_TRUE(Plane.Resize(130, 65));
	Check(Plane, vScalar);
}

TEST(EditorRenderSpans, EmptySubtreesSkipGrowingDimensionsWithoutAllocations)
{
	for(const int Edge : {1000, 10000, 100000})
	{
		CEditorTilePlane<CTileValues> Plane(Edge, Edge);
		const auto Empty = Plane.ReadRow(1, Edge / 2, Edge);
		EXPECT_TRUE(Empty.m_Cells.empty());
		EXPECT_EQ(Empty.m_Length, Edge - 1u);
		editor_history::CStorageUsage EmptyUsage;
		Plane.Account(EmptyUsage);
		EXPECT_EQ(EmptyUsage.Bytes(), 0u);
		Plane.Set(static_cast<std::size_t>(Edge / 2) * Edge + Edge - 1, CTile{1, 0, 0, 0});
		editor_history::CStorageUsage Before;
		Plane.Account(Before);
		std::size_t Spans = 0, Cells = 0;
		for(std::size_t X = 0; X < static_cast<std::size_t>(Edge);)
		{
			const auto Row = Plane.ReadRow(X, Edge / 2, Edge);
			++Spans;
			Cells += Row.m_Cells.size();
			X += Row.m_Length;
		}
		EXPECT_LE(Spans, 16u * (Plane.Plane().TreeDepth() + 1));
		EXPECT_LE(Cells, 32u);
		editor_history::CStorageUsage After;
		Plane.Account(After);
		EXPECT_EQ(After.Bytes(), Before.Bytes());
	}
}

TEST(EditorRenderSpans, BoundedCursorReusesChunksAndHandlesCollisions)
{
	CEditorTilePlane<CTileValues> Plane(1024, 32);
	for(int X = 0; X < Plane.Width(); X += 32)
		Plane.Set(X, CTile{static_cast<unsigned char>(X / 32 + 1), TILEFLAG_ROTATE, 0, 0});
	CTileChunkCursor Cursor;
	for(int Y = 0; Y < Plane.Height(); ++Y)
		for(int X = 0; X < Plane.Width(); X += 32)
		{
			const auto Row = Plane.ReadRowCached(X, Y, Plane.Width(), Cursor);
			ASSERT_EQ(Row.m_Cells.size(), 32u);
			EXPECT_EQ(Row.m_Cells[0].m_Index, Y == 0 ? X / 32 + 1 : 0);
			EXPECT_EQ(Row.m_Cells[0].m_Flags, Y == 0 ? TILEFLAG_ROTATE : 0);
		}
	EXPECT_EQ(Cursor.m_TreeLookups, 32u);
	EXPECT_LE(sizeof(Cursor), 1600u);

	// Direct-map collisions must replace both populated and empty entries.
	CEditorTilePlane<CTileValues> Colliding(4128, 1);
	Colliding.Set(0, CTile{19, TILEFLAG_ROTATE, 0, 0});
	Colliding.Set(2048, CTile{27, TILEFLAG_XFLIP, 0, 0});
	CTileChunkCursor CollisionCursor;
	for(const int X : {0, 2048, 0, 4096, 2048})
	{
		const auto Row = Colliding.ReadRowCached(X, 0, Colliding.Width(), CollisionCursor);
		const auto Scalar = Colliding[X];
		const CTile Actual = Row.m_Cells.empty() ? CTile{} : static_cast<CTile>(Row.m_Cells[0]);
		EXPECT_EQ(Actual.m_Index, Scalar.m_Index);
		EXPECT_EQ(Actual.m_Flags, Scalar.m_Flags);
	}
	EXPECT_EQ(CollisionCursor.m_TreeLookups, 5u);
}

TEST(EditorRenderSpans, DerivedFlagsCanOnlyAddOpacity)
{
	CEditorTilePlane<CTileValues> Plane(8, 1);
	std::array<unsigned char, 256> aFlags;
	aFlags.fill(255);
	for(int Index = 0; Index < 8; ++Index)
		Plane.Set(Index, CTile{17, static_cast<unsigned char>((Index & 3) | (Index & 4 ? TILEFLAG_ROTATE : 0)), 0, 0});
	CRenderTileSource<CTile> Source(Plane, aFlags.data());
	std::array<CTile, 32> aBuffer;
	const auto Row = Source.ReadRow(0, 0, 8, aBuffer);
	ASSERT_EQ(Row.m_Cells.size(), 8u);
	for(int Index = 0; Index < 8; ++Index)
	{
		const auto Expected = (Index & 3) | (Index & 4 ? TILEFLAG_ROTATE : 0) | TILEFLAG_OPAQUE;
		EXPECT_EQ(Row.m_Cells[Index].m_Flags, Expected);
		EXPECT_EQ(Source[Index].m_Flags, Expected);
	}
}

TEST(EditorRenderSpans, SpecialPlanesPreserveNativeFieldsAcrossChunkRows)
{
	const auto Check = []<typename TValue, typename TNative>(TValue, TNative Native) {
		constexpr int Width = 65, Height = 33;
		CEditorTilePlane<TValue> Plane(Width, Height);
		for(const std::size_t Index : {0u, 31u, 32u, 64u, 32u * Width + 64u})
			Plane.Set(Index, Native);
		CRenderTileSource<TNative> Source(Plane);
		CTileChunkCursor Cursor;
		std::array<TNative, 32> aBuffer;
		for(int Y = 0; Y < Height; ++Y)
			for(std::size_t X = 0; X < Width;)
			{
				const auto Row = Source.ReadRow(X, Y, Width, aBuffer, &Cursor);
				ASSERT_GT(Row.m_Length, 0u);
				for(std::size_t Offset = 0; Offset < Row.m_Length; ++Offset)
				{
					const auto Index = Y * Width + X + Offset;
					const auto Expected = Index == 0 || Index == 31 || Index == 32 || Index == 64 || Index == 32 * Width + 64 ? Native : TNative{};
					const auto Actual = Row.m_Cells.empty() ? TNative{} : Row.m_Cells[Offset];
					EXPECT_EQ(Actual.m_Type, Expected.m_Type);
					if constexpr(requires { Actual.m_Number; })
						EXPECT_EQ(Actual.m_Number, Expected.m_Number);
					if constexpr(requires { Actual.m_Flags; })
					{
						EXPECT_EQ(Actual.m_Flags, Expected.m_Flags);
						EXPECT_EQ(Actual.m_Delay, Expected.m_Delay);
					}
					if constexpr(requires { Actual.m_Force; })
					{
						EXPECT_EQ(Actual.m_Force, Expected.m_Force);
						EXPECT_EQ(Actual.m_MaxSpeed, Expected.m_MaxSpeed);
						EXPECT_EQ(Actual.m_Angle, Expected.m_Angle);
						EXPECT_EQ(Actual.m_MustBe0, 0);
					}
				}
				X += Row.m_Length;
			}
	};
	Check(CTeleTileValues{}, CTeleTile{79, TILE_TELEIN});
	Check(CSpeedupTileValues{}, CSpeedupTile{31, 83, TILE_SPEED_BOOST, 0, -135});
	Check(CSwitchTileValues{}, CSwitchTile{46, TILE_SWITCHTIMEDOPEN, TILEFLAG_ROTATE | TILEFLAG_YFLIP, 255});
	Check(CTuneTileValues{}, CTuneTile{28, TILE_TUNE});
}
