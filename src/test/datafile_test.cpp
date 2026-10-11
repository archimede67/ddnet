#include "test.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/mapitems_ex.h>

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>
#include <limits>
#include <memory>

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
		EXPECT_TRUE(Writer.Failed());
	}
	char *pContents = pStorage->ReadFileStr("readonly.map", IStorage::TYPE_SAVE);
	ASSERT_NE(pContents, nullptr);
	EXPECT_STREQ(pContents, "original");
	free(pContents);
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
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "eager", IStorage::TYPE_SAVE));
	ASSERT_EQ(Reader.NumData(), 2);
	ASSERT_EQ(Reader.GetDataSize(0), vData.size());
	EXPECT_EQ(memcmp(Reader.GetData(0), vData.data(), vData.size()), 0);
	EXPECT_STREQ(Reader.GetDataString(1), "highly compressible second block");
	Reader.Close();
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
