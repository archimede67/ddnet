#include "test.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/editor/history/save_job.h>
#include <game/mapitems_ex.h>

#include <gtest/gtest.h>
#include <zlib.h>

#include <chrono>
#include <cstring>
#include <future>
#include <limits>
#include <memory>
#include <thread>

TEST(Datafile, ExtendedType)
{
	std::unique_ptr<IStorage> pStorage = CreateLocalStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating local storage";

	CTestInfo Info;

	CMapItemTest ItemTest;
	ItemTest.m_Version = 1;
	ItemTest.m_aFields[0] = 1234;
	ItemTest.m_aFields[1] = 5678;
	ItemTest.m_Field3 = 9876;
	ItemTest.m_Field4 = 5432;

	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(pStorage.get(), Info.m_aFilename));

		Writer.AddItem(MAPITEMTYPE_TEST, 0x8000, sizeof(ItemTest), &ItemTest);

		EXPECT_TRUE(Writer.Finish());
	}

	{
		CDataFileReader Reader;
		ASSERT_TRUE(Reader.Open(pStorage.get(), Info.m_aFilename, IStorage::TYPE_ALL));

		int Start, Num;
		Reader.GetType(MAPITEMTYPE_TEST, &Start, &Num);
		EXPECT_EQ(Num, 1);

		int Index = Reader.FindItemIndex(MAPITEMTYPE_TEST, 0x8000);
		EXPECT_EQ(Start, Index);
		ASSERT_GE(Index, 0);
		ASSERT_EQ(Reader.GetItemSize(Index), (int)sizeof(ItemTest));

		int Type, Id;
		const CMapItemTest *pTest = (const CMapItemTest *)Reader.GetItem(Index, &Type, &Id);
		EXPECT_EQ(pTest, Reader.FindItem(MAPITEMTYPE_TEST, 0x8000));
		EXPECT_EQ(Type, MAPITEMTYPE_TEST);
		EXPECT_EQ(Id, 0x8000);

		EXPECT_EQ(pTest->m_Version, ItemTest.m_Version);
		EXPECT_EQ(pTest->m_aFields[0], ItemTest.m_aFields[0]);
		EXPECT_EQ(pTest->m_aFields[1], ItemTest.m_aFields[1]);
		EXPECT_EQ(pTest->m_Field3, ItemTest.m_Field3);
		EXPECT_EQ(pTest->m_Field4, ItemTest.m_Field4);

		Reader.Close();
	}

	if(!HasFailure())
	{
		pStorage->RemoveFile(Info.m_aFilename, IStorage::TYPE_SAVE);
	}
}

