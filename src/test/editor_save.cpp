#include "test.h"

#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/editor/history/save_job.h>
#include <game/editor/mapitems/document_export.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

namespace
{
	CEditorDocumentValues MakeSaveDocument(const char *pAuthor)
	{
		CEditorDocumentValues Root;
		str_copy(Root.m_MapInfo.m_Author.Buffer(), pAuthor, 32);
		CGroupDocumentValues Group;
		Group.m_Id = 1;
		Group.m_GameGroup = true;
		CLayerDocumentValues Layer;
		Layer.m_Id = 2;
		Layer.m_Type = LAYERTYPE_TILES;
		CTileLayerDocumentValues Tiles;
		Tiles.m_HasGame = true;
		Tiles.m_Tiles.Resize(2, 2);
		Layer.m_Data = std::move(Tiles);
		Group.m_Layers.Update([&](auto &Layers) { Layers.emplace_back(std::move(Layer)); });
		Root.m_Groups.Update([&](auto &Groups) { Groups.emplace_back(std::move(Group)); });
		return Root;
	}

	void CheckCapturedSave(bool BlockDestination)
	{
		CTestInfo Info;
		Info.m_DeleteTestStorageFilesOnSuccess = true;
		auto pStorage = Info.CreateTestStorage();
		ASSERT_NE(pStorage, nullptr);
		if(BlockDestination)
		{
			ASSERT_TRUE(pStorage->CreateFolder("saved.map", IStorage::TYPE_SAVE));
			IOHANDLE File = pStorage->OpenFile("saved.map/keep", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_close(File), 0);
		}

		CEditorDocumentValues Document;
		CGroupDocumentValues Group;
		Group.m_Id = 1;
		Group.m_GameGroup = true;
		CLayerDocumentValues Layer;
		Layer.m_Id = 2;
		Layer.m_Type = LAYERTYPE_TILES;
		auto &Tiles = std::get<CTileLayerDocumentValues>(Layer.m_Data);
		Tiles.m_HasGame = true;
		ASSERT_TRUE(Tiles.m_Tiles.Resize(1, 1));
		Group.m_Layers.Update([&](auto &vLayers) { vLayers.emplace_back(Layer); });
		Document.m_Groups.Update([&](auto &vGroups) { vGroups.emplace_back(Group); });
		CEditorSaveState Saves;
		const auto LoadedKey = EditorDocumentPersistedKey(Document);
		ASSERT_TRUE(Saves.InitializeLoaded(LoadedKey));
		str_copy(Document.m_MapInfo.m_Author.Buffer(), "captured author");
		const auto CapturedKey = EditorDocumentPersistedKey(Document);
		auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(Document), CapturedKey, "saved.map", editor_history::ESaveKind::MANUAL);
		ASSERT_TRUE(Ticket);
		auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), "queued.tmp", std::move(*Ticket));
		editor_history::CSaveQueue Queue;
		pJob->SetWriteTurn(Queue.Register());
		EXPECT_FALSE(pStorage->FileExists("queued.tmp", IStorage::TYPE_SAVE));
		EXPECT_EQ(pJob->Ticket().m_pRoot, nullptr);
		str_copy(Document.m_MapInfo.m_Author.Buffer(), "later author");
		EXPECT_TRUE(Saves.Dirty(CapturedKey, editor_history::ESaveKind::MANUAL));

		CJobPool Pool;
		Pool.Init(1);
		Pool.Add(pJob);
		Pool.Shutdown();
		ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
		const bool Success = pJob->ErrorMessage()[0] == '\0';
		EXPECT_EQ(Success, !BlockDestination) << pJob->ErrorMessage();
		EXPECT_EQ(Saves.Complete(pJob->Ticket(), Success), !BlockDestination);
		EXPECT_EQ(Saves.Dirty(LoadedKey, editor_history::ESaveKind::MANUAL), !BlockDestination);
		EXPECT_EQ(Saves.Dirty(CapturedKey, editor_history::ESaveKind::MANUAL), BlockDestination);
		EXPECT_TRUE(Saves.Dirty(EditorDocumentPersistedKey(Document), editor_history::ESaveKind::MANUAL));
		EXPECT_FALSE(pStorage->FileExists("queued.tmp", IStorage::TYPE_SAVE));
		if(BlockDestination)
		{
			EXPECT_TRUE(pStorage->FileExists("saved.map/keep", IStorage::TYPE_SAVE));
			return;
		}
		CDataFileReader Reader;
		ASSERT_TRUE(Reader.Open(pStorage.get(), "saved.map", IStorage::TYPE_SAVE));
		const auto *pInfo = static_cast<const CMapItemInfo *>(Reader.FindItem(MAPITEMTYPE_INFO, 0));
		ASSERT_NE(pInfo, nullptr);
		EXPECT_STREQ(Reader.GetDataString(pInfo->m_Author), "captured author");
	}
}

TEST(EditorSaveJob, CapturedRootSavesCapturedContentsAndCompletesMarker)
{
	CheckCapturedSave(false);
}

TEST(EditorSaveJob, FailedRenamePreservesDestinationAndSavedMarker)
{
	CheckCapturedSave(true);
}

TEST(EditorSaveJob, AliasedDestinationsRemainOrderedAndTicketsPinExactContents)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CEditorSaveState Saves;
	editor_history::CSaveQueue Queue;
	std::vector<std::shared_ptr<CDataFileWriterFinishJob>> vpJobs;
	for(const char *pValue : {"first", "second"})
	{
		auto Root = MakeSaveDocument(pValue);
		auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(std::move(Root)), pValue, vpJobs.empty() ? "saved.map" : "./saved.map", editor_history::ESaveKind::MANUAL);
		ASSERT_TRUE(Ticket);
		auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), pValue, *Ticket);
		pJob->SetWriteTurn(Queue.Register());
		vpJobs.push_back(std::move(pJob));
	}
	CJobPool Pool;
	Pool.Init(2);
	for(const auto &pJob : vpJobs)
		Pool.Add(pJob);
	Pool.Shutdown();
	for(const auto &pJob : vpJobs)
	{
		ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
		EXPECT_EQ(pJob->ErrorMessage()[0], '\0');
		EXPECT_TRUE(Saves.Complete(pJob->Ticket(), pJob->ErrorMessage()[0] == '\0'));
	}
	EXPECT_FALSE(Saves.Dirty("second", editor_history::ESaveKind::MANUAL));
	EXPECT_TRUE(Saves.Dirty("third edit", editor_history::ESaveKind::MANUAL));
	EXPECT_TRUE(Saves.Dirty("second", editor_history::ESaveKind::AUTOMATIC));
	EXPECT_FALSE(Saves.Complete(vpJobs.front()->Ticket(), true));
	CEditorSaveState Reopened;
	EXPECT_FALSE(Reopened.Complete(vpJobs.back()->Ticket(), true));
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "saved.map", IStorage::TYPE_SAVE));
	EXPECT_STREQ(static_cast<const char *>(Reader.GetData(0)), "second");
	Reader.Close();
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, QueuedExportsWaitBeforeOpeningAndFailuresReleaseTheirTurn)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	editor_history::CSaveQueue Queue;
	const auto pBlocker = Queue.Register();
	std::vector<std::shared_ptr<CDataFileWriterFinishJob>> vpJobs;
	CJobPool Pool;
	Pool.Init(2);
	{
		CEditorSaveState Saves;
		for(int Index = 0; Index < 4; ++Index)
		{
			auto Root = MakeSaveDocument("captured before close");
			if(Index == 1)
				Root = {}; // Invalid document graph.
			if(Index == 2)
				Root.m_Groups.Update([](auto &Groups) {
					Groups[0].Update([](auto &Group) {
						Group.m_Layers.Update([](auto &Layers) {
							Layers[0].Update([](auto &Layer) { std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Resize(100000, 100000); });
						});
					});
				});
			const std::string Temporary = Index == 0 ? "missing-directory/temp.map" : "queued-" + std::to_string(Index) + ".map";
			auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(std::move(Root)), "revision", "result.map", editor_history::ESaveKind::MANUAL);
			ASSERT_TRUE(Ticket);
			auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), Temporary.c_str(), *Ticket);
			pJob->SetWriteTurn(Queue.Register());
			vpJobs.push_back(pJob);
			Pool.Add(pJob);
			EXPECT_FALSE(pStorage->FileExists(Temporary.c_str(), IStorage::TYPE_SAVE));
		}
	} // The owning document/save state closes before export starts.
	pBlocker->Complete();
	Pool.Shutdown();
	for(int Index = 0; Index < 4; ++Index)
	{
		ASSERT_EQ(vpJobs[Index]->State(), IJob::STATE_DONE);
		EXPECT_EQ(vpJobs[Index]->ErrorMessage()[0] == '\0', Index == 3);
	}
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "result.map", IStorage::TYPE_SAVE));
	EXPECT_STREQ(Reader.GetDataString(0), "captured before close");
	Reader.Close();
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, AutosavePrunesCapturedPrefixAndLimitOnlyAfterSuccessfulReplacement)
{
	for(bool FailReplacement : {false, true})
	{
		SCOPED_TRACE(FailReplacement);
		CTestInfo Info;
		const auto pStorage = Info.CreateTestStorage();
		ASSERT_NE(pStorage, nullptr);
		ASSERT_TRUE(pStorage->CreateFolder("maps", IStorage::TYPE_SAVE));
		ASSERT_TRUE(pStorage->CreateFolder("maps/auto", IStorage::TYPE_SAVE));
		const char *apOldNames[] = {
			"maps/auto/captured_2024-01-01_00-00-00.map",
			"maps/auto/captured_2024-01-01_00-00-01.map",
			"maps/auto/captured_2024-01-01_00-00-02.map",
			"maps/auto/other_2024-01-01_00-00-00.map",
			"maps/auto/other_2024-01-01_00-00-01.map"};
		for(const char *pName : apOldNames)
		{
			const auto File = pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_write(File, "retained", 8), 8u);
			ASSERT_EQ(io_close(File), 0);
		}
		const char *pDestination = "maps/auto/captured_2024-01-01_00-00-03.map";
		if(FailReplacement)
		{
			ASSERT_TRUE(pStorage->CreateFolder(pDestination, IStorage::TYPE_SAVE));
			const auto File = pStorage->OpenFile("maps/auto/captured_2024-01-01_00-00-03.map/keep", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_close(File), 0);
		}
		CEditorSaveState Saves;
		auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(MakeSaveDocument("autosave")), "captured", pDestination, editor_history::ESaveKind::AUTOMATIC);
		ASSERT_TRUE(Ticket);
		auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), "maps/auto/temporary.map", *Ticket);
		std::string AutosaveName = "captured";
		int Maximum = 2;
		pJob->SetAutosaveName(AutosaveName, Maximum);
		// Later editor preferences or document names cannot change queued cleanup.
		AutosaveName = "other";
		Maximum = 1;
		editor_history::CSaveQueue Queue;
		const auto pBlocker = Queue.Register();
		pJob->SetWriteTurn(Queue.Register());
		CJobPool Pool;
		Pool.Init(1);
		Pool.Add(pJob);
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while(pJob->State() == IJob::STATE_QUEUED && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::yield();
		EXPECT_EQ(pJob->State(), IJob::STATE_RUNNING);
		for(const char *pName : apOldNames)
			EXPECT_TRUE(pStorage->FileExists(pName, IStorage::TYPE_SAVE));
		EXPECT_FALSE(pStorage->FileExists("maps/auto/temporary.map", IStorage::TYPE_SAVE));
		pBlocker->Complete();
		Pool.Shutdown();
		ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
		EXPECT_EQ(pJob->ErrorMessage()[0] != '\0', FailReplacement);
		for(int Index = 0; Index < 5; ++Index)
			EXPECT_EQ(pStorage->FileExists(apOldNames[Index], IStorage::TYPE_SAVE), FailReplacement || Index >= 2);
		EXPECT_FALSE(pStorage->FileExists("maps/auto/temporary.map", IStorage::TYPE_SAVE));
		if(!FailReplacement)
		{
			CDataFileReader Reader;
			ASSERT_TRUE(Reader.Open(pStorage.get(), pDestination, IStorage::TYPE_SAVE));
			EXPECT_STREQ(Reader.GetDataString(0), "autosave");
		}
		Info.m_DeleteTestStorageFilesOnSuccess = true;
	}
}