TEST(Datafile, StringData)
{
	std::unique_ptr<IStorage> pStorage = CreateLocalStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating local storage";

	CTestInfo Info;

	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(pStorage.get(), Info.m_aFilename));

		EXPECT_EQ(Writer.AddDataString(""), -1); // Empty string is not added
		EXPECT_EQ(Writer.AddDataString("Abc"), 0);
		EXPECT_EQ(Writer.AddDataString("DDNet最好了"), 1);
		EXPECT_EQ(Writer.AddDataString("aβい🐘"), 2);
		EXPECT_EQ(Writer.AddData(3, "Abc"), 3); // Not zero-terminated
		EXPECT_EQ(Writer.AddData(7, "foo\0bar"), 4); // Early zero-terminator
		EXPECT_EQ(Writer.AddData(5, "xyz\xff\0"), 5); // Truncated UTF-8
		EXPECT_EQ(Writer.AddData(4, "XYZ\xff"), 6); // Truncated UTF-8 and not zero-terminated

		EXPECT_TRUE(Writer.Finish());
	}

	{
		CDataFileReader Reader;
		ASSERT_TRUE(Reader.Open(pStorage.get(), Info.m_aFilename, IStorage::TYPE_ALL));

		EXPECT_EQ(Reader.GetDataString(-1000), nullptr);
		EXPECT_STREQ(Reader.GetDataString(-1), "");
		EXPECT_STREQ(Reader.GetDataString(0), "Abc");
		EXPECT_STREQ(Reader.GetDataString(1), "DDNet最好了");
		EXPECT_STREQ(Reader.GetDataString(2), "aβい🐘");
		EXPECT_EQ(Reader.GetDataString(3), nullptr);
		EXPECT_EQ(Reader.GetDataString(4), nullptr);
		EXPECT_EQ(Reader.GetDataString(5), nullptr);
		EXPECT_EQ(Reader.GetDataString(6), nullptr);
		EXPECT_EQ(Reader.GetDataString(1000), nullptr);

		Reader.Close();
	}

	if(!HasFailure())
	{
		pStorage->RemoveFile(Info.m_aFilename, IStorage::TYPE_SAVE);
	}
}

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

	/** Only changes the output open mode, exercising actual low-level I/O failure. */
	class CReadOnlyOutputStorage : public IStorage
	{
		IStorage *m_pStorage;

	public:
		explicit CReadOnlyOutputStorage(IStorage *pStorage) :
			m_pStorage(pStorage) {}
		int NumPaths() const override { return m_pStorage->NumPaths(); }
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { m_pStorage->ListDirectory(Type, pPath, Callback, pUser); }
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_pStorage->ListDirectoryInfo(Type, pPath, Callback, pUser); }
		IOHANDLE OpenFile(const char *pFilename, int Flags, int Type, char *pBuffer, int BufferSize) override { return m_pStorage->OpenFile(pFilename, IOFLAG_READ, Type, pBuffer, BufferSize); }
		bool FileExists(const char *pFilename, int Type) override { return m_pStorage->FileExists(pFilename, Type); }
		bool FolderExists(const char *pFilename, int Type) override { return m_pStorage->FolderExists(pFilename, Type); }
		bool ReadFile(const char *pFilename, int Type, void **ppResult, unsigned *pResultLen) override { return m_pStorage->ReadFile(pFilename, Type, ppResult, pResultLen); }
		char *ReadFileStr(const char *pFilename, int Type) override { return m_pStorage->ReadFileStr(pFilename, Type); }
		bool RetrieveTimes(const char *pFilename, int Type, time_t *pCreated, time_t *pModified) override { return m_pStorage->RetrieveTimes(pFilename, Type, pCreated, pModified); }
		bool CalculateHashes(const char *pFilename, int Type, SHA256_DIGEST *pSha256, unsigned *pCrc) override { return m_pStorage->CalculateHashes(pFilename, Type, pSha256, pCrc); }
		bool FindFile(const char *pFilename, const char *pPath, int Type, char *pBuffer, int BufferSize) override { return m_pStorage->FindFile(pFilename, pPath, Type, pBuffer, BufferSize); }
		size_t FindFiles(const char *pFilename, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_pStorage->FindFiles(pFilename, pPath, Type, pEntries); }
		bool RemoveFile(const char *pFilename, int Type) override { return m_pStorage->RemoveFile(pFilename, Type); }
		bool RemoveFolder(const char *pFilename, int Type) override { return m_pStorage->RemoveFolder(pFilename, Type); }
		bool RenameFile(const char *pOld, const char *pNew, int Type) override { return m_pStorage->RenameFile(pOld, pNew, Type); }
		bool CreateFolder(const char *pFolder, int Type) override { return m_pStorage->CreateFolder(pFolder, Type); }
		void GetCompletePath(int Type, const char *pDir, char *pBuffer, unsigned Size) override { m_pStorage->GetCompletePath(Type, pDir, pBuffer, Size); }
		bool RemoveBinaryFile(const char *pFilename) override { return m_pStorage->RemoveBinaryFile(pFilename); }
		bool RenameBinaryFile(const char *pOld, const char *pNew) override { return m_pStorage->RenameBinaryFile(pOld, pNew); }
		const char *GetBinaryPath(const char *pFilename, char *pBuffer, unsigned Size) override { return m_pStorage->GetBinaryPath(pFilename, pBuffer, Size); }
		const char *GetBinaryPathAbsolute(const char *pFilename, char *pBuffer, unsigned Size) override { return m_pStorage->GetBinaryPathAbsolute(pFilename, pBuffer, Size); }
	};
}

TEST(Datafile, FinishReportsAnActualWriteFailure)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto File = pStorage->OpenFile("readonly.map", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File, "original", 8), 8u);
	ASSERT_EQ(io_close(File), 0);
	CReadOnlyOutputStorage ReadOnly(pStorage.get());
	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(&ReadOnly, "readonly.map"));
		Writer.AddDataString("attempted replacement");
		EXPECT_FALSE(Writer.Finish());
	}
	char *pContents = pStorage->ReadFileStr("readonly.map", IStorage::TYPE_SAVE);
	ASSERT_NE(pContents, nullptr);
	EXPECT_STREQ(pContents, "original");
	free(pContents);
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, FailedWritePreservesDestinationAndSavedContentMarker)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	for(const char *pName : {"temporary.map", "saved.map"})
	{
		const auto File = pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		ASSERT_EQ(io_write(File, "original", 8), 8u);
		ASSERT_EQ(io_close(File), 0);
	}
	CReadOnlyOutputStorage ReadOnly(pStorage.get());
	CEditorSaveState Saves;
	ASSERT_TRUE(Saves.InitializeLoaded("baseline"));
	auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(MakeSaveDocument("changed")), "changed", "saved.map", editor_history::ESaveKind::MANUAL);
	ASSERT_TRUE(Ticket);
	auto pJob = std::make_shared<CDataFileWriterFinishJob>(&ReadOnly, "temporary.map", *Ticket);
	editor_history::CSaveQueue Queue;
	pJob->SetWriteTurn(Queue.Register("saved.map"));
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
	EXPECT_NE(pJob->ErrorMessage()[0], '\0');
	EXPECT_FALSE(Saves.Complete(pJob->Ticket(), pJob->ErrorMessage()[0] == '\0'));
	EXPECT_FALSE(Saves.Dirty("baseline", editor_history::ESaveKind::MANUAL));
	EXPECT_TRUE(Saves.Dirty("changed", editor_history::ESaveKind::MANUAL));
	char *pContents = pStorage->ReadFileStr("saved.map", IStorage::TYPE_SAVE);
	ASSERT_NE(pContents, nullptr);
	EXPECT_STREQ(pContents, "original");
	free(pContents);
	EXPECT_FALSE(pStorage->FileExists("temporary.map", IStorage::TYPE_SAVE));
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, FailedFinalReplacementDoesNotEstablishASaveMarker)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("blocked.map", IStorage::TYPE_SAVE));
	const auto File = pStorage->OpenFile("blocked.map/keep", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File, "original", 8), 8u);
	ASSERT_EQ(io_close(File), 0);
	CEditorSaveState Saves;
	auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(MakeSaveDocument("changed")), "changed", "blocked.map", editor_history::ESaveKind::MANUAL);
	ASSERT_TRUE(Ticket);
	auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), "temporary.map", *Ticket);
	editor_history::CSaveQueue Queue;
	pJob->SetWriteTurn(Queue.Register("blocked.map"));
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
	EXPECT_NE(pJob->ErrorMessage()[0], '\0');
	EXPECT_FALSE(Saves.Complete(pJob->Ticket(), pJob->ErrorMessage()[0] == '\0'));
	EXPECT_TRUE(Saves.Dirty("changed", editor_history::ESaveKind::MANUAL));
	char *pContents = pStorage->ReadFileStr("blocked.map/keep", IStorage::TYPE_SAVE);
	ASSERT_NE(pContents, nullptr);
	EXPECT_STREQ(pContents, "original");
	free(pContents);
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, SameDestinationWritesRemainOrderedAndTicketsPinExactContents)
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
		auto Ticket = Saves.Capture(std::make_shared<const CEditorDocumentValues>(std::move(Root)), pValue, "saved.map", editor_history::ESaveKind::MANUAL);
		ASSERT_TRUE(Ticket);
		auto pJob = std::make_shared<CDataFileWriterFinishJob>(pStorage.get(), pValue, *Ticket);
		pJob->SetWriteTurn(Queue.Register("saved.map"));
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

TEST(Datafile, EagerCompressionMatchesDeferredAndCompress2)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::vector<unsigned char> vData(300000);
	unsigned State = 17;
	for(auto &DataByte : vData)
	{
		State = State * 1664525u + 1013904223u;
		DataByte = State >> 24;
	}
	for(bool Eager : {false, true})
	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(pStorage.get(), Eager ? "eager" : "deferred"));
		if(Eager)
			Writer.SetEagerCompression();
		EXPECT_EQ(Writer.AddData(vData.size(), vData.data()), 0);
		EXPECT_EQ(Writer.AddDataString("highly compressible second block"), 1);
		ASSERT_TRUE(Writer.Finish());
	}
	void *pDeferred, *pEager;
	unsigned DeferredSize, EagerSize;
	ASSERT_TRUE(pStorage->ReadFile("deferred", IStorage::TYPE_SAVE, &pDeferred, &DeferredSize));
	ASSERT_TRUE(pStorage->ReadFile("eager", IStorage::TYPE_SAVE, &pEager, &EagerSize));
	ASSERT_EQ(DeferredSize, EagerSize);
	EXPECT_EQ(memcmp(pDeferred, pEager, EagerSize), 0);
	// Independent zlib oracle: 36-byte header plus two offset/size pairs.
	uLongf CompressedSize = compressBound(vData.size());
	std::vector<unsigned char> vCompressed(CompressedSize);
	ASSERT_EQ(compress2(vCompressed.data(), &CompressedSize, vData.data(), vData.size(), Z_DEFAULT_COMPRESSION), Z_OK);
	ASSERT_GE(EagerSize, 52u + CompressedSize);
	EXPECT_EQ(memcmp(static_cast<unsigned char *>(pEager) + 52, vCompressed.data(), CompressedSize), 0);
	free(pDeferred);
	free(pEager);
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(Datafile, EagerFailureIsStickyAndDoesNotReadOversizedInput)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "failed"));
	Writer.SetEagerCompression();
	const char DataByte = 0;
	EXPECT_EQ(Writer.AddData(static_cast<size_t>(std::numeric_limits<int>::max()) + 1, &DataByte), -1);
	EXPECT_TRUE(Writer.Failed());
	EXPECT_EQ(Writer.AddData(1, &DataByte), -1);
	EXPECT_FALSE(Writer.Finish());
	Info.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(EditorSaveJob, QueuedExportsWaitBeforeOpeningAndFailuresReleaseTheirTurn)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	editor_history::CSaveQueue Queue;
	const auto pBlocker = Queue.Register("all exports");
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
			pJob->SetWriteTurn(Queue.Register("all exports"));
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
		const auto pBlocker = Queue.Register("exports");
		pJob->SetWriteTurn(Queue.Register("exports"));
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
			pJob->SetWriteTurn(Queue.Register("exports"));
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