TEST(EditorSaveJob, ExportRootsRetireOnWorkersBeforeForegroundCollection)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	for(bool Fail : {false, true})
	{
		for(bool Observe : {false, true})
		{
			std::thread::id DestroyThread;
			std::atomic<bool> Destroyed = false;
			auto pRoot = std::shared_ptr<const CEditorDocumentValues>(new CEditorDocumentValues(MakeSaveDocument("last owner")), [&](const auto *pValue) {
				DestroyThread = std::this_thread::get_id();
				delete pValue;
				Destroyed = true;
			});
			CEditorSaveState Saves;
			auto Ticket = Saves.Capture(std::move(pRoot), "last owner", "retired.map", editor_history::ESaveKind::MANUAL);
			ASSERT_TRUE(Ticket);
			auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), Fail ? "missing/retired.tmp" : "retired.tmp", std::move(*Ticket));
			editor_history::CSaveQueue Queue;
			pJob->SetWriteTurn(Queue.Register());
			EXPECT_EQ(pJob->Ticket().m_pRoot, nullptr);
			std::promise<void> Acquired, Release;
			auto Released = Release.get_future();
			std::thread Observer;
			if(Observe)
			{
				Observer = std::thread([&] {
					auto pObserved = pJob->ExportRoot();
					Acquired.set_value();
					Released.wait();
					editor_history::CStorageUsage Usage;
					pObserved->Account(Usage);
					EXPECT_GT(Usage.Bytes(), 0u);
				});
				Acquired.get_future().wait();
			}
			CJobPool Pool;
			Pool.Init(1);
			Pool.Add(pJob);
			Pool.Shutdown(); // non-abortable exports also retire during shutdown
			EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
			EXPECT_EQ(pJob->ExportRoot(), nullptr);
			EXPECT_EQ(pJob->ErrorMessage()[0] != '\0', Fail);
			EXPECT_EQ(Destroyed.load(), !Observe);
			if(Observe)
			{
				Release.set_value();
				Observer.join();
			}
			EXPECT_TRUE(Destroyed.load());
			EXPECT_NE(DestroyThread, std::this_thread::get_id());
			EXPECT_EQ(Saves.Complete(pJob->Ticket(), !Fail), !Fail);
			// The foreground still owns the completed job and its marker metadata.
		}
	}
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, FailedProjectionDoesNotAppendLaterLayers)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	auto Document = MakeSaveDocument("before failure");
	Document.m_Groups.Update([](auto &Groups) {
		Groups[0].Update([](auto &Group) {
			Group.m_Layers.Update([](auto &Layers) {
				Layers[0].Update([](auto &Layer) {
					ASSERT_TRUE(std::get<CTileLayerDocumentValues>(Layer.m_Data).m_Tiles.Resize(100000, 100000));
				});
				CLayerDocumentValues LaterLayer;
				LaterLayer.m_Id = 3;
				LaterLayer.m_Type = LAYERTYPE_QUADS;
				LaterLayer.m_Data = CLayerQuadsValues{};
				Layers.emplace_back(std::move(LaterLayer));
			});
		});
	});
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "partial.map"));
	Writer.SetEagerCompression();
	std::string Error;
	EXPECT_FALSE(WriteEditorDocument(Document, Writer, Error));
	EXPECT_FALSE(Error.empty());
	// Inspect the already accepted prefix; production aborts this failed export.
	ASSERT_TRUE(Writer.Finish());
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "partial.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(Reader.NumData(), 1);
	EXPECT_STREQ(Reader.GetDataString(0), "before failure");
	EXPECT_EQ(Reader.NumItems(), 2);
	EXPECT_EQ(Reader.FindItem(MAPITEMTYPE_LAYER, 0), nullptr);
	EXPECT_EQ(Reader.FindItem(MAPITEMTYPE_LAYER, 1), nullptr);
	Reader.Close();
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}
