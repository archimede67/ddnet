// Actual editor adapter regressions. This factory is linked only into the
// headless editor-testrunner; the production client has its ordinary factory.
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/client/graphics_threaded.h>
#include <engine/config.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <engine/textrender.h>

// The prepared Windows benchmark injects windows.h before this test source.
#ifdef IMAGE_CURSOR
#undef IMAGE_CURSOR
#endif
#include <generated/client_data.h>

#include <game/editor/editor.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>

#include <SDL.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <future>
#include <random>
#include <thread>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
	class CReleaseProbe final : public CEditorComponent
	{
	public:
		bool m_Held = false;
		bool OnInput(const IInput::CEvent &Event) override
		{
			if(Event.m_Key != KEY_F8)
				return false;
			if(Event.m_Flags & IInput::FLAG_RELEASE)
				m_Held = false;
			else if(Event.m_Flags & IInput::FLAG_PRESS)
				m_Held = true;
			return true;
		}
	} gs_ReleaseProbe;
	CEditor *gs_pEditor;
	IKernel *gs_pKernel;
	std::optional<CEditorDocumentValues> gs_Initial;

	// Only this editor caches the proxy; all other engine users keep the real
	// engine. Holding submissions gives tests an immutable, not-yet-run capture.
	class CControlledEngine final : public IEngine
	{
	public:
		IEngine *m_pReal = nullptr;
		bool m_Hold = false;
		bool m_HoldSavesOnly = false;
		bool m_Fail = false;
		std::size_t m_Submissions = 0;
		std::vector<std::shared_ptr<IJob>> m_vHeld;
		void Init() override { m_pReal->Init(); }
		void ShutdownJobs() override { m_pReal->ShutdownJobs(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&pLogger) override { m_pReal->SetAdditionalLogger(std::move(pLogger)); }
		void AddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Submissions;
#if defined(__cpp_exceptions)
			if(m_Fail)
				throw std::bad_alloc();
#endif
			if(m_Hold && (!m_HoldSavesOnly || dynamic_cast<CDataFileWriterFinishJob *>(pJob.get())))
				m_vHeld.push_back(std::move(pJob));
			else
				m_pReal->AddJob(std::move(pJob));
		}
		void Release()
		{
			m_Hold = false;
			m_HoldSavesOnly = false;
			m_Fail = false;
			for(auto &pJob : m_vHeld)
				m_pReal->AddJob(std::move(pJob));
			m_vHeld.clear();
		}
	} gs_ControlledEngine;

	bool WaitForJob(const std::shared_ptr<IJob> &pJob)
	{
		const auto Deadline = time_get_nanoseconds() + std::chrono::seconds(15);
		while(!pJob->Done() && time_get_nanoseconds() < Deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return pJob->Done();
	}

	class CBlockingOutputStorage : public IStorage
	{
		IStorage *m_pStorage;

		std::promise<void> m_Release;
		std::shared_future<void> m_Released = m_Release.get_future();
		bool m_DidRelease = false;

	public:
		std::atomic<int> m_Opens = 0;
		std::promise<void> m_Started;
		bool m_Block = false;
		void Release()
		{
			if(!m_DidRelease)
			{
				m_DidRelease = true;
				m_Release.set_value();
			}
		}
		explicit CBlockingOutputStorage(IStorage *pStorage) :
			m_pStorage(pStorage) {}
		int NumPaths() const override { return m_pStorage->NumPaths(); }
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { m_pStorage->ListDirectory(Type, pPath, Callback, pUser); }
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_pStorage->ListDirectoryInfo(Type, pPath, Callback, pUser); }
		IOHANDLE OpenFile(const char *pFilename, int Flags, int Type, char *pBuffer, int BufferSize) override
		{
			++m_Opens;
			m_Started.set_value();
			if(m_Block)
				m_Released.wait();
			return m_pStorage->OpenFile(pFilename, Flags, Type, pBuffer, BufferSize);
		}
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

	class CEditorHistoryRuntime : public ::testing::Test
	{
	protected:
		CEditor &Editor() { return *gs_pEditor; }
		CEditorMap &Map() { return *Editor().Map(); }
		CEditorDocumentHistory &Timeline() { return Map().m_DocumentHistory; }

		void SetUp() override
		{
			Editor().Reset();
			Editor().SelectMap(0);
			if(Timeline().Pending())
			{
				ASSERT_TRUE(Timeline().PublishAtSafePoint());
			}
			ASSERT_TRUE(Map().RestoreDocumentAtSafePoint(*gs_Initial, [](const char *pError) { ADD_FAILURE() << pError; }));
			ASSERT_TRUE(Timeline().Initialize());
			Editor().m_Dialog = DIALOG_NONE;
			Editor().m_Mode = MODE_LAYERS;
			Editor().m_ShowPicker = false;
			Editor().m_pBrush->Clear();
			Editor().Ui()->ClearObjectReferences();
			Editor().Ui()->ClearHotkeys();
			Editor().Input()->Clear();
			g_Config.m_ClEditorMaxHistory = 100;
			g_Config.m_ClEditorHistoryMemory = 256;
			Timeline().ApplyPreferences();
		}
		void TearDown() override
		{
			gs_ControlledEngine.Release();
			DrainSaves();
			Modifier(false);
			Editor().Reset();
			if(Timeline().Pending())
			{
				EXPECT_TRUE(Timeline().PublishAtSafePoint());
			}
			Editor().m_pBrush->Clear();
			Editor().Ui()->ClearObjectReferences();
			Editor().Ui()->ClearHotkeys();
			Editor().Input()->Clear();
		}
		CEditorDocumentValues Snapshot()
		{
			std::string Error;
			auto Root = Map().CaptureDocument(nullptr, Error);
			EXPECT_TRUE(Root.has_value()) << Error;
			return Root ? std::move(*Root) : CEditorDocumentValues{};
		}
		void AssertCurrent() { EXPECT_TRUE(Snapshot() == Timeline().History()->Current()); }
		void SelectGame()
		{
			for(size_t G = 0; G < Map().m_vpGroups.size(); ++G)
				for(size_t L = 0; L < Map().m_vpGroups[G]->m_vpLayers.size(); ++L)
					if(Map().m_vpGroups[G]->m_vpLayers[L] == Map().m_pGameLayer)
						Map().SelectLayer(L, G);
		}
		void MouseButton(int Button, bool Down)
		{
			SDL_Event Event{};
			Event.type = Down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			Event.button.button = Button;
			Event.button.state = Down ? SDL_PRESSED : SDL_RELEASED;
			ASSERT_EQ(SDL_PushEvent(&Event), 1);
			static_cast<IEngineInput *>(Editor().Input())->Update();
			Editor().Input()->ConsumeEvents([&](const IInput::CEvent &InputEvent) { Editor().OnInput(InputEvent); });
		}
		void Key(int Code, int Flags = IInput::FLAG_PRESS)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = Flags;
			Editor().OnInput(Event);
		}
		void PhysicalKey(SDL_Scancode Scan, SDL_Keycode Code, bool Down, bool Repeat = false)
		{
			SDL_Event Event{};
			Event.type = Down ? SDL_KEYDOWN : SDL_KEYUP;
			Event.key.keysym.scancode = Scan;
			Event.key.keysym.sym = Code;
			Event.key.state = Down ? SDL_PRESSED : SDL_RELEASED;
			Event.key.repeat = Repeat;
			ASSERT_EQ(SDL_PushEvent(&Event), 1);
			static_cast<IEngineInput *>(Editor().Input())->Update();
			Editor().Input()->ConsumeEvents([&](const IInput::CEvent &InputEvent) { Editor().OnInput(InputEvent); });
		}
		void Type(const char *pText)
		{
			auto *pInput = CLineInput::GetActiveInput();
			ASSERT_NE(pInput, nullptr);
			pInput->SelectAll();
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			Editor().OnInput(Event);
		}
		void Modifier(bool Down)
		{
			SDL_Event Event{};
			Event.type = Down ? SDL_KEYDOWN : SDL_KEYUP;
			Event.key.keysym.scancode = SDL_SCANCODE_LCTRL;
			Event.key.keysym.sym = SDLK_LCTRL;
			Event.key.state = Down ? SDL_PRESSED : SDL_RELEASED;
			ASSERT_EQ(SDL_PushEvent(&Event), 1);
			static_cast<IEngineInput *>(Editor().Input())->Update();
			Editor().Input()->ConsumeEvents([&](const IInput::CEvent &InputEvent) { Editor().OnInput(InputEvent); });
		}
		void Move(float X, float Y)
		{
			const vec2 Target = vec2(X, Y) * vec2(Editor().Graphics()->WindowWidth(), Editor().Graphics()->WindowHeight()) / vec2(Editor().Ui()->Screen()->w, Editor().Ui()->Screen()->h);
			const vec2 Delta = Target - Editor().Ui()->UpdatedMousePos();
			Editor().Ui()->OnCursorMove(Delta.x, Delta.y);
		}
		template<typename F>
		void Frame(F &&Body)
		{
			Timeline().StartFrame();
			Editor().Ui()->StartCheck();
			Editor().Ui()->Update();
			Body();
			Editor().Ui()->FinishCheck();
			Timeline().FinishFrame();
			Editor().Ui()->ClearHotkeys();
			Editor().Input()->Clear();
		}
		template<typename F>
		void Click(float X, float Y, F &&Body, int Button = SDL_BUTTON_LEFT)
		{
			Move(X, Y);
			Frame(Body);
			MouseButton(Button, true);
			Frame(Body);
			MouseButton(Button, false);
			Frame(Body);
		}
		CEditorDocumentHistory::SMemoryUsage WaitForMemory()
		{
			const auto Deadline = time_get_nanoseconds() + std::chrono::seconds(15);
			auto Result = Timeline().MemoryUsage();
			while(!Result.m_Current && time_get_nanoseconds() < Deadline)
			{
				Timeline().StartFrame();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				Result = Timeline().MemoryUsage();
			}
			EXPECT_TRUE(Result.m_Current);
			return Result;
		}
		void AssertMemoryMatchesFresh()
		{
			const auto Result = WaitForMemory();
			editor_history::CStorageUsage Live, Caches, Draft, Saves;
			Map().AccountLiveStorage(Live);
			Map().AccountRuntimeCaches(Caches);
			EXPECT_EQ(Result.m_LiveBytes, Live.Bytes());
			EXPECT_EQ(Result.m_CacheBytes, Caches.Bytes());
			std::size_t DraftBytes = 0;
			if(Timeline().Active())
			{
				Timeline().History()->AccountRetainedPayload(Draft);
				const auto Retained = Draft.Bytes();
				Map().AccountLiveStorage(Draft);
				Timeline().History()->AccountDraft(Draft);
				DraftBytes = Draft.Bytes() - Retained;
			}
			EXPECT_EQ(Result.m_DraftExtraBytes, DraftBytes);
			for(const auto &pSave : Editor().m_WriterFinishJobs)
				if(Map().OwnsSave(pSave->Ticket()))
				{
					const auto pRoot = pSave->ExportRoot();
					if(pRoot && Saves.Add(pRoot.get(), sizeof(CEditorDocumentValues)))
						pRoot->Account(Saves);
				}
			EXPECT_EQ(Result.m_SavePinnedBytes, Saves.Bytes());
		}
		void DrainSaves()
		{
			const auto Deadline = time_get_nanoseconds() + std::chrono::seconds(15);
			while(!Editor().m_WriterFinishJobs.empty() && time_get_nanoseconds() < Deadline)
			{
				Editor().HandleWriterFinishJobs();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			ASSERT_TRUE(Editor().m_WriterFinishJobs.empty());
		}
	};

	// The randomized oracle owns ordinary vectors and scalar values. It never
	// captures the live result to advance its model, nor shares mutable records
	// with history. The loaded fixture is an unchanged, complete baseline.
	struct CPlainLayer
	{
		CLayerValues m_Values;
		std::variant<CLayerQuadsValues, CLayerSoundsValues> m_Data;
	};
	struct CPlainGroup
	{
		CLayerGroupValues m_Values;
		std::vector<CPlainLayer> m_vLayers;
	};
	struct CPlainGraph
	{
		CMapValues m_Values;
		std::vector<CPlainGroup> m_vGroups;
		std::vector<CEnvelopeValues> m_vEnvelopes;

		CEditorDocumentValues Expected(const CEditorDocumentValues &Baseline) const
		{
			auto Result = Baseline;
			static_cast<CMapValues &>(Result) = m_Values;
			Result.m_Groups.Update([&](auto &Groups) {
				for(const auto &PlainGroup : m_vGroups)
				{
					CGroupDocumentValues Group;
					static_cast<CLayerGroupValues &>(Group) = PlainGroup.m_Values;
					Group.m_Layers.Update([&](auto &Layers) {
						for(const auto &PlainLayer : PlainGroup.m_vLayers)
						{
							CLayerDocumentValues Layer;
							static_cast<CLayerValues &>(Layer) = PlainLayer.m_Values;
							std::visit([&](const auto &Data) { Layer.m_Data = Data; }, PlainLayer.m_Data);
							Layers.emplace_back(std::move(Layer));
						}
					});
					Groups.emplace_back(std::move(Group));
				}
			});
			Result.m_Envelopes.Update([&](auto &Envelopes) {
				for(const auto &Envelope : m_vEnvelopes)
					Envelopes.emplace_back(Envelope);
			});
			return Result;
		}
	};

	TEST_F(CEditorHistoryRuntime, RandomizedGraphMatchesIndependentDeepValueModel)
	{
		const auto Baseline = Snapshot();
		const size_t BaseGroups = Map().m_vpGroups.size();
		const size_t BaseEnvelopes = Map().m_vpEnvelopes.size();
		g_Config.m_ClEditorMaxHistory = 500;
		g_Config.m_ClEditorHistoryMemory = 1024;
		for(const unsigned Seed : {0x271828u, 0x314159u, 0xC0FFEEu})
		{
			SCOPED_TRACE(::testing::Message() << "seed=" << Seed);
			ASSERT_TRUE(Map().RestoreDocumentAtSafePoint(Baseline, [](const char *pError) { ADD_FAILURE() << pError; }));
			ASSERT_TRUE(Timeline().Initialize());
			Timeline().ApplyPreferences();
			CPlainGraph Model;
			Model.m_Values = static_cast<const CMapValues &>(Baseline);
			std::vector<CPlainGraph> vExpected{Model};
			size_t Cursor = 0;
			std::mt19937 Random(Seed);
			std::array<int, 16> aOperations{};
			int Owner;
			for(int Step = 0; Step < 192; ++Step)
			{
				int Operation = Step < 16 ? Step : Random() % 16;
				if(Model.m_vGroups.empty() && (Operation == 2 || Operation == 3 || Operation == 4 || Operation == 6 || Operation == 8 || Operation == 9 || Operation == 15))
					Operation = 0;
				if(Model.m_vEnvelopes.empty() && (Operation == 5 || Operation == 7))
					Operation = 1;
				SCOPED_TRACE(::testing::Message() << "step=" << Step << " operation=" << Operation);
				++aOperations[Operation];
				const auto Before = Model.Expected(Baseline);
				if(Operation == 11 || Operation == 12)
				{
					const bool Possible = Operation == 11 ? Cursor > 0 : Cursor + 1 < vExpected.size();
					EXPECT_EQ(Operation == 11 ? Timeline().Undo() : Timeline().Redo(), Possible);
					if(Possible)
					{
						ASSERT_TRUE(Timeline().PublishAtSafePoint());
						Cursor = Operation == 11 ? Cursor - 1 : Cursor + 1;
						Model = vExpected[Cursor];
					}
				}
				else if(Operation == 13)
				{
					ASSERT_TRUE(Timeline().Begin(&Owner, "Random cancelled structure", editor_history::ECategory::MAP));
					ASSERT_TRUE(Timeline().Update(&Owner, [&] {
						Map().NewGroup()->AddLayer(std::make_shared<CLayerQuads>(&Map()));
						Map().m_MapInfo.m_Author.Buffer()[0] = 'X';
					}));
					ASSERT_TRUE(Timeline().Cancel(CEditorDocumentHistory::ECancellation::ESCAPE));
					ASSERT_TRUE(Timeline().PublishAtSafePoint());
				}
				else
				{
					const size_t GroupIndex = Model.m_vGroups.empty() ? 0 : Random() % Model.m_vGroups.size();
					const size_t EnvelopeIndex = Model.m_vEnvelopes.empty() ? 0 : Random() % Model.m_vEnvelopes.size();
					ASSERT_TRUE(Timeline().Edit(&Owner, "Random structure", editor_history::ECategory::MAP, [&] {
						switch(Operation)
						{
						case 0:
						{
							CPlainGroup Group;
							auto pGroup = Map().NewGroup();
							Group.m_Values.m_Id = pGroup->m_Id;
							str_format(Group.m_Values.m_aName, sizeof(Group.m_Values.m_aName), "g%d", Step);
							str_copy(pGroup->m_aName, Group.m_Values.m_aName);
							CPlainLayer Quads, Sounds;
							auto pQuads = std::make_shared<CLayerQuads>(&Map());
							auto pSounds = std::make_shared<CLayerSounds>(&Map());
							Quads.m_Values.m_Id = pQuads->m_Id;
							Quads.m_Values.m_Type = LAYERTYPE_QUADS;
							Sounds.m_Values.m_Id = pSounds->m_Id;
							Sounds.m_Values.m_Type = LAYERTYPE_SOUNDS;
							Sounds.m_Data = CLayerSoundsValues{};
							pGroup->AddLayer(pQuads);
							pGroup->AddLayer(pSounds);
							Group.m_vLayers = {Quads, Sounds};
							Model.m_vGroups.push_back(std::move(Group));
							break;
						}
						case 1:
						{
							CEnvelopeValues Envelope;
							auto pEnvelope = std::make_shared<CEnvelope>(&Map(), CEnvelope::EType::POSITION);
							Envelope.m_Id = pEnvelope->m_Id;
							CEnvelopePointValues Point;
							Point.m_Id = Map().AllocateObjectId();
							Point.m_aValues[0] = Step * 31;
							Point.m_Curvetype = CURVETYPE_LINEAR;
							Envelope.m_vPoints.push_back(Point);
							pEnvelope->m_vPoints.push_back(Point);
							Map().m_vpEnvelopes.push_back(pEnvelope);
							Model.m_vEnvelopes.push_back(Envelope);
							break;
						}
						case 2:
						case 15:
							Model.m_vGroups[GroupIndex].m_Values.m_ClipW += Step + 1;
							Map().m_vpGroups[BaseGroups + GroupIndex]->m_ClipW += Step + 1;
							break;
						case 3:
						case 8:
						{
							// References are identity values; envelope order may change later.
							const CDocumentReference Reference{Model.m_vEnvelopes.empty() ? 0 : Model.m_vEnvelopes[EnvelopeIndex].m_Id};
							if(Operation == 8 && !Model.m_vEnvelopes.empty())
							{
								Model.m_vEnvelopes[EnvelopeIndex].m_vPoints[0].m_aValues[1] += Step;
								Map().m_vpEnvelopes[BaseEnvelopes + EnvelopeIndex]->m_vPoints[0].m_aValues[1] += Step;
							}
							for(size_t L = 0; L < Model.m_vGroups[GroupIndex].m_vLayers.size(); ++L)
							{
								auto &Layer = Model.m_vGroups[GroupIndex].m_vLayers[L];
								auto pLayer = Map().m_vpGroups[BaseGroups + GroupIndex]->m_vpLayers[L];
								if(auto *pQuads = std::get_if<CLayerQuadsValues>(&Layer.m_Data))
								{
									if(Operation == 8 && !pQuads->m_vQuads.empty())
									{
										pQuads->m_vQuads[0].m_aPoints[2].x += Step;
										std::static_pointer_cast<CLayerQuads>(pLayer)->m_vQuads[0].m_aPoints[2].x += Step;
										continue;
									}
									CQuadValues Quad;
									Quad.m_Id = Map().AllocateObjectId();
									Quad.m_aPoints[4] = {Step * 7, -Step};
									Quad.m_PosEnv = Reference;
									pQuads->m_vQuads.push_back(Quad);
									std::static_pointer_cast<CLayerQuads>(pLayer)->m_vQuads.push_back(Quad);
								}
								else
								{
									auto &Sources = std::get<CLayerSoundsValues>(Layer.m_Data).m_vSources;
									if(Operation == 8 && !Sources.empty())
									{
										Sources[0].m_TimeDelay += Step;
										std::static_pointer_cast<CLayerSounds>(pLayer)->m_vSources[0].m_TimeDelay += Step;
										continue;
									}
									CSoundSourceValues Source;
									Source.m_Id = Map().AllocateObjectId();
									Source.m_Position = {Step, Step * 9};
									Source.m_Shape.m_Type = CSoundShape::SHAPE_CIRCLE;
									Source.m_Shape.m_Circle.m_Radius = Step + 2;
									Source.m_PosEnv = Reference;
									std::get<CLayerSoundsValues>(Layer.m_Data).m_vSources.push_back(Source);
									std::static_pointer_cast<CLayerSounds>(pLayer)->m_vSources.push_back(Source);
								}
							}
							break;
						}
						case 4:
							Map().DeleteGroup(BaseGroups + GroupIndex);
							Model.m_vGroups.erase(Model.m_vGroups.begin() + GroupIndex);
							break;
						case 5:
						{
							const auto Id = Model.m_vEnvelopes[EnvelopeIndex].m_Id;
							Map().DeleteEnvelope(BaseEnvelopes + EnvelopeIndex);
							Model.m_vEnvelopes.erase(Model.m_vEnvelopes.begin() + EnvelopeIndex);
							for(auto &Group : Model.m_vGroups)
								for(auto &Layer : Group.m_vLayers)
									std::visit([&](auto &Data) {
										if constexpr(std::is_same_v<std::decay_t<decltype(Data)>, CLayerQuadsValues>)
										{
											for(auto &Quad : Data.m_vQuads)
												if(Quad.m_PosEnv.m_Id == Id)
													Quad.m_PosEnv = {};
										}
										else
											for(auto &Source : Data.m_vSources)
												if(Source.m_PosEnv.m_Id == Id)
													Source.m_PosEnv = {};
									},
										Layer.m_Data);
							break;
						}
						case 6:
							std::swap(Map().m_vpGroups[BaseGroups], Map().m_vpGroups[BaseGroups + GroupIndex]);
							std::swap(Model.m_vGroups[0], Model.m_vGroups[GroupIndex]);
							break;
						case 7:
							Map().MoveEnvelope(BaseEnvelopes + EnvelopeIndex, BaseEnvelopes);
							std::rotate(Model.m_vEnvelopes.begin(), Model.m_vEnvelopes.begin() + EnvelopeIndex, Model.m_vEnvelopes.begin() + EnvelopeIndex + 1);
							break;
						case 9:
							Map().m_vpGroups[BaseGroups + GroupIndex]->MoveLayer(0, 1);
							std::swap(Model.m_vGroups[GroupIndex].m_vLayers[0], Model.m_vGroups[GroupIndex].m_vLayers[1]);
							break;
						case 10:
							str_format(Model.m_Values.m_MapInfo.m_Author.Buffer(), 32, "seed%u step%d", Seed, Step);
							str_copy(Map().m_MapInfo.m_Author.Buffer(), Model.m_Values.m_MapInfo.m_Author.Buffer());
							Model.m_Values.m_vSettings.emplace_back("sv_test_cmds 1");
							Map().m_vSettings.emplace_back("sv_test_cmds 1");
							break;
						case 14: break; // true no-op, including after undo
						default: ADD_FAILURE() << "unhandled random operation";
						}
						Map().OnModify();
					}));
					if(!(Before == Model.Expected(Baseline)))
					{
						vExpected.resize(Cursor + 1);
						vExpected.push_back(Model);
						++Cursor;
					}
				}
				const auto Expected = Model.Expected(Baseline);
				std::string Error;
				ASSERT_TRUE(Expected.Validate(Error)) << Error;
				const auto Actual = Snapshot();
				ASSERT_TRUE(Actual.Validate(Error)) << Error;
				ASSERT_TRUE(Actual == Expected);
				ASSERT_TRUE(Timeline().History()->Current() == Expected);
				EXPECT_EQ(Timeline().History()->Cursor(), Cursor);
				EXPECT_EQ(Timeline().History()->Revisions().size(), vExpected.size());
				EXPECT_EQ(Timeline().CanRedo(), Cursor + 1 < vExpected.size());
			}
			for(const auto Count : aOperations)
				EXPECT_GT(Count, 0);
			// Every retained result is checked again after all subsequent mutations.
			while(Cursor > 0)
			{
				ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
				ASSERT_TRUE(Snapshot() == vExpected[--Cursor].Expected(Baseline));
			}
			while(Cursor + 1 < vExpected.size())
			{
				ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
				ASSERT_TRUE(Snapshot() == vExpected[++Cursor].Expected(Baseline));
			}
		}
	}

	TEST_F(CEditorHistoryRuntime, QuickActionsRestoreCompleteGraphAndDefaults)
	{
		std::vector<CEditorDocumentValues> Expected{Snapshot()};
		const auto Action = [&](auto &&Function) {
			Function();
			ASSERT_FALSE(Timeline().Active());
			ASSERT_FALSE(Timeline().Pending());
			ASSERT_EQ(Timeline().History()->Revisions().size(), Expected.size() + 1);
			AssertCurrent();
			Expected.push_back(Snapshot());
		};
		Action([&] { Editor().AddGroup(); });
		Action([&] { Editor().AddTileLayer(); });
		Action([&] { Editor().AddQuadsLayer(); });
		Action([&] { Editor().AddQuadOrSound(); });
		const auto pQuads = std::static_pointer_cast<CLayerQuads>(Map().SelectedLayer(0));
		ASSERT_EQ(pQuads->m_vQuads.size(), 1u);
		EXPECT_NE(pQuads->m_vQuads[0].m_Id, 0u);
		EXPECT_EQ(pQuads->m_vQuads[0].m_aPoints[1].x - pQuads->m_vQuads[0].m_aPoints[0].x, f2fx(64));
		Action([&] { Editor().AddSoundLayer(); });
		Action([&] { Editor().AddQuadOrSound(); });
		const auto &Source = std::static_pointer_cast<CLayerSounds>(Map().SelectedLayer(0))->m_vSources[0];
		EXPECT_NE(Source.m_Id, 0u);
		EXPECT_EQ(Source.m_Loop, 1);
		EXPECT_EQ(Source.m_Pan, 1);
		EXPECT_EQ(Source.m_Falloff, 80);
		EXPECT_EQ(Source.m_Shape.m_Circle.m_Radius, 1500);
		Action([&] { Editor().DeleteSelectedLayer(); });
		for(size_t I = Expected.size() - 1; I > 0; --I)
		{
			ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Expected[I - 1]);
		}
		for(size_t I = 1; I < Expected.size(); ++I)
		{
			ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Expected[I]);
		}
	}

	TEST_F(CEditorHistoryRuntime, ActualNumericDraftAcceptBlurEscapeAndLocalUndo)
	{
		SelectGame();
		const int Width = Map().m_pGameLayer->Width();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		const auto Open = [&] { Click(200, 26, Properties, SDL_BUTTON_RIGHT); ASSERT_TRUE(Timeline().Active()); ASSERT_NE(CLineInput::GetActiveInput(), nullptr); };
		Open();
		Type("-");
		Frame(Properties);
		Key(KEY_RETURN);
		Frame(Properties);
		EXPECT_TRUE(Timeline().Active());
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
		Type(std::to_string(Width + 10).c_str());
		Frame(Properties);
		Modifier(true);
		Key(KEY_Z);
		Modifier(false);
		Frame(Properties);
		EXPECT_STREQ(CLineInput::GetActiveInput()->GetString(), "-");
		Type(std::to_string(Width + 10).c_str());
		Frame(Properties);
		Key(KEY_RETURN);
		Frame(Properties);
		ASSERT_FALSE(Timeline().Active());
		ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width + 10);
		const auto Wider = Snapshot();
		AssertCurrent();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		SelectGame();
		Open();
		Type("-");
		Frame(Properties);
		Key(KEY_ESCAPE);
		Frame(Properties);
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		ASSERT_TRUE(Timeline().CanRedo());
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Wider);
		SelectGame();
		Open();
		Type(std::to_string(Width + 20).c_str());
		Frame(Properties);
		Click(600, 400, Properties);
		ASSERT_FALSE(Timeline().Active());
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width + 20);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 3u);
		AssertCurrent();
	}

	TEST_F(CEditorHistoryRuntime, BoundNameConsumesExhaustedLocalHistory)
	{
		SelectGame();
		const std::string Name = Map().m_pGameLayer->m_aName;
		{
			CLineInput Input(Map().m_pGameLayer->m_aName, sizeof(Map().m_pGameLayer->m_aName));
			const auto NameFrame = [&] { const CUIRect Rect{20, 20, 240, 24}; Editor().DoDocumentEditBox(&Input, &Rect, 12, "Rename layer"); };
			Click(70, 30, NameFrame);
			ASSERT_TRUE(Timeline().Owns(&Input));
			Type("new name");
			Frame(NameFrame);
			for(int I = 0; I < 3; ++I)
			{
				Modifier(true);
				Key(KEY_Z);
				Modifier(false);
				Frame(NameFrame);
			}
			EXPECT_EQ(Map().m_pGameLayer->m_aName, Name);
			EXPECT_TRUE(Timeline().Active());
			EXPECT_FALSE(Timeline().Pending());
			Type("renamed");
			Frame(NameFrame);
			Key(KEY_RETURN);
			EXPECT_FALSE(Timeline().Active());
			EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
			EXPECT_STREQ(Map().m_pGameLayer->m_aName, "renamed");
		}
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		SelectGame();
		{
			CLineInput Input(Map().m_pGameLayer->m_aName, sizeof(Map().m_pGameLayer->m_aName));
			const auto NameFrame = [&] { const CUIRect Rect{20, 20, 240, 24}; Editor().DoDocumentEditBox(&Input, &Rect, 12, "Rename layer"); };
			Click(70, 30, NameFrame);
			Type("cancel");
			Frame(NameFrame);
			Key(KEY_ESCAPE);
			EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
		}
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Timeline().CanRedo());
		EXPECT_EQ(Map().m_pGameLayer->m_aName, Name);
		AssertCurrent();
	}

	TEST_F(CEditorHistoryRuntime, MultiLayerStrokesIgnoreSamplingRateAndReleaseOutsideViewport)
	{
		Editor().AddGroup();
		Editor().AddTileLayer();
		Editor().AddTileLayer();
		const int Group = Map().m_SelectedGroup;
		Map().SelectLayer(0, Group);
		Map().m_vSelectedLayers = {0, 1};
		Editor().m_pBrush->Clear();
		auto pBrush = std::make_shared<CLayerTiles>(Editor().m_pBrush->Map(), 1, 1);
		CTile Tile{};
		Tile.m_Index = TILE_DEATH;
		pBrush->SetTile(0, 0, Tile);
		Editor().m_pBrush->AddLayer(pBrush);
		const auto Baseline = Snapshot();
		ASSERT_TRUE(Timeline().Clear());
		std::optional<CEditorDocumentValues> Result;
		for(const int Updates : {1, 10, 200, 1000})
		{
			Map().SelectLayer(0, Group);
			Map().m_vSelectedLayers = {0, 1};
			const CUIRect View{20, 20, 400, 300};
			const auto Paint = [&] { Editor().MapView()->Render(View); };
			Editor().m_ShowPicker = false;
			Editor().m_ShowTileInfo = CEditor::SHOW_TILE_OFF;
			Map().m_MapViewState.m_MouseWorldPos = vec2(32, 32);
			Move(100, 100);
			Frame(Paint);
			MouseButton(SDL_BUTTON_LEFT, true);
			Frame(Paint);
			ASSERT_TRUE(Timeline().Active());
			for(int I = 0; I < Updates; ++I)
				Frame(Paint);
			EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
			Move(500, 400);
			MouseButton(SDL_BUTTON_LEFT, false);
			Frame(Paint);
			ASSERT_FALSE(Timeline().Active());
			ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
			const auto Painted = Snapshot();
			if(Result)
				EXPECT_TRUE(*Result == Painted);
			else
				Result = Painted;
			for(const auto &pLayer : Map().m_vpGroups[Group]->m_vpLayers)
				EXPECT_EQ(std::static_pointer_cast<CLayerTiles>(pLayer)->GetTile(1, 1).m_Index, TILE_DEATH);
			ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Baseline);
			ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Painted);
			ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
			ASSERT_TRUE(Timeline().Clear());
		}
	}

	TEST_F(CEditorHistoryRuntime, SessionLocksDoNotBlockRestorationOrKeepDeletedWrapperAlive)
	{
		Editor().AddGroup();
		Editor().AddTileLayer();
		const auto LayerId = Map().SelectedLayer(0)->m_Id;
		Map().SelectedLayer(0)->m_Visible = false;
		Map().SelectedLayer(0)->m_Readonly = true;
		std::weak_ptr<CLayer> Old = Map().SelectedLayer(0);
		const auto BeforeDelete = Snapshot();
		Editor().DeleteSelectedLayer();
		EXPECT_TRUE(Old.expired());
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == BeforeDelete);
		std::shared_ptr<CLayer> Found;
		for(const auto &pGroup : Map().m_vpGroups)
			for(const auto &pLayer : pGroup->m_vpLayers)
				if(pLayer->m_Id == LayerId)
					Found = pLayer;
		ASSERT_NE(Found, nullptr);
		EXPECT_FALSE(Found->m_Visible);
		EXPECT_TRUE(Found->m_Readonly);
		EXPECT_EQ(Editor().Ui()->ActiveItem(), nullptr);
	}

	TEST_F(CEditorHistoryRuntime, FreshDocumentNoopAndCancelAgreeWithDirtyPresentation)
	{
		Editor().AddDefaultMap();
		ASSERT_TRUE(Map().m_Modified);
		ASSERT_TRUE(Timeline().History()->Dirty(editor_history::ESaveKind::MANUAL));
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "No change", editor_history::ECategory::MAP, [] {}));
		EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
		ASSERT_TRUE(Timeline().Begin(&Owner, "Cancel", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Cancel(CEditorDocumentHistory::ECancellation::ESCAPE));
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Map().m_Modified);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
		Editor().CloseMap(1, false);
	}

	TEST_F(CEditorHistoryRuntime, KeyboardUndoCancelsCapturedOwnerAndSecondUndoTraverses)
	{
		Editor().AddGroup();
		const auto Committed = Snapshot();
		int Owner;
		ASSERT_TRUE(Timeline().Begin(&Owner, "Captured transform", editor_history::ECategory::MAP));
		Editor().Ui()->SetActiveItem(&Owner);
		Timeline().TrackPointer(&Owner, &Owner);
		ASSERT_TRUE(Timeline().Update(&Owner, [&] { Map().m_vpGroups.back()->m_OffsetX += 25; }));
		Modifier(true);
		Key(KEY_Y);
		Modifier(false);
		EXPECT_TRUE(Timeline().Active());
		EXPECT_FALSE(Timeline().Pending());
		Modifier(true);
		Key(KEY_Z);
		Modifier(false);
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_EQ(Timeline().History()->Cursor(), 1u);
		EXPECT_TRUE(Snapshot() == Committed);
		Modifier(true);
		Key(KEY_Z);
		Modifier(false);
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		EXPECT_EQ(Timeline().History()->Cursor(), 0u);
		EXPECT_TRUE(Timeline().CanRedo());
	}

	TEST_F(CEditorHistoryRuntime, CaptureLossAndDisappearingControlCancelBeforePublication)
	{
		const auto Before = Snapshot();
		int Owner;
		ASSERT_TRUE(Timeline().Begin(&Owner, "Captured move", editor_history::ECategory::MAP));
		Editor().Ui()->SetActiveItem(&Owner);
		Timeline().TrackPointer(&Owner, &Owner);
		ASSERT_TRUE(Timeline().Update(&Owner, [&] { Map().m_vpGroups.back()->m_OffsetX += 25; }));
		Editor().Ui()->SetActiveItem(nullptr);
		Timeline().FinishFrame();
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		SelectGame();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type("99");
		Frame(Properties);
		Timeline().StartFrame();
		Timeline().FinishFrame();
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
		EXPECT_TRUE(Snapshot() == Before);
	}

	TEST_F(CEditorHistoryRuntime, ManualSaveSettlesNumericDraftAndAutosaveDefers)
	{
		g_Config.m_EdAutosaveInterval = 0;
		SelectGame();
		const int Width = Map().m_pGameLayer->Width();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		const auto SaveError = [](const char *pError) { ADD_FAILURE() << pError; };
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type(std::to_string(Width + 20).c_str());
		Frame(Properties);
		EXPECT_FALSE(Map().SaveWithKind("maps/deferred-auto.map", editor_history::ESaveKind::AUTOMATIC, SaveError));
		EXPECT_TRUE(Timeline().Active());
		EXPECT_TRUE(Editor().m_WriterFinishJobs.empty());
		ASSERT_TRUE(Editor().Save("maps/settled-width.map"));
		EXPECT_TRUE(Editor().m_WriterFinishJobs.empty());
		Frame(Properties);
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width + 20);
		Editor().OnRender();
		DrainSaves();
		EXPECT_FALSE(Map().m_Modified);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		CEditorMap ReadBack(&Editor());
		ASSERT_TRUE(ReadBack.Load("maps/settled-width.map", IStorage::TYPE_SAVE, SaveError));
		EXPECT_EQ(ReadBack.m_pGameLayer->Width(), Width + 20);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		SelectGame();
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type("-");
		Frame(Properties);
		ASSERT_TRUE(Editor().Save("maps/invalid-width.map"));
		Frame(Properties);
		ASSERT_TRUE(Timeline().Pending());
		Editor().OnRender();
		DrainSaves();
		EXPECT_FALSE(Map().m_Modified);
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_TRUE(Timeline().CanRedo());
		CEditorMap InvalidReadBack(&Editor());
		ASSERT_TRUE(InvalidReadBack.Load("maps/invalid-width.map", IStorage::TYPE_SAVE, SaveError));
		EXPECT_EQ(InvalidReadBack.m_pGameLayer->Width(), Width);
	}

	TEST_F(CEditorHistoryRuntime, MapSwitchSettlesValidDraftCancelsInvalidAndKeepsOtherDocument)
	{
		auto *pOriginal = &Map();
		const int Width = Map().m_pGameLayer->Width();
		Editor().AddDefaultMap();
		auto *pOther = &Map();
		const auto Other = Snapshot();
		Editor().SelectMap(0);
		SelectGame();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type(std::to_string(Width + 30).c_str());
		Frame(Properties);
		Editor().SelectMap(1);
		EXPECT_EQ(&Map(), pOriginal);
		Frame(Properties);
		Editor().OnRender();
		ASSERT_EQ(&Map(), pOther);
		EXPECT_EQ(pOriginal->m_pGameLayer->Width(), Width + 30);
		EXPECT_EQ(pOriginal->m_DocumentHistory.History()->Revisions().size(), 2u);
		EXPECT_TRUE(Snapshot() == Other);
		Editor().SelectMap(0);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		SelectGame();
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type("-");
		Frame(Properties);
		Editor().SelectMap(1);
		Frame(Properties);
		Editor().OnRender();
		ASSERT_EQ(&Map(), pOther);
		EXPECT_EQ(pOriginal->m_pGameLayer->Width(), Width);
		EXPECT_TRUE(pOriginal->m_DocumentHistory.CanRedo());
		EXPECT_TRUE(Snapshot() == Other);
		Editor().CloseMap(1, false);
	}

	TEST_F(CEditorHistoryRuntime, DeferredActionsCannotAddressReplacementDocument)
	{
		int Calls = 0;
		Editor().AddDefaultMap();
		const auto Lifetime = Map().Lifetime();
		Editor().DeferDocumentAction(&Map(), [&] { ++Calls; });
		Editor().CloseMap(1, false);
		Editor().AddDefaultMap();
		Editor().OnRender();
		EXPECT_EQ(Calls, 0);
		EXPECT_FALSE(Map().HasLifetime(Lifetime));
		EXPECT_TRUE(Lifetime.expired());
		Editor().CloseMap(1, false);
	}

	TEST_F(CEditorHistoryRuntime, RealEnvelopeMapSettingsButtonsShareFilteredChronology)
	{
		CEnvelopeEditor Envelopes;
		Envelopes.OnInit(&Editor());
		Envelopes.OnReset();
		const CUIRect View{20, 20, 800, 300};
		const auto EnvelopeFrame = [&] { Envelopes.Render(View); };
		const auto SettingsFrame = [&] { Editor().RenderServerSettingsEditor(View, true); };
		std::vector<CEditorDocumentValues> Expected{Snapshot()};
		const auto Before = Map().m_vpEnvelopes.size();
		Click(735, 27, EnvelopeFrame);
		ASSERT_EQ(Map().m_vpEnvelopes.size(), Before + 1);
		ASSERT_EQ(Timeline().History()->Revisions().back().m_Category, editor_history::ECategory::ENVELOPE);
		Expected.push_back(Snapshot());
		Editor().AddGroup();
		Expected.push_back(Snapshot());
		Map().m_MapSettingsCommandContext.m_CommandInput.Set("tune gravity 0.4321");
		Map().m_MapSettingsCommandContext.Update();
		Click(642.5f, 30, SettingsFrame);
		ASSERT_EQ(Timeline().History()->Revisions().size(), 4u);
		ASSERT_EQ(Timeline().History()->Revisions().back().m_Category, editor_history::ECategory::SETTINGS);
		Expected.push_back(Snapshot());
		Editor().Ui()->ClearObjectReferences();
		for(size_t I = Expected.size() - 1; I > 0; --I)
		{
			ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Expected[I - 1]);
		}
		for(size_t I = 1; I < Expected.size(); ++I)
		{
			ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Expected[I]);
		}
		Map().m_EditorHistoryUiState.m_Category = static_cast<int>(editor_history::ECategory::ENVELOPE);
		const auto HistoryFrame = [&] { Editor().RenderEditorHistory(View); };
		Click(120, 83, HistoryFrame);
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_EQ(Timeline().History()->Cursor(), 1u);
		EXPECT_TRUE(Snapshot() == Expected[1]);
	}

	TEST_F(CEditorHistoryRuntime, RetainedHighIdentityRestoresAndExhaustedCreationRollsBack)
	{
		CEditorMap Isolated(&Editor());
		Isolated.CreateDefault();
		auto High = *Isolated.m_DocumentHistory.History()->PinCurrent();
		High.m_Groups.Update([](auto &Groups) { Groups[0].Update([](auto &Group) { Group.m_Id = std::numeric_limits<std::uint64_t>::max() - 1; }); });
		std::string Error;
		ASSERT_TRUE(High.Validate(Error)) << Error;
		ASSERT_TRUE(Isolated.RestoreDocumentAtSafePoint(High, [](const char *pError) { ADD_FAILURE() << pError; }));
		ASSERT_TRUE(Isolated.m_DocumentHistory.Initialize());
		int Owner;
		ASSERT_FALSE(Isolated.m_DocumentHistory.Edit(&Owner, "Exhausted creation", editor_history::ECategory::MAP, [&] { Isolated.NewGroup(); }));
		ASSERT_TRUE(Isolated.m_DocumentHistory.Pending() && Isolated.m_DocumentHistory.PublishAtSafePoint());
		const auto After = Isolated.CaptureDocument(nullptr, Error);
		ASSERT_TRUE(After.has_value()) << Error;
		EXPECT_TRUE(*After == High);
		EXPECT_EQ(Isolated.m_DocumentHistory.History()->Revisions().size(), 1u);
	}

	TEST_F(CEditorHistoryRuntime, LiveRetentionPreferencesPreserveRedoAndMinimumPair)
	{
		Editor().AddGroup();
		Editor().AddTileLayer();
		Editor().AddQuadsLayer();
		const auto Final = Snapshot();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		g_Config.m_ClEditorMaxHistory = 1;
		g_Config.m_ClEditorHistoryMemory = 16384;
		Timeline().ApplyPreferences();
		EXPECT_FALSE(Timeline().History()->Oversized());
		EXPECT_GT(Timeline().History()->Revisions().size() - 1, Timeline().History()->Limits().m_Entries);
		int Owner;
		ASSERT_TRUE(Timeline().Begin(&Owner, "Count-only retained redo", editor_history::ECategory::MAP));
		EXPECT_FALSE(Timeline().CanRedo());
		EXPECT_LT(Timeline().History()->Cursor() + 1, Timeline().History()->Revisions().size());
		Frame([&] { Editor().RenderEditorHistory({20, 20, 1000, 300}); });
		ASSERT_TRUE(Timeline().Cancel(CEditorDocumentHistory::ECancellation::ESCAPE));
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		g_Config.m_ClEditorHistoryMemory = 1;
		Timeline().ApplyPreferences();
		EXPECT_TRUE(Timeline().CanUndo());
		EXPECT_TRUE(Timeline().CanRedo());
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Final);
		Editor().AddGroup();
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_TRUE(Timeline().CanUndo());
		const auto Usage = WaitForMemory();
		EXPECT_GT(Usage.m_LiveBytes, 0u);
		EXPECT_GT(Usage.m_CacheBytes, 0u);
		EXPECT_EQ(Usage.m_SavePinnedBytes, 0u);
		EXPECT_EQ(Usage.m_DraftExtraBytes, 0u);
		const CUIRect View{20, 20, 1000, 300};
		Frame([&] { Editor().RenderEditorHistory(View); });
		AssertCurrent();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
	}

	TEST_F(CEditorHistoryRuntime, ResourceImportAndAppendRedoUseRetainedInputs)
	{
		const auto Original = Snapshot();
		ASSERT_TRUE(CEditor::AddImage("mapres/retained-input.png", IStorage::TYPE_SAVE, &Editor()));
		ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
		const auto Imported = Snapshot();
		ASSERT_TRUE(Editor().Storage()->RemoveFile("mapres/retained-input.png", IStorage::TYPE_SAVE));
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Original);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Imported);
		ASSERT_TRUE(Map().Append("maps/append-input.map", IStorage::TYPE_SAVE, [](const char *pError) { ADD_FAILURE() << pError; }));
		ASSERT_EQ(Timeline().History()->Revisions().size(), 3u);
		const auto Appended = Snapshot();
		ASSERT_TRUE(Editor().Storage()->RemoveFile("maps/append-input.map", IStorage::TYPE_SAVE));
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Imported);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Appended);
		std::string Error;
		EXPECT_TRUE(Appended.Validate(Error)) << Error;
	}

	TEST_F(CEditorHistoryRuntime, DetachedResourcePreparationFailureKeepsDocumentAndRedo)
	{
		const auto *pOriginalMap = &Map();
		Editor().AddDefaultMap();
		ASSERT_TRUE(CEditor::AddImage("mapres/preparation-original.png", IStorage::TYPE_SAVE, &Editor()));
		auto High = Snapshot();
		High.m_Groups.Update([](auto &Groups) { Groups[0].Update([](auto &Group) { Group.m_Id = std::numeric_limits<std::uint64_t>::max() - 1; }); });
		ASSERT_TRUE(Map().RestoreDocumentAtSafePoint(High, [](const char *pError) { ADD_FAILURE() << pError; }));
		ASSERT_TRUE(Timeline().Initialize());
		Map().m_SelectedImage = 0;
		const auto Id = Map().SelectedImage()->m_Id;
		// Replacement reuses identity even after every fresh ID has been consumed.
		ASSERT_TRUE(Editor().ReplaceImage("mapres/preparation-replace.png", IStorage::TYPE_SAVE, true));
		EXPECT_EQ(Map().SelectedImage()->m_Id, Id);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		ASSERT_TRUE(Editor().Save("maps/preparation-baseline.map"));
		DrainSaves();
		ASSERT_FALSE(Map().m_Modified);
		const auto Before = Snapshot();
		const auto Revisions = Timeline().History()->Revisions().size();
		EXPECT_FALSE(CEditor::AddImage("mapres/preparation-add.png", IStorage::TYPE_SAVE, &Editor()));
		if(Timeline().Pending())
		{
			ASSERT_TRUE(Timeline().PublishAtSafePoint());
		}
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_EQ(Timeline().History()->Revisions().size(), Revisions);
		EXPECT_TRUE(Timeline().CanRedo());
		EXPECT_FALSE(Map().m_Modified);
		CImageInfo Image;
		Image.m_Width = Image.m_Height = 2;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.AllocateFillZero();
		Map().AddTileArt(std::move(Image), "exhausted-art.png");
		if(Timeline().Pending())
		{
			ASSERT_TRUE(Timeline().PublishAtSafePoint());
		}
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_TRUE(Timeline().CanRedo());
		EXPECT_FALSE(Map().m_Modified);
		EXPECT_FALSE(Editor().Load("maps/no-preparation-input.map", IStorage::TYPE_SAVE));
		EXPECT_TRUE(Snapshot() == Before);
#if defined(__cpp_exceptions)
		std::weak_ptr<CEditorImage> WeakPrepared;
		const auto Result = Editor().PrepareDocumentOperation("allocation recovery", [&]() -> std::shared_ptr<CEditorImage> {
			auto pPrepared = std::make_shared<CEditorImage>(&Map(), Id);
			WeakPrepared = pPrepared;
			throw std::bad_alloc();
		});
		EXPECT_FALSE(Result);
		EXPECT_TRUE(WeakPrepared.expired());
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_TRUE(Timeline().CanRedo());
#endif
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_STREQ(Map().m_vpImages[0]->m_aName, "preparation-replace");
		Editor().CloseMap(1, false);
		EXPECT_EQ(Editor().Map(), pOriginalMap);
	}

#if defined(__cpp_exceptions)
	TEST_F(CEditorHistoryRuntime, LoadedDocumentSurvivesCompletionNotificationFailure)
	{
		str_copy(Map().m_aFilename, "maps/fixture.map");
		const auto *pBefore = &Map();
		bool CallbackRan = false;
		ASSERT_TRUE(Editor().LoadWithCallback("maps/fixture.map", IStorage::TYPE_SAVE, [&] {
			CallbackRan = true;
			Map().m_ValidSaveFilename = true;
			throw std::bad_alloc();
		}));
		EXPECT_TRUE(CallbackRan);
		EXPECT_NE(&Map(), pBefore);
		EXPECT_TRUE(Map().m_ValidSaveFilename);
		EXPECT_STREQ(Map().m_aFilename, "maps/fixture.map");
		EXPECT_STREQ(Map().m_aDisplayName, "fixture (2)");
		AssertCurrent();
		Editor().AddGroup();
		ASSERT_TRUE(Timeline().CanUndo());
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		Editor().CloseMap(1, false);
		EXPECT_EQ(&Map(), pBefore);
		EXPECT_STREQ(Map().m_aDisplayName, "fixture");
	}
#endif

	TEST_F(CEditorHistoryRuntime, GeneratedTileAndQuadArtAreSingleDeterministicEdits)
	{
		const auto Image = [] {
			CImageInfo Result;
			Result.m_Width = 2;
			Result.m_Height = 2;
			Result.m_Format = CImageInfo::FORMAT_RGBA;
			Result.AllocateFillZero();
			for(size_t I = 0; I < 4; ++I)
			{
				Result.m_pData[I * 4 + I % 3] = 255;
				Result.m_pData[I * 4 + 3] = I == 3 ? 0 : 255;
			}
			return Result;
		};
		const auto Original = Snapshot();
		const auto Groups = Map().m_vpGroups.size();
		Map().AddTileArt(Image(), "missing-source-tile.png");
		ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
		ASSERT_EQ(Map().m_vpGroups.size(), Groups + 1);
		const auto Tiles = Snapshot();
		auto pTiles = std::static_pointer_cast<CLayerTiles>(Map().m_vpGroups.back()->m_vpLayers[0]);
		EXPECT_EQ(pTiles->Width(), 2);
		EXPECT_EQ(pTiles->Height(), 2);
		EXPECT_EQ(pTiles->GetTile(1, 1).m_Index, 0);
		CQuadArtParameters Parameters{};
		Parameters.m_ImagePixelSize = 1;
		Parameters.m_QuadPixelSize = 8;
		Parameters.m_Optimize = true;
		str_copy(Parameters.m_aFilename, "missing-source-quad.png");
		Map().AddQuadArt(Image(), Parameters);
		ASSERT_EQ(Timeline().History()->Revisions().size(), 3u);
		const auto Quads = Snapshot();
		const auto pQuads = std::static_pointer_cast<CLayerQuads>(Map().m_vpGroups.back()->m_vpLayers[0]);
		ASSERT_EQ(pQuads->m_vQuads.size(), 3u);
		for(const auto &Quad : pQuads->m_vQuads)
			EXPECT_NE(Quad.m_Id, 0u);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Tiles);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Original);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Tiles);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Quads);
	}

	TEST_F(CEditorHistoryRuntime, ActualQuadAndSoundBrushesPlaceEveryObjectInOneEntry)
	{
		Editor().AddGroup();
		Editor().AddQuadsLayer();
		Editor().AddQuadsLayer();
		Editor().AddSoundLayer();
		Editor().AddSoundLayer();
		const int Group = Map().m_SelectedGroup;
		for(int Type = 0; Type < 2; ++Type)
		{
			Map().SelectLayer(Type * 2, Group);
			Map().m_vSelectedLayers = {Type * 2, Type * 2 + 1};
			Editor().m_pBrush->Clear();
			if(Type == 0)
			{
				auto pBrush = std::make_shared<CLayerQuads>(Editor().m_pBrush->Map());
				for(int I = 0; I < 3; ++I)
					pBrush->NewQuad(32 * I, 0, 16, 16);
				Editor().m_pBrush->AddLayer(pBrush);
			}
			else
			{
				auto pBrush = std::make_shared<CLayerSounds>(Editor().m_pBrush->Map());
				for(int I = 0; I < 3; ++I)
					pBrush->NewSource(32 * I, 0);
				Editor().m_pBrush->AddLayer(pBrush);
			}
			ASSERT_TRUE(Timeline().Clear());
			const auto Before = Snapshot();
			const CUIRect View{20, 20, 400, 300};
			const auto Paint = [&] { Editor().MapView()->Render(View); };
			Map().m_MapViewState.m_MouseWorldPos = vec2(32, 32);
			Click(100, 100, Paint);
			ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
			for(int L = 0; L < 2; ++L)
			{
				const auto pLayer = Map().m_vpGroups[Group]->m_vpLayers[Type * 2 + L];
				if(Type == 0)
					EXPECT_EQ(std::static_pointer_cast<CLayerQuads>(pLayer)->m_vQuads.size(), 3u);
				else
					EXPECT_EQ(std::static_pointer_cast<CLayerSounds>(pLayer)->m_vSources.size(), 3u);
			}
			const auto Placed = Snapshot();
			ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Before);
			ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
			EXPECT_TRUE(Snapshot() == Placed);
		}
	}

	TEST_F(CEditorHistoryRuntime, ActualMultiQuadTransformRestoresEverySelectedPoint)
	{
		Editor().AddGroup();
		Editor().AddQuadsLayer();
		auto pLayer = std::static_pointer_cast<CLayerQuads>(Map().SelectedLayer(0));
		ASSERT_TRUE(Timeline().Edit(pLayer.get(), "Prepare quads", editor_history::ECategory::MAP, [&] { pLayer->NewQuad(64, 64, 16, 16); pLayer->NewQuad(128, 64, 16, 16); }));
		Map().m_vSelectedQuads = {0, 1};
		Map().m_MapGridState.m_GridActive = false;
		g_Config.m_EdAlignQuads = 0;
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		const auto Draw = [&] {
			Editor().Graphics()->QuadsBegin();
			Editor().Ui()->SetHotItem(&pLayer->m_vQuads[0].m_aPoints[4]);
			Editor().DoQuad(Map().m_vSelectedLayers[0], pLayer, &pLayer->m_vQuads[0], 0);
			Editor().Graphics()->QuadsEnd();
		};
		Map().m_MapViewState.m_MouseWorldPos = vec2(64, 64);
		Move(100, 100);
		Frame(Draw);
		MouseButton(SDL_BUTTON_LEFT, true);
		Frame(Draw);
		Move(140, 130);
		Map().m_MapViewState.m_MouseWorldPos = vec2(96, 80);
		Map().m_MapViewState.m_MouseDeltaWorld = vec2(32, 16);
		Frame(Draw);
		ASSERT_TRUE(Timeline().Active());
		MouseButton(SDL_BUTTON_LEFT, false);
		Frame(Draw);
		ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_EQ(pLayer->m_vQuads[0].m_aPoints[4].x, f2fx(96));
		EXPECT_EQ(pLayer->m_vQuads[1].m_aPoints[4].x, f2fx(160));
		const auto Moved = Snapshot();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Moved);
	}

	TEST_F(CEditorHistoryRuntime, DocumentTextStillDeliversPhysicalReleaseToComponents)
	{
		SelectGame();
		Key(KEY_F8);
		ASSERT_TRUE(gs_ReleaseProbe.m_Held);
		CLineInput Input(Map().m_pGameLayer->m_aName, sizeof(Map().m_pGameLayer->m_aName));
		const auto NameFrame = [&] { const CUIRect Rect{20, 20, 240, 24}; Editor().DoDocumentEditBox(&Input, &Rect, 12, "Rename layer"); };
		Click(70, 30, NameFrame);
		ASSERT_TRUE(Input.IsActive());
		Key(KEY_F8, IInput::FLAG_RELEASE);
		EXPECT_FALSE(gs_ReleaseProbe.m_Held);
		Key(KEY_ESCAPE);
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
	}

	TEST_F(CEditorHistoryRuntime, ClosingEditorSettlesTypedNumberBeforeHiding)
	{
		SelectGame();
		const int Width = Map().m_pGameLayer->Width();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type(std::to_string(Width + 7).c_str());
		Frame(Properties);
		ASSERT_FALSE(Editor().OnClose());
		Frame(Properties);
		Editor().OnRender();
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width + 7);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_FALSE(Timeline().Active());
		EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
		EXPECT_EQ(g_Config.m_ClEditor, 0);
		g_Config.m_ClEditor = 1;
	}

	TEST_F(CEditorHistoryRuntime, DeferredLoadAppliesCompletionToPreparedDocument)
	{
		SelectGame();
		const int Width = Map().m_pGameLayer->Width();
		auto *pOriginal = &Map();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type(std::to_string(Width + 9).c_str());
		Frame(Properties);
		CEditorMap *pCompleted = nullptr;
		ASSERT_TRUE(Editor().LoadWithCallback("maps/fixture.map", IStorage::TYPE_SAVE, [&] { pCompleted = &Map(); }));
		EXPECT_EQ(&Map(), pOriginal);
		EXPECT_EQ(pCompleted, nullptr);
		Frame(Properties);
		Editor().OnRender();
		EXPECT_NE(&Map(), pOriginal);
		EXPECT_EQ(pCompleted, &Map());
		EXPECT_EQ(pOriginal->m_pGameLayer->Width(), Width + 9);
		EXPECT_EQ(Map().m_pGameLayer->Width(), Width);
		Editor().CloseMap(1, false);
	}

	TEST_F(CEditorHistoryRuntime, DeferredSavePreservesCloseAfterAcceptedDraft)
	{
		Editor().AddDefaultMap();
		SelectGame();
		const auto Lifetime = Map().Lifetime();
		const auto Properties = [&] { CUIRect Rect{20, 20, 240, 160}; Map().m_pGameLayer->RenderProperties(&Rect); };
		Click(200, 26, Properties, SDL_BUTTON_RIGHT);
		Type("64");
		Frame(Properties);
		Editor().m_CloseMapAfterSave = true;
		ASSERT_TRUE(CEditor::CallbackSaveMap("maps/close-draft.map", IStorage::TYPE_SAVE, &Editor()));
		Frame(Properties);
		Editor().OnRender();
		DrainSaves();
		EXPECT_TRUE(Lifetime.expired());
		EXPECT_FALSE(Map().HasLifetime(Lifetime));
	}

	TEST_F(CEditorHistoryRuntime, FontToolUndoAndIntentionalSwitchRetireToolState)
	{
		ASSERT_FALSE(Map().m_vpImages.empty());
		Editor().AddGroup();
		Editor().AddTileLayer();
		auto pLayer = std::static_pointer_cast<CLayerTiles>(Map().SelectedLayer(0));
		ASSERT_TRUE(Timeline().Edit(pLayer.get(), "Prepare font layer", editor_history::ECategory::MAP, [&] { pLayer->m_Image = Map().ImageReference(0); }));
		pLayer->m_KnownTextModeLayer = true;
		Map().m_MapViewState.m_MouseWorldPos = vec2(32, 32);
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		Modifier(true);
		Key(KEY_T);
		Modifier(false);
		ASSERT_TRUE(Editor().FontTyper()->IsActive());
		for(const int KeyCode : {KEY_A, KEY_B, KEY_SPACE, KEY_C, KEY_D})
			Key(KeyCode);
		const auto Typed = Snapshot();
		EXPECT_EQ(Map().m_FontTyperState.m_TextIndex, ivec2(6, 1));
		Modifier(true);
		Key(KEY_LEFT);
		EXPECT_EQ(Map().m_FontTyperState.m_TextIndex, ivec2(3, 1));
		Key(KEY_RIGHT);
		EXPECT_EQ(Map().m_FontTyperState.m_TextIndex, ivec2(6, 1));
		Key(KEY_HOME);
		EXPECT_EQ(Map().m_FontTyperState.m_TextIndex, ivec2(1, 1));
		Key(KEY_END);
		EXPECT_EQ(Map().m_FontTyperState.m_TextIndex, ivec2(6, 1));
		EXPECT_TRUE(Snapshot() == Typed);
		Key(KEY_Z);
		Modifier(false);
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_FALSE(Editor().FontTyper()->IsActive());
		EXPECT_EQ(Editor().m_Dialog, DIALOG_NONE);
		EXPECT_TRUE(Snapshot() == Before);
		Map().m_MapViewState.m_MouseWorldPos = vec2(32, 32);
		Modifier(true);
		Key(KEY_T);
		Modifier(false);
		Key(KEY_A);
		Editor().SwitchTool([&] { Editor().m_Mode = MODE_IMAGES; });
		EXPECT_FALSE(Editor().FontTyper()->IsActive());
		EXPECT_EQ(Editor().m_Dialog, DIALOG_NONE);
		EXPECT_FALSE(Timeline().Active());
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_FALSE(Snapshot() == Before);
		AssertCurrent();
	}

	TEST_F(CEditorHistoryRuntime, ColorPopupAcceptEscapeAndForcedClosureUseOneOwner)
	{
		Editor().AddGroup();
		Editor().AddTileLayer();
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		const auto Properties = [&] {
			CUIRect Rect{20, 20, 240, 200};
			std::static_pointer_cast<CLayerTiles>(Map().SelectedLayer(0))->RenderProperties(&Rect);
		};
		const auto OpenAndPreview = [&] {
			Click(200, 91, Properties);
			ASSERT_TRUE(Editor().Ui()->IsPopupOpen(&Editor().m_ColorPickerPopupContext));
			Editor().m_ColorPickerPopupContext.m_RgbaColor = ColorRGBA(0.25f, 0.5f, 0.75f, 1);
			Frame(Properties);
			ASSERT_TRUE(Timeline().Active());
		};
		OpenAndPreview();
		Editor().Ui()->ClosePopupMenus();
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		OpenAndPreview();
		Key(KEY_ESCAPE);
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		OpenAndPreview();
		Editor().Ui()->ClosePopupMenu(&Editor().m_ColorPickerPopupContext, false, CUi::EPopupCloseReason::ACCEPT);
		Frame(Properties);
		EXPECT_FALSE(Timeline().Active());
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_EQ(std::static_pointer_cast<CLayerTiles>(Map().SelectedLayer(0))->m_Color.r, 64);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
	}

	TEST_F(CEditorHistoryRuntime, SavingColorPopupSettlesValidAndCancelsInvalidChildDraft)
	{
		Editor().AddGroup();
		Editor().AddTileLayer();
		ASSERT_TRUE(Timeline().Clear());
		SPopupMenuId PopupId;
		const auto Open = [&] {
			Editor().Ui()->DoPopupMenu(&PopupId, 20, 20, 240, 240, Map().SelectedLayer(0).get(), [](void *pContext, CUIRect Rect, bool) {
				return static_cast<CLayerTiles *>(pContext)->RenderProperties(&Rect);
			});
			Editor().m_ColorPickerPopupContext.m_ColorMode = CUi::SColorPickerPopupContext::MODE_RGBA;
			const auto Popups = [&] { Editor().Ui()->RenderPopupMenus(); };
			Click(200, 98, Popups);
			ASSERT_TRUE(Editor().Ui()->IsPopupOpen(&Editor().m_ColorPickerPopupContext));
			// Child popup at (200, 98), inner margin 7, red selector after 143px.
			Click(225, 256, Popups, SDL_BUTTON_RIGHT);
			ASSERT_TRUE(Editor().Ui()->ValueSelectorTextActive());
		};
		Open();
		Type("17");
		ASSERT_TRUE(Editor().Save("maps/color-draft.map"));
		Editor().OnRender();
		Editor().OnRender();
		DrainSaves();
		EXPECT_FALSE(Editor().DocumentNumberInputActive());
		Editor().OnRender();
		EXPECT_FALSE(Timeline().Active());
		EXPECT_FALSE(Editor().Ui()->IsPopupOpen(&Editor().m_ColorPickerPopupContext));
		EXPECT_EQ(std::static_pointer_cast<CLayerTiles>(Map().SelectedLayer(0))->m_Color.r, 17);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_FALSE(Map().m_Modified);
		Editor().Reset();
		const auto Before = Snapshot();
		Open();
		Editor().m_ColorPickerPopupContext.m_RgbaColor = ColorRGBA(0.75f, 0.5f, 0.25f, 1);
		Type("-");
		ASSERT_TRUE(Editor().Save("maps/invalid-color.map"));
		Editor().OnRender();
		Editor().OnRender();
		DrainSaves();
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_FALSE(Timeline().Active());
		EXPECT_FALSE(Map().m_Modified);
	}

	TEST_F(CEditorHistoryRuntime, EverySpecialPlaneRestoresAndPersistsThroughNativeSaveLoad)
	{
		Editor().AddDefaultMap();
		SelectGame();
		Editor().AddFrontLayer();
		Editor().AddTeleLayer();
		Editor().AddSpeedupLayer();
		Editor().AddSwitchLayer();
		Editor().AddTuneLayer();
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Resize special-plane fixture", editor_history::ECategory::MAP, [&] { Map().m_pGameLayer->Resize(65, 33); }));
		const std::array<std::size_t, 4> aIndices{31, 32, 32 * 65 + 63, 32 * 65 + 64};
		const std::array<CTeleTile, 4> aTele{{{11, TILE_TELEIN}, {12, 255}, {13, TILE_TELEOUT}, {14, TILE_TELEIN}}};
		const std::array<CSpeedupTile, 4> aSpeedup{{{7, 30, TILE_SPEED_BOOST, 0, -135}, {8, 31, 255, 0, 90}, {0, 32, TILE_SPEED_BOOST, 0, 45}, {9, 33, TILE_SPEED_BOOST, 0, 273}}};
		const std::array<CSwitchTile, 4> aSwitch{{{21, TILE_SWITCHOPEN, TILEFLAG_ROTATE | TILEFLAG_OPAQUE, 17}, {22, 255, TILEFLAG_XFLIP, 18}, {23, ENTITY_CRAZY_SHOTGUN + 1 + ENTITY_OFFSET, TILEFLAG_YFLIP, 19}, {24, ENTITY_ARMOR_1 + ENTITY_OFFSET, TILEFLAG_XFLIP | TILEFLAG_YFLIP, 20}}};
		const std::array<CTuneTile, 4> aTune{{{31, TILE_TUNE}, {32, 255}, {33, TILE_TUNE}, {34, TILE_TUNE}}};
		const std::array<unsigned char, 4> aTeleBase{TILE_TELEIN, 0, TILE_TELEOUT, TILE_TELEIN};
		const std::array<unsigned char, 4> aSpeedupBase{TILE_SPEED_BOOST, 0, 0, TILE_SPEED_BOOST};
		const std::array<unsigned char, 4> aSwitchBase{TILE_SWITCHOPEN, 0, 0, ENTITY_ARMOR_1 + ENTITY_OFFSET};
		const std::array<unsigned char, 4> aTuneBase{TILE_TUNE, 0, TILE_TUNE, TILE_TUNE};
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		ASSERT_TRUE(Timeline().Edit(&Owner, "Populate all special planes", editor_history::ECategory::MAP, [&] {
			Map().m_pGameLayer->SetTile(1, 0, CTile{TILE_SOLID, TILEFLAG_XFLIP});
			Map().m_pFrontLayer->SetTile(2, 0, CTile{TILE_DEATH, TILEFLAG_YFLIP});
			Map().m_pTeleLayer->SetTile(3, 0, CTile{TILE_TELEIN});
			Map().m_pTeleLayer->m_TeleTiles.Set(3, CTeleTile{19, TILE_TELEIN});
			Map().m_pSpeedupLayer->SetTile(4, 0, CTile{TILE_SPEED_BOOST});
			Map().m_pSpeedupLayer->m_SpeedupTiles.Set(4, CSpeedupTile{43, 91, TILE_SPEED_BOOST, 0, 273});
			Map().m_pSwitchLayer->SetTile(5, 0, CTile{TILE_SWITCHOPEN, TILEFLAG_ROTATE});
			Map().m_pSwitchLayer->m_SwitchTiles.Set(5, CSwitchTile{21, TILE_SWITCHOPEN, TILEFLAG_ROTATE, 17});
			Map().m_pTuneLayer->SetTile(6, 0, CTile{TILE_TUNE});
			Map().m_pTuneLayer->m_TuneTiles.Set(6, CTuneTile{27, TILE_TUNE});
			for(std::size_t I = 0; I < aIndices.size(); ++I)
			{
				const auto Cell = aIndices[I];
				Map().m_pTeleLayer->m_TeleTiles.Set(Cell, aTele[I]);
				Map().m_pSpeedupLayer->m_SpeedupTiles.Set(Cell, aSpeedup[I]);
				Map().m_pSwitchLayer->m_SwitchTiles.Set(Cell, aSwitch[I]);
				Map().m_pTuneLayer->m_TuneTiles.Set(Cell, aTune[I]);
				Map().m_pTeleLayer->m_Tiles.Set(Cell, CTile{aTeleBase[I], 0, 0, 0});
				Map().m_pSpeedupLayer->m_Tiles.Set(Cell, CTile{aSpeedupBase[I], 0, 0, 0});
				Map().m_pSwitchLayer->m_Tiles.Set(Cell, CTile{aSwitchBase[I], static_cast<unsigned char>(aSwitchBase[I] ? aSwitch[I].m_Flags : 0), 0, 0});
				Map().m_pTuneLayer->m_Tiles.Set(Cell, CTile{aTuneBase[I], 0, 0, 0});
			}
			str_copy(Map().m_MapInfo.m_Author.Buffer(), "snapshot author");
			Map().m_vSettings.emplace_back("tune gravity 0.3456");
		}));
		const auto Populated = Snapshot();
		const auto AssertValues = [&](const CEditorMap &Document) {
			EXPECT_EQ(Document.m_pGameLayer->GetTile(1, 0).m_Index, TILE_SOLID);
			EXPECT_EQ(Document.m_pFrontLayer->GetTile(2, 0).m_Index, TILE_DEATH);
			EXPECT_EQ(Document.m_pTeleLayer->m_TeleTiles[3].m_Number, 19);
			EXPECT_EQ(Document.m_pTeleLayer->m_TeleTiles[3].m_Type, TILE_TELEIN);
			EXPECT_EQ(Document.m_pSpeedupLayer->m_SpeedupTiles[4].m_Force, 43);
			EXPECT_EQ(Document.m_pSpeedupLayer->m_SpeedupTiles[4].m_MaxSpeed, 91);
			EXPECT_EQ(Document.m_pSpeedupLayer->m_SpeedupTiles[4].m_Angle, 273);
			EXPECT_EQ(Document.m_pSwitchLayer->m_SwitchTiles[5].m_Number, 21);
			EXPECT_EQ(Document.m_pSwitchLayer->m_SwitchTiles[5].m_Delay, 17);
			EXPECT_EQ(Document.m_pSwitchLayer->m_SwitchTiles[5].m_Flags, TILEFLAG_ROTATE);
			EXPECT_EQ(Document.m_pTuneLayer->m_TuneTiles[6].m_Number, 27);
			EXPECT_EQ(Document.m_pTeleLayer->GetTile(3, 0).m_Index, TILE_TELEIN);
			EXPECT_EQ(Document.m_pSpeedupLayer->GetTile(4, 0).m_Index, TILE_SPEED_BOOST);
			EXPECT_EQ(Document.m_pSwitchLayer->GetTile(5, 0).m_Index, TILE_SWITCHOPEN);
			EXPECT_EQ(Document.m_pSwitchLayer->GetTile(5, 0).m_Flags, TILEFLAG_ROTATE);
			EXPECT_EQ(Document.m_pTuneLayer->GetTile(6, 0).m_Index, TILE_TUNE);
			auto Tele = Document.m_pTeleLayer->TeleTilesForRender();
			auto Speedup = Document.m_pSpeedupLayer->SpeedupTilesForRender();
			auto Switch = Document.m_pSwitchLayer->SwitchTilesForRender();
			auto Tune = Document.m_pTuneLayer->TuneTilesForRender();
			for(std::size_t I = 0; I < aIndices.size(); ++I)
			{
				const auto Cell = aIndices[I];
				EXPECT_EQ(Document.m_pTeleLayer->m_TeleTiles[Cell], CTeleTileValues(aTele[I]));
				EXPECT_EQ(Document.m_pSpeedupLayer->m_SpeedupTiles[Cell], CSpeedupTileValues(aSpeedup[I]));
				EXPECT_EQ(Document.m_pSwitchLayer->m_SwitchTiles[Cell], CSwitchTileValues(aSwitch[I]));
				EXPECT_EQ(Document.m_pTuneLayer->m_TuneTiles[Cell], CTuneTileValues(aTune[I]));
				EXPECT_EQ(CTeleTileValues(Tele[Cell]), CTeleTileValues(aTele[I]));
				EXPECT_EQ(CSpeedupTileValues(Speedup[Cell]), CSpeedupTileValues(aSpeedup[I]));
				EXPECT_EQ(Speedup[Cell].m_MustBe0, 0);
				EXPECT_EQ(CSwitchTileValues(Switch[Cell]), CSwitchTileValues(aSwitch[I]));
				EXPECT_EQ(CTuneTileValues(Tune[Cell]), CTuneTileValues(aTune[I]));
				EXPECT_EQ(Document.m_pTeleLayer->TilesForRender(&Document)[Cell].m_Index, aTeleBase[I]);
				EXPECT_EQ(Document.m_pSpeedupLayer->TilesForRender(&Document)[Cell].m_Index, aSpeedupBase[I]);
				const auto SwitchBase = Document.m_pSwitchLayer->TilesForRender(&Document)[Cell];
				EXPECT_EQ(SwitchBase.m_Index, aSwitchBase[I]);
				EXPECT_EQ(SwitchBase.m_Flags, aSwitchBase[I] ? aSwitch[I].m_Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE) : 0);
				EXPECT_EQ(Document.m_pTuneLayer->TilesForRender(&Document)[Cell].m_Index, aTuneBase[I]);
			}
			EXPECT_STREQ(Document.m_MapInfo.m_Author.Buffer(), "snapshot author");
		};
		AssertValues(Map());
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Populated);
		AssertValues(Map());
		ASSERT_TRUE(Editor().Save("maps/special-planes.map"));
		DrainSaves();
		EXPECT_TRUE(Snapshot() == Populated);
		CEditorMap Loaded(&Editor());
		ASSERT_TRUE(Loaded.Load("maps/special-planes.map", IStorage::TYPE_SAVE, [](const char *pError) { ADD_FAILURE() << pError; }));
		AssertValues(Loaded);
		std::string Error;
		const auto Reloaded = Loaded.CaptureDocument(nullptr, Error);
		ASSERT_TRUE(Reloaded.has_value()) << Error;
		EXPECT_EQ(EditorDocumentPersistedKey(*Reloaded), EditorDocumentPersistedKey(Populated));
		Editor().CloseMap(1, false);
	}

	TEST_F(CEditorHistoryRuntime, FailedValidationPreservesRedoBaselineAndSuccessfulSaveMarker)
	{
		ASSERT_TRUE(Editor().Save("maps/validated-baseline.map"));
		DrainSaves();
		ASSERT_FALSE(Map().m_Modified);
		const auto Before = Snapshot();
		Editor().AddGroup();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		int Owner;
		ASSERT_FALSE(Timeline().Edit(&Owner, "Invalid duplicate identity", editor_history::ECategory::MAP, [&] {
			Map().m_vpGroups[1]->m_Id = Map().m_vpGroups[0]->m_Id;
		}));
		ASSERT_TRUE(Timeline().Pending() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_TRUE(Timeline().CanRedo());
		EXPECT_FALSE(Map().m_Modified);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
	}

	TEST_F(CEditorHistoryRuntime, FailedResourceRealizationKeepsRecordedBytesAndReportsPlaceholder)
	{
		const auto Before = Snapshot();
		int Owner;
		const unsigned char aInvalidOpus[] = {1, 3, 5, 7, 11, 13};
		ASSERT_TRUE(Timeline().Edit(&Owner, "Retain unavailable sample", editor_history::ECategory::MAP, [&] {
			auto pSound = std::make_shared<CEditorSound>(&Map());
			str_copy(pSound->m_aName, "unavailable sample");
			pSound->m_Content.emplace(aInvalidOpus);
			Map().m_vpSounds.push_back(std::move(pSound));
		}));
		const auto WithSound = Snapshot();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == WithSound);
		const auto &Sound = *Map().m_vpSounds.back();
		EXPECT_LT(Sound.m_SoundId, 0);
		ASSERT_EQ(Sound.DataSize(), sizeof(aInvalidOpus));
		EXPECT_EQ(mem_comp(Sound.Data(), aInvalidOpus, sizeof(aInvalidOpus)), 0);
		EXPECT_TRUE(std::any_of(Editor().m_PopupMessageContexts.begin(), Editor().m_PopupMessageContexts.end(), [](const auto &Context) {
			return str_find(Context.second->m_aMessage, "playback sample could not be created") != nullptr;
		}));
	}

	TEST_F(CEditorHistoryRuntime, ActualQuadKnifeCompletesOrCancelsWholeMultiClickTool)
	{
		Editor().AddGroup();
		Editor().AddQuadsLayer();
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Prepare knife quad", editor_history::ECategory::MAP, [&] {
			std::static_pointer_cast<CLayerQuads>(Map().SelectedLayer(0))->NewQuad(64, 64, 128, 128);
		}));
		Map().SelectQuad(0);
		Map().m_MapGridState.m_GridActive = false;
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		const auto Slice = [&] {
			Editor().QuadKnife()->Activate(0);
			for(const auto Point : {vec2(16, 16), vec2(112, 16), vec2(112, 112), vec2(16, 112)})
			{
				Map().m_MapViewState.m_MouseWorldPos = Point;
				Click(100, 100, [&] { Editor().QuadKnife()->DoSlice(); });
			}
			EXPECT_EQ(std::static_pointer_cast<CLayerQuads>(Map().SelectedLayer(0))->m_vQuads.size(), 2u);
			EXPECT_TRUE(Timeline().Active());
		};
		Slice();
		Editor().QuadKnife()->Deactivate();
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		const auto Sliced = Snapshot();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		Slice();
		Key(KEY_ESCAPE);
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_FALSE(Editor().QuadKnife()->IsActive());
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Sliced);
	}

	TEST_F(CEditorHistoryRuntime, PhysicalSettingReorderHoldIncludesRepeatsUntilRelease)
	{
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Prepare settings", editor_history::ECategory::SETTINGS, [&] {
			Map().m_vSettings = {CEditorMapSetting("tune gravity 0.4"), CEditorMapSetting("tune ground_control_speed 10"), CEditorMapSetting("tune air_control_speed 8"), CEditorMapSetting("tune hook_length 400")};
		}));
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		Map().m_MapSettingsCommandContext.m_CommandSelectedIndex = 3;
		PhysicalKey(SDL_SCANCODE_LALT, SDLK_LALT, true);
		for(int Repeat = 0; Repeat < 3; ++Repeat)
		{
			PhysicalKey(SDL_SCANCODE_UP, SDLK_UP, true, Repeat != 0);
			Frame([&] { Editor().RenderServerSettingsEditor({20, 20, 800, 300}, true); });
			EXPECT_TRUE(Timeline().Active());
			EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
		}
		PhysicalKey(SDL_SCANCODE_UP, SDLK_UP, false);
		PhysicalKey(SDL_SCANCODE_LALT, SDLK_LALT, false);
		EXPECT_FALSE(Timeline().Active());
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_STREQ(Map().m_vSettings.front().m_Command.Buffer(), "tune hook_length 400");
		const auto Reordered = Snapshot();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Reordered);
	}

	TEST_F(CEditorHistoryRuntime, ActualEnvelopeDragMovesAllSelectedPointsAndCommitsOnRelease)
	{
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Prepare point drag", editor_history::ECategory::ENVELOPE, [&] {
			auto pEnvelope = Map().NewEnvelope(CEnvelope::EType::POSITION);
			pEnvelope->m_vPoints[0].m_aValues[0] = 256;
			pEnvelope->m_vPoints[1].m_aValues[0] = 768;
			Map().m_SelectedEnvelope = Map().m_vpEnvelopes.size() - 1;
		}));
		CEnvelopeEditor Envelopes;
		Envelopes.OnInit(&Editor());
		auto &State = Map().m_EnvelopeEditorState;
		State.m_ResetZoom = false;
		State.m_ZoomX.SetValueInstant(2);
		State.m_ZoomY.SetValueInstant(2);
		State.m_Offset = vec2(0.1f, 0.1f);
		State.m_ActiveChannels = 1;
		Map().m_vSelectedEnvelopePoints = {{0, 0}, {1, 0}};
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		const auto Draw = [&] { Envelopes.Render({20, 20, 800, 300}); };
		Move(100, 259.25f);
		Frame(Draw);
		MouseButton(SDL_BUTTON_LEFT, true);
		Frame(Draw);
		Move(100, 270);
		Frame(Draw);
		Move(100, 290);
		Frame(Draw);
		ASSERT_TRUE(Timeline().Active());
		MouseButton(SDL_BUTTON_LEFT, false);
		Frame(Draw);
		EXPECT_FALSE(Timeline().Pending());
		ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
		const auto &Points = Map().m_vpEnvelopes[Map().m_SelectedEnvelope]->m_vPoints;
		EXPECT_LT(Points[0].m_aValues[0], 256);
		EXPECT_EQ(768 - Points[1].m_aValues[0], 256 - Points[0].m_aValues[0]);
		const auto Moved = Snapshot();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Moved);
	}

	TEST_F(CEditorHistoryRuntime, ActualEnvelopePopupValidatesDraftAndEscapeReleasesFocusedOwner)
	{
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Prepare point input", editor_history::ECategory::ENVELOPE, [&] {
			auto pEnvelope = Map().NewEnvelope(CEnvelope::EType::POSITION);
			pEnvelope->m_vPoints[0].m_aValues[0] = 256;
			pEnvelope->m_vPoints[1].m_aValues[0] = 768;
			Map().m_SelectedEnvelope = Map().m_vpEnvelopes.size() - 1;
		}));
		CEnvelopeEditor Envelopes;
		Envelopes.OnInit(&Editor());
		auto &State = Map().m_EnvelopeEditorState;
		State.m_ResetZoom = false;
		State.m_ZoomX.SetValueInstant(2);
		State.m_ZoomY.SetValueInstant(2);
		State.m_Offset = vec2(0.1f, 0.1f);
		State.m_ActiveChannels = 1;
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		const auto Draw = [&] { Envelopes.Render({20, 20, 800, 300}); Editor().Ui()->RenderPopupMenus(); };
		const auto Open = [&] {
			Map().SelectEnvPoint(1, 0);
			Click(500, 191.75f, Draw, SDL_BUTTON_RIGHT);
			ASSERT_TRUE(Editor().Ui()->IsPopupOpen());
			Click(610, 205, Draw);
			ASSERT_NE(CLineInput::GetActiveInput(), nullptr);
		};
		Open();
		Type("-");
		Key(KEY_RETURN);
		Frame(Draw);
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_NE(CLineInput::GetActiveInput(), nullptr);
		Type("1.25");
		Key(KEY_RETURN);
		Frame(Draw);
		EXPECT_EQ(Map().m_vpEnvelopes[Map().m_SelectedEnvelope]->m_vPoints[1].m_aValues[0], 1280);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		Open();
		Type("9.75");
		Key(KEY_ESCAPE);
		Frame(Draw);
		EXPECT_FALSE(Editor().Ui()->IsPopupOpen());
		EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
		EXPECT_FALSE(Editor().DocumentNumberInputActive());
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_TRUE(Timeline().CanRedo());
	}

#if defined(_MSC_VER) && defined(_DEBUG)
	// The debug CRT supplies a test-only allocation hook. Other threads keep
	// allocating normally, including the client's asynchronous I/O workers.
	// Skip small debug iterator proxies: MSVC allocates those in noexcept
	// empty-vector constructors, unlike the release library. Fail staging storage.
	thread_local bool gs_FailCurrentThreadAllocation = false;
	std::atomic<_CRT_ALLOC_HOOK> gs_PreviousAllocHook{nullptr};
	int __cdecl RejectTestAllocation(int Type, void *pData, size_t Size, int Block, long Request, const unsigned char *pFile, int Line)
	{
		if(Type == _HOOK_ALLOC && Size > 64 && gs_FailCurrentThreadAllocation)
		{
			gs_FailCurrentThreadAllocation = false; // exception machinery may allocate
			return false;
		}
		const auto Previous = gs_PreviousAllocHook.load();
		return !Previous || Previous(Type, pData, Size, Block, Request, pFile, Line);
	}
	class CFailAllocationScope
	{
	public:
		CFailAllocationScope()
		{
			gs_PreviousAllocHook = _CrtSetAllocHook(RejectTestAllocation);
			gs_FailCurrentThreadAllocation = true;
		}
		~CFailAllocationScope()
		{
			gs_FailCurrentThreadAllocation = false;
			_CrtSetAllocHook(gs_PreviousAllocHook.load());
		}
	};

	TEST_F(CEditorHistoryRuntime, FailedNavigationDismissesRequestButRequiredRollbackKeepsPending)
	{
		const auto Before = Snapshot();
		Editor().AddGroup();
		const auto After = Snapshot();
		const auto Cursor = Timeline().History()->Cursor();
		ASSERT_TRUE(Timeline().Undo());
		bool Published;
		{
			CFailAllocationScope Failure;
			Published = Timeline().PublishAtSafePoint();
		}
		EXPECT_FALSE(Published);
		EXPECT_FALSE(Timeline().Pending());
		EXPECT_EQ(Timeline().History()->Cursor(), Cursor);
		EXPECT_TRUE(Snapshot() == After);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Resume after failed navigation", editor_history::ECategory::MAP, [&] { Map().m_vpGroups[0]->m_OffsetX += 17; }));
		const auto Committed = Snapshot();
		ASSERT_TRUE(Editor().Save("maps/after-navigation-failure.map"));
		DrainSaves();
		ASSERT_TRUE(Timeline().Begin(&Owner, "Required rollback", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Update(&Owner, [&] { Map().m_vpGroups[0]->m_OffsetX += 33; }));
		ASSERT_TRUE(Timeline().Cancel(CEditorDocumentHistory::ECancellation::ESCAPE));
		{
			CFailAllocationScope Failure;
			Published = Timeline().PublishAtSafePoint();
		}
		EXPECT_FALSE(Published);
		EXPECT_TRUE(Timeline().Pending());
		EXPECT_FALSE(Snapshot() == Committed);
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Committed);
		EXPECT_FALSE(Map().m_Modified);
	}
#endif

	TEST_F(CEditorHistoryRuntime, LocalSettersRecordOnlyActiveInputsAndOneEvent)
	{
		CLineInputBuffered<256> Input;
		Input.Set("inactive");
		Input.SetRange("replace", 0, 8);
		Input.Clear();
		Input.Set("base");
		Input.Activate(EInputPriority::UI);
		EXPECT_FALSE(Input.UndoText());
		Input.Set("first");
		Input.SetRange("next", 0, 5);
		Input.Clear();
		ASSERT_TRUE(Input.UndoText());
		EXPECT_STREQ(Input.GetString(), "next");
		ASSERT_TRUE(Input.UndoText());
		EXPECT_STREQ(Input.GetString(), "first");
		ASSERT_TRUE(Input.UndoText());
		EXPECT_STREQ(Input.GetString(), "base");
		EXPECT_FALSE(Input.UndoText());
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_TEXT;
		str_copy(Event.m_aText, "x");
		ASSERT_TRUE(Input.ProcessInput(Event));
		EXPECT_STREQ(Input.GetString(), "basex");
		ASSERT_TRUE(Input.UndoText());
		EXPECT_STREQ(Input.GetString(), "base");
		EXPECT_FALSE(Input.UndoText()); // nested SetRange must not record a second edit
		Input.Deactivate();
		Input.Set("after");
		Input.Activate(EInputPriority::UI);
		EXPECT_FALSE(Input.UndoText());
		Input.Deactivate();
	}

	TEST_F(CEditorHistoryRuntime, RetentionMemoryPreferencesSaturateAtNativeSizeLimit)
	{
		for(const int MiB : {4095, 4096, 16384})
		{
			g_Config.m_ClEditorHistoryMemory = MiB;
			const auto Expected = std::min<std::uint64_t>(static_cast<std::uint64_t>(MiB) * 1048576, std::numeric_limits<size_t>::max());
			Timeline().ApplyPreferences();
			EXPECT_EQ(Timeline().History()->Limits().m_Bytes, Expected);
			ASSERT_TRUE(Timeline().Initialize());
			EXPECT_EQ(Timeline().History()->Limits().m_Bytes, Expected);
		}
	}

	TEST_F(CEditorHistoryRuntime, ImageCopyAllocationFailureReturnsEmptyAndRetainsPixels)
	{
		CEditorImage Image(&Map());
		const std::array<std::uint8_t, 4> Pixels{1, 2, 3, 4};
		Image.m_Content.emplace(Pixels);
		// A standalone deliberately oversized projection exercises malloc failure
		// without a production allocator hook or modifying a recorded document.
		Image.m_Width = std::numeric_limits<size_t>::max() / 2;
		Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_R;
		auto Copy = Image.ImageCopy();
		EXPECT_EQ(Copy.m_pData, nullptr);
		EXPECT_EQ(Copy.m_Width, 0u);
		EXPECT_EQ(Copy.m_Height, 0u);
		EXPECT_FALSE(Editor().Graphics()->LoadTextureRaw(std::move(Copy), 0, "failed image projection").IsValid());
		EXPECT_EQ(Image.m_Content->Bytes().size(), Pixels.size());
		EXPECT_TRUE(std::equal(Pixels.begin(), Pixels.end(), Image.m_Content->Bytes().begin()));
	}

	TEST_F(CEditorHistoryRuntime, NativeSoundImportReplacementAndPreparedSampleLifetime)
	{
		const auto Before = Snapshot();
		ASSERT_TRUE(CEditor::AddSound("mapres/sound-original.opus", IStorage::TYPE_SAVE, &Editor()));
		const auto Id = Map().SelectedSound()->m_Id;
		const auto SampleId = Map().SelectedSound()->m_SoundId;
		ASSERT_GE(SampleId, 0);
		const auto Duration = Editor().Sound()->GetSampleTotalTime(SampleId);
		EXPECT_GT(Duration, 0.0f);
		const auto Imported = Snapshot();
		std::weak_ptr<CEditorSound> Old = Map().SelectedSound();
		ASSERT_TRUE(Editor().ReplaceSound("mapres/sound-replace.opus", IStorage::TYPE_SAVE, true));
		EXPECT_EQ(Map().SelectedSound()->m_Id, Id);
		EXPECT_TRUE(Old.expired());
		EXPECT_NE(Map().SelectedSound()->m_SoundId, SampleId);
		EXPECT_GT(Editor().Sound()->GetSampleTotalTime(Map().SelectedSound()->m_SoundId), Duration);
		const auto Replaced = Snapshot();
		ASSERT_TRUE(Editor().Storage()->RemoveFile("mapres/sound-original.opus", IStorage::TYPE_SAVE));
		ASSERT_TRUE(Editor().Storage()->RemoveFile("mapres/sound-replace.opus", IStorage::TYPE_SAVE));
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Imported);
		EXPECT_FLOAT_EQ(Editor().Sound()->GetSampleTotalTime(Map().SelectedSound()->m_SoundId), Duration);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Before);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Imported);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Replaced);
#if defined(__cpp_exceptions)
		std::weak_ptr<CEditorSound> Prepared;
		int PreparedSample = -1;
		const auto Content = Map().m_vpSounds[Map().SoundIndex({Id})]->m_Content;
		ASSERT_TRUE(Content);
		auto Result = Editor().PrepareDocumentOperation("prepared sample failure", [&]() -> std::shared_ptr<CEditorSound> {
			auto pSound = std::make_shared<CEditorSound>(&Map());
			Prepared = pSound;
			pSound->m_SoundId = Editor().Sound()->LoadOpusFromMem(Content->Bytes().data(), Content->Bytes().size(), true, "prepared sample");
			PreparedSample = pSound->m_SoundId;
			throw std::bad_alloc();
		});
		EXPECT_FALSE(Result);
		ASSERT_GE(PreparedSample, 0);
		EXPECT_TRUE(Prepared.expired());
		CEditorSound Probe(&Map());
		Probe.m_SoundId = Editor().Sound()->LoadOpusFromMem(Content->Bytes().data(), Content->Bytes().size(), true, "sample slot reuse");
		// The native decoder's free list reuses the released prepared slot.
		EXPECT_EQ(Probe.m_SoundId, PreparedSample);
		EXPECT_GT(Editor().Sound()->GetSampleTotalTime(Probe.m_SoundId), 0.0f);
		EXPECT_TRUE(Snapshot() == Replaced);
#endif
	}

	TEST_F(CEditorHistoryRuntime, RestoredImageLoadsRulesOnActualPaintAndRedoUsesRecordedTiles)
	{
		ASSERT_TRUE(CEditor::AddImage("mapres/automatic-input.png", IStorage::TYPE_SAVE, &Editor()));
		const auto ImageId = Map().SelectedImage()->m_Id;
		Editor().AddGroup();
		Editor().AddTileLayer();
		const int Group = Map().m_SelectedGroup;
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Prepare automatic layer", editor_history::ECategory::MAP, [&] {
			auto pLayer = std::static_pointer_cast<CLayerTiles>(Map().SelectedLayer(0));
			pLayer->m_Image = {ImageId};
			pLayer->m_AutomapperConfig = 0;
			pLayer->m_Seed = 17;
			pLayer->m_AutoAutomapper = true;
		}));
		Map().m_SelectedImage = Map().ImageIndex({ImageId});
		std::weak_ptr<CEditorImage> Original = Map().SelectedImage();
		ASSERT_TRUE(Editor().ReplaceImage("mapres/automatic-replace.png", IStorage::TYPE_SAVE, true));
		ASSERT_TRUE(Original.expired());
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		const auto pImage = Map().m_vpImages[Map().ImageIndex({ImageId})];
		EXPECT_EQ(pImage->m_Automapper.ConfigNamesNum(), 0); // restoration does not read rules
		Map().SelectLayer(0, Group);
		Editor().m_pBrush->Clear();
		auto pBrush = std::make_shared<CLayerTiles>(Editor().m_pBrush->Map(), 1, 1);
		pBrush->SetTile(0, 0, CTile{1, 0, 0, 0});
		Editor().m_pBrush->AddLayer(pBrush);
		const auto Baseline = Snapshot();
		ASSERT_TRUE(Timeline().Clear());
		const auto Paint = [&] { Editor().MapView()->Render({20, 20, 400, 300}); };
		Editor().m_ShowPicker = false;
		Editor().m_ShowTileInfo = CEditor::SHOW_TILE_OFF;
		Map().m_MapViewState.m_MouseWorldPos = vec2(32, 32);
		Move(100, 100);
		Frame(Paint);
		MouseButton(SDL_BUTTON_LEFT, true);
		Frame(Paint);
		for(int Update = 0; Update < 20; ++Update)
			Frame(Paint);
		Move(500, 400);
		MouseButton(SDL_BUTTON_LEFT, false);
		Frame(Paint);
		EXPECT_EQ(pImage->m_Automapper.ConfigNamesNum(), 1);
		ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
		EXPECT_EQ(std::static_pointer_cast<CLayerTiles>(Map().m_vpGroups[Group]->m_vpLayers[0])->GetTile(1, 1).m_Index, 7);
		const auto Generated = Snapshot();
		ASSERT_TRUE(Editor().Storage()->RemoveFile("editor/automap/automatic-input.rules", IStorage::TYPE_SAVE));
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Baseline);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		EXPECT_TRUE(Snapshot() == Generated);
	}

	TEST_F(CEditorHistoryRuntime, EnvelopePopupAutosaveDefersAndFocusLossDiscardsOwnerlessDraft)
	{
		CEditorMap Other(&Editor());
		Other.CreateDefault();
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Prepare popup", editor_history::ECategory::ENVELOPE, [&] {
			auto pEnvelope = Map().NewEnvelope(CEnvelope::EType::POSITION);
			pEnvelope->m_vPoints[0].m_aValues[0] = 256;
			pEnvelope->m_vPoints[1].m_aValues[0] = 768;
			Map().m_SelectedEnvelope = Map().m_vpEnvelopes.size() - 1;
		}));
		CEnvelopeEditor Envelopes;
		Envelopes.OnInit(&Editor());
		auto &State = Map().m_EnvelopeEditorState;
		State.m_ResetZoom = false;
		State.m_ZoomX.SetValueInstant(2);
		State.m_ZoomY.SetValueInstant(2);
		State.m_Offset = vec2(0.1f, 0.1f);
		State.m_ActiveChannels = 1;
		ASSERT_TRUE(Timeline().Clear());
		const auto Before = Snapshot();
		const auto Draw = [&] { Envelopes.Render({20, 20, 800, 300}); Editor().Ui()->RenderPopupMenus(); };
		Map().SelectEnvPoint(1, 0);
		Click(500, 191.75f, Draw, SDL_BUTTON_RIGHT);
		ASSERT_TRUE(Editor().Ui()->IsPopupOpen());
		Click(610, 205, Draw);
		ASSERT_NE(CLineInput::GetActiveInput(), nullptr);
		Type("9.75");
		ASSERT_TRUE(Editor().DocumentNumberInputActive());
		ASSERT_FALSE(Timeline().Active());
		const auto LastSave = Map().m_LastSaveTime;
		const auto Jobs = Editor().m_WriterFinishJobs.size();
		EXPECT_FALSE(Map().SaveWithKind("maps/deferred-popup.map", editor_history::ESaveKind::AUTOMATIC, [](const char *pError) { ADD_FAILURE() << pError; }));
		EXPECT_TRUE(Map().PerformAutosave([](const char *pError) { ADD_FAILURE() << pError; }));
		EXPECT_EQ(Map().m_LastSaveTime, LastSave);
		EXPECT_EQ(Editor().m_WriterFinishJobs.size(), Jobs);
		EXPECT_TRUE(Other.SaveWithKind("maps/other-auto.map", editor_history::ESaveKind::AUTOMATIC, [](const char *pError) { ADD_FAILURE() << pError; }));
		DrainSaves();
		// Override only the existing engine focus query; editor graphics still
		// point at the real initialized headless backend. No production test hook.
		class CInactiveGraphics : public CGraphics_Threaded
		{
			int WindowActive() override { return 0; }
		};
		auto Inactive = std::make_unique<CInactiveGraphics>();
		auto *pGraphics = gs_pKernel->RequestInterface<IEngineGraphics>();
		gs_pKernel->ReregisterInterface<IEngineGraphics>(Inactive.get());
		Editor().CEditor::OnUpdate();
		gs_pKernel->ReregisterInterface<IEngineGraphics>(pGraphics);
		EXPECT_FALSE(Editor().Ui()->IsPopupOpen());
		EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
		EXPECT_FALSE(Editor().DocumentNumberInputActive());
		Frame(Draw);
		Key(KEY_RETURN);
		Frame(Draw);
		EXPECT_TRUE(Snapshot() == Before);
		EXPECT_EQ(Timeline().History()->Revisions().size(), 1u);
	}

	TEST_F(CEditorHistoryRuntime, ErrorPopupsReuseContextAndOwnItUntilDismissalOrShutdown)
	{
		Editor().Ui()->ClosePopupMenus();
		Editor().FreeDynamicPopupMenus();
		ASSERT_TRUE(Editor().m_PopupMessageContexts.empty());
		Editor().ShowFileDialogError("history failure %d", 17);
		Editor().ShowFileDialogError("history failure %d", 17);
		ASSERT_EQ(Editor().m_PopupMessageContexts.size(), 1u);
		EXPECT_TRUE(Editor().Ui()->IsPopupOpen(Editor().m_PopupMessageContexts.begin()->second.get()));
		Editor().Ui()->ClosePopupMenus();
		Editor().FreeDynamicPopupMenus();
		EXPECT_TRUE(Editor().m_PopupMessageContexts.empty());
		// Leave a live error popup at shutdown too: its scoped owner must release it
		// even when there is no subsequent rendered frame to collect closed popups.
		Editor().ShowFileDialogError("history failure at shutdown");
	}

	TEST_F(CEditorHistoryRuntime, MemoryDetailsRefreshAutomaticallyAndRetainedBytesStayCurrent)
	{
		ASSERT_TRUE(Timeline().Initialize());
		const auto Before = Timeline().MemoryUsage();
		EXPECT_FALSE(Before.m_Current);
		const auto Sample = WaitForMemory();
		EXPECT_TRUE(Sample.m_Sampled);
		EXPECT_TRUE(Sample.m_Current);
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Memory sampling edit", editor_history::ECategory::MAP, [&] {
			Map().m_pGameLayer->m_Tiles.Set(0, CTile{TILE_SOLID, 0, 0, 0});
		}));
		const auto Stale = Timeline().MemoryUsage();
		EXPECT_TRUE(Stale.m_Sampled);
		EXPECT_FALSE(Stale.m_Current);
		EXPECT_EQ(Stale.m_LiveBytes, Sample.m_LiveBytes);
		editor_history::CStorageUsage Retained;
		Timeline().History()->AccountRetainedPayload(Retained);
		EXPECT_EQ(Timeline().History()->RetainedPayloadBytes(), Retained.Bytes());
		EXPECT_TRUE(WaitForMemory().m_Current);
	}

	TEST_F(CEditorHistoryRuntime, AutomaticMemoryCoversDraftNavigationRetentionAndSavePins)
	{
		AssertMemoryMatchesFresh();
		const auto Stable = Timeline().MemoryUsage();
		for(int FrameIndex = 0; FrameIndex < 10; ++FrameIndex)
			Frame([] {});
		EXPECT_EQ(Timeline().MemoryUsage().m_SamplesStarted, Stable.m_SamplesStarted);
		int Owner;
		ASSERT_TRUE(Timeline().Begin(&Owner, "Memory preview", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Update(&Owner, [&] { Map().m_pGameLayer->m_Tiles.Set(0, CTile{TILE_SOLID, 0, 0, 0}); }));
		AssertMemoryMatchesFresh();
		EXPECT_GT(Timeline().MemoryUsage().m_DraftExtraBytes, 0u);
		ASSERT_TRUE(Timeline().Cancel(CEditorDocumentHistory::ECancellation::ESCAPE));
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		AssertMemoryMatchesFresh();
		ASSERT_TRUE(Timeline().Edit(&Owner, "Memory edit", editor_history::ECategory::MAP, [&] { Map().m_pGameLayer->m_Tiles.Set(0, CTile{TILE_DEATH, 0, 0, 0}); }));
		AssertMemoryMatchesFresh();
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		AssertMemoryMatchesFresh();
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		AssertMemoryMatchesFresh();
		g_Config.m_ClEditorMaxHistory = 1;
		Timeline().ApplyPreferences();
		AssertMemoryMatchesFresh();
		ASSERT_TRUE(Timeline().Clear());
		AssertMemoryMatchesFresh();
		gs_ControlledEngine.m_Hold = true;
		gs_ControlledEngine.m_HoldSavesOnly = true;
		ASSERT_TRUE(Editor().Save("maps/memory-pins.map"));
		AssertMemoryMatchesFresh();
		EXPECT_GT(Timeline().MemoryUsage().m_SavePinnedBytes, 0u);
		const auto pSave = Editor().m_WriterFinishJobs.back();
		gs_ControlledEngine.Release();
		ASSERT_TRUE(WaitForJob(pSave));
		// Export ownership has retired, but the foreground has not collected the
		// completion ticket yet. A previously pinned sample is already stale.
		EXPECT_FALSE(Timeline().MemoryUsage().m_Current);
		EXPECT_EQ(WaitForMemory().m_SavePinnedBytes, 0u);
		DrainSaves();
		AssertMemoryMatchesFresh();
		EXPECT_EQ(Timeline().MemoryUsage().m_SavePinnedBytes, 0u);
		ASSERT_TRUE(CEditor::AddImage("editor/cursor.png", IStorage::TYPE_ALL, &Editor()));
		AssertMemoryMatchesFresh();
		Map().m_vpImages.back()->m_Automapper.Load("grass_main");
		AssertMemoryMatchesFresh();
		Map().m_vpImages.back()->m_Automapper.Unload();
		AssertMemoryMatchesFresh();
	}

	TEST_F(CEditorHistoryRuntime, IdleEditorRendersConvergeWithoutRepeatingMemorySamples)
	{
		const auto PreviousExtraEditor = Editor().m_ActiveExtraEditor;
		const auto PreviousAutosave = g_Config.m_EdAutosaveInterval;
		g_Config.m_EdAutosaveInterval = 0;
		Editor().m_ActiveExtraEditor = CEditor::EXTRAEDITOR_HISTORY;
		SelectGame();
		const auto RenderFrame = [&] {
			Editor().OnUpdate();
			Editor().OnRender();
			Editor().Graphics()->Swap();
			Editor().Graphics()->WaitForIdle();
		};
		Timeline().InvalidateMemoryUsage();
		auto Usage = Timeline().MemoryUsage();
		const auto Deadline = time_get_nanoseconds() + std::chrono::seconds(5);
		do
		{
			RenderFrame();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			Usage = Timeline().MemoryUsage();
		} while(!Usage.m_Current && time_get_nanoseconds() < Deadline);
		EXPECT_TRUE(Usage.m_Current);
		const auto Stable = Usage;
		for(int FrameIndex = 0; FrameIndex < 20; ++FrameIndex)
		{
			RenderFrame();
			Usage = Timeline().MemoryUsage();
			EXPECT_TRUE(Usage.m_Current);
			EXPECT_EQ(Usage.m_Generation, Stable.m_Generation);
			EXPECT_EQ(Usage.m_SamplesStarted, Stable.m_SamplesStarted);
		}
		Editor().m_ActiveExtraEditor = PreviousExtraEditor;
		g_Config.m_EdAutosaveInterval = PreviousAutosave;
	}

	TEST_F(CEditorHistoryRuntime, AutomaticMemoryCoalescesAndRetiresClosedDocumentObservations)
	{
		const auto Stable = WaitForMemory();
		gs_ControlledEngine.m_Hold = true;
		Timeline().InvalidateMemoryUsage();
		const auto Pending = Timeline().MemoryUsage();
		ASSERT_TRUE(Pending.m_Updating);
		ASSERT_EQ(gs_ControlledEngine.m_vHeld.size(), 1u);
		const auto pObsolete = gs_ControlledEngine.m_vHeld.front();
		Map().m_vSettings.reserve(Map().m_vSettings.capacity() + 10000);
		for(int Change = 0; Change < 100; ++Change)
			Timeline().InvalidateMemoryUsage();
		EXPECT_FALSE(Timeline().MemoryUsage().m_Current);
		EXPECT_EQ(Timeline().MemoryUsage().m_SamplesStarted, Pending.m_SamplesStarted);
		gs_ControlledEngine.Release();
		ASSERT_TRUE(WaitForJob(pObsolete));
		gs_ControlledEngine.m_Hold = true;
		const auto Replaced = Timeline().MemoryUsage();
		EXPECT_FALSE(Replaced.m_Current);
		EXPECT_EQ(Replaced.m_LiveBytes, Stable.m_LiveBytes);
		EXPECT_EQ(Replaced.m_SamplesStarted, Stable.m_SamplesStarted + 2);
		ASSERT_EQ(gs_ControlledEngine.m_vHeld.size(), 1u);
		gs_ControlledEngine.Release();
		const auto Latest = WaitForMemory();
		EXPECT_GT(Latest.m_LiveBytes, Stable.m_LiveBytes);
		AssertMemoryMatchesFresh();

		Editor().AddDefaultMap();
		const auto Lifetime = Map().Lifetime();
		gs_ControlledEngine.m_Hold = true;
		EXPECT_TRUE(Timeline().MemoryUsage().m_Updating);
		ASSERT_EQ(gs_ControlledEngine.m_vHeld.size(), 1u);
		const auto pClosedObservation = gs_ControlledEngine.m_vHeld.front();
		Editor().CloseMap(1, false);
		Editor().AddDefaultMap();
		EXPECT_TRUE(Lifetime.expired());
		EXPECT_EQ(pClosedObservation->State(), IJob::STATE_QUEUED);
		gs_ControlledEngine.Release();
		ASSERT_TRUE(WaitForJob(pClosedObservation));
		AssertMemoryMatchesFresh();
		Editor().CloseMap(1, false);
	}

#if defined(__cpp_exceptions)
	TEST_F(CEditorHistoryRuntime, AutomaticMemoryFailureRetriesAreBoundedAndRecover)
	{
		const auto Stable = WaitForMemory();
		gs_ControlledEngine.m_Fail = true;
		const auto Before = gs_ControlledEngine.m_Submissions;
		Timeline().InvalidateMemoryUsage();
		EXPECT_TRUE(Timeline().MemoryUsage().m_Failed);
		Map().m_vSettings.reserve(Map().m_vSettings.capacity() + 10000);
		for(int FrameIndex = 0; FrameIndex < 1000; ++FrameIndex)
		{
			Timeline().InvalidateMemoryUsage();
			EXPECT_FALSE(Timeline().MemoryUsage().m_Current);
		}
		EXPECT_EQ(gs_ControlledEngine.m_Submissions, Before + 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(550));
		EXPECT_TRUE(Timeline().MemoryUsage().m_Failed);
		EXPECT_EQ(gs_ControlledEngine.m_Submissions, Before + 2);
		gs_ControlledEngine.m_Fail = false;
		for(int FrameIndex = 0; FrameIndex < 1000; ++FrameIndex)
			EXPECT_FALSE(Timeline().MemoryUsage().m_Current);
		EXPECT_EQ(gs_ControlledEngine.m_Submissions, Before + 2);
		const auto Recovered = WaitForMemory();
		EXPECT_FALSE(Recovered.m_Failed);
		EXPECT_GT(Recovered.m_LiveBytes, Stable.m_LiveBytes);
		EXPECT_EQ(Recovered.m_SamplesStarted, Stable.m_SamplesStarted + 1);
		AssertMemoryMatchesFresh();
		// Successful observations are still refreshed immediately on an edit.
		Timeline().InvalidateMemoryUsage();
		EXPECT_TRUE(Timeline().MemoryUsage().m_Updating);
	}
#endif

	TEST_F(CEditorHistoryRuntime, DifferentDestinationsShareOneExportTurn)
	{
		CBlockingOutputStorage FirstStorage(Editor().Storage()), SecondStorage(Editor().Storage());
		FirstStorage.m_Block = true;
		CEditorSaveState Saves;
		const auto MakeJob = [&](IStorage *pStorage, const char *pDestination, const char *pTemporary) {
			auto Ticket = Saves.Capture(Timeline().History()->PinCurrent(), "queue test", pDestination, editor_history::ESaveKind::COPY);
			return std::make_shared<CDataFileWriterFinishJob>(pStorage, pTemporary, std::move(*Ticket));
		};
		const auto pFirst = MakeJob(&FirstStorage, "maps/turn-first.map", "maps/turn-first.tmp");
		const auto pSecond = MakeJob(&SecondStorage, "maps/turn-second.map", "maps/turn-second.tmp");
		Editor().QueueWriterFinishJob(pFirst);
		EXPECT_EQ(FirstStorage.m_Started.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);
		Editor().QueueWriterFinishJob(pSecond);
		const auto Deadline = time_get_nanoseconds() + std::chrono::seconds(5);
		while(pSecond->State() == IJob::STATE_QUEUED && time_get_nanoseconds() < Deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		EXPECT_EQ(pSecond->State(), IJob::STATE_RUNNING);
		EXPECT_EQ(SecondStorage.m_Started.get_future().wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
		EXPECT_EQ(SecondStorage.m_Opens.load(), 0);
		EXPECT_FALSE(Editor().Storage()->FileExists("maps/turn-second.tmp", IStorage::TYPE_SAVE));
		FirstStorage.Release();
		EXPECT_TRUE(WaitForJob(pFirst));
		EXPECT_TRUE(WaitForJob(pSecond));
		EXPECT_EQ(SecondStorage.m_Opens.load(), 1);
		EXPECT_STREQ(pFirst->ErrorMessage(), "");
		EXPECT_STREQ(pSecond->ErrorMessage(), "");
		DrainSaves();
	}

	TEST_F(CEditorHistoryRuntime, SaveMembershipInvalidatesOnlyItsOwningMap)
	{
		const auto OtherStable = WaitForMemory();
		auto *pOther = &Timeline();
		Editor().AddDefaultMap();
		WaitForMemory();
		gs_ControlledEngine.m_Hold = true;
		gs_ControlledEngine.m_HoldSavesOnly = true;
		ASSERT_TRUE(Editor().Save("maps/one-map-only.map"));
		EXPECT_GT(WaitForMemory().m_SavePinnedBytes, 0u);
		EXPECT_TRUE(pOther->MemoryUsage().m_Current);
		EXPECT_EQ(pOther->MemoryUsage().m_SamplesStarted, OtherStable.m_SamplesStarted);
		const auto pSave = Editor().m_WriterFinishJobs.back();
		gs_ControlledEngine.Release();
		ASSERT_TRUE(WaitForJob(pSave));
		EXPECT_EQ(WaitForMemory().m_SavePinnedBytes, 0u);
		EXPECT_TRUE(pOther->MemoryUsage().m_Current);
		EXPECT_EQ(pOther->MemoryUsage().m_SamplesStarted, OtherStable.m_SamplesStarted);
		DrainSaves();
		AssertMemoryMatchesFresh();
		EXPECT_TRUE(pOther->MemoryUsage().m_Current);
		EXPECT_EQ(pOther->MemoryUsage().m_SamplesStarted, OtherStable.m_SamplesStarted);
		Editor().CloseMap(1, false);
	}

	class CTileSubmissionGraphics : public CGraphics_Threaded
	{
		CScreenRect m_Screen{0, 0, 640, 480};
		std::array<float, 8> m_aUv{};
		int m_TextureIndex = -1;
		std::array<float, 4> m_aColor{};

	public:
		class CSubmission
		{
		public:
			std::array<float, 4> m_aRect;
			std::array<float, 8> m_aUv;
			std::array<float, 4> m_aColor;
			int m_TextureIndex;
			bool operator==(const CSubmission &) const = default;
		};
		std::vector<CSubmission> m_vSubmitted;
		std::array<int, 4> m_aStateChanges{}; // begin, color, end, screen
		bool m_TextureArrays = false;
		CTileSubmissionGraphics()
		{
			m_ScreenWidth = 1280;
			m_ScreenHeight = 720;
		}
		void MapScreen(const CScreenRect &Screen) override
		{
			m_Screen = Screen;
			++m_aStateChanges[3];
		}
		CScreenRect GetScreen() const override { return m_Screen; }
		bool HasTextureArraysSupport() override { return m_TextureArrays; }
		void QuadsBegin() override { ++m_aStateChanges[0]; }
		void QuadsEnd() override { ++m_aStateChanges[2]; }
		void QuadsTex3DBegin() override { QuadsBegin(); }
		void QuadsTex3DEnd() override { QuadsEnd(); }
		void SetColor(ColorRGBA Color) override
		{
			m_aColor = {Color.r, Color.g, Color.b, Color.a};
			++m_aStateChanges[1];
		}
		void QuadsSetSubsetFree(float X0, float Y0, float X1, float Y1, float X2, float Y2, float X3, float Y3, int Index) override
		{
			m_aUv = {X0, Y0, X1, Y1, X2, Y2, X3, Y3};
			m_TextureIndex = Index;
		}
		void QuadsDrawTL(const CQuadItem *pQuads, int Count) override
		{
			for(int Index = 0; Index < Count; ++Index)
				m_vSubmitted.push_back({{pQuads[Index].m_X, pQuads[Index].m_Y, pQuads[Index].m_Width, pQuads[Index].m_Height}, m_aUv, m_aColor, m_TextureIndex});
		}
		void QuadsTex3DDrawTL(const CQuadItem *pQuads, int Count) override { QuadsDrawTL(pQuads, Count); }
	};

	class COverlaySubmissionGraphics : public CTileSubmissionGraphics
	{
	public:
		struct CState
		{
			std::array<float, 4> m_aColor{0.2f, 0.3f, 0.4f, 0.5f};
			float m_Rotation = 0.25f;
			int m_Sprite = -1, m_Texture = -1;
			bool m_Drawing = false;
			bool operator==(const CState &) const = default;
		} m_State;
		struct CArrow
		{
			std::array<float, 3> m_aGeometry;
			CState m_State;
			bool operator==(const CArrow &) const = default;
		};
		std::vector<CArrow> m_vArrows;
		void TextureSet(CTextureHandle Texture) override { m_State.m_Texture = Texture.Id(); }
		void SelectSprite(int Id, int) override { m_State.m_Sprite = Id; }
		void QuadsSetRotation(float Angle) override { m_State.m_Rotation = Angle; }
		void QuadsBegin() override
		{
			CTileSubmissionGraphics::QuadsBegin();
			m_State.m_Drawing = true;
			m_State.m_Rotation = 0;
			m_State.m_aColor = {1, 1, 1, 1};
		}
		void QuadsEnd() override
		{
			CTileSubmissionGraphics::QuadsEnd();
			m_State.m_Drawing = false;
		}
		void SetColor(ColorRGBA Color) override
		{
			CTileSubmissionGraphics::SetColor(Color);
			m_State.m_aColor = {Color.r, Color.g, Color.b, Color.a};
		}
		void DrawSprite(float X, float Y, float Size) override { m_vArrows.push_back({{X, Y, Size}, m_State}); }
	};

	class COverlaySubmissionText : public ITextRender
	{
	public:
		struct CLabel
		{
			std::string m_Text;
			std::array<float, 3> m_aGeometry;
			std::array<float, 4> m_aColor;
			bool operator==(const CLabel &) const = default;
		};
		std::vector<CLabel> m_vLabels;
		ColorRGBA m_Color{0.2f, 0.3f, 0.4f, 0.5f};
		void TextColor(float R, float G, float B, float A) override { m_Color = ColorRGBA(R, G, B, A); }
		void TextColor(ColorRGBA Color) override { m_Color = Color; }
		ColorRGBA GetTextColor() const override { return m_Color; }
		void Text(float X, float Y, float Size, const char *pText, float) override
		{
			m_vLabels.push_back({pText, {X, Y, Size}, {m_Color.r, m_Color.g, m_Color.b, m_Color.a}});
		}
		// A deterministic font makes positions and shrink-to-fit independently testable.
		float TextWidth(float Size, const char *pText, int Length, float, int, const STextSizeProperties &) override { return (Length < 0 ? str_length(pText) : Length) * Size / 2; }

		// Overlay rendering must not depend on font loading, containers or other state.
		bool LoadFonts() override
		{
			ADD_FAILURE();
			return false;
		}
		void SetFontPreset(EFontPreset) override { ADD_FAILURE(); }
		void SetFontLanguageVariant(const char *) override { ADD_FAILURE(); }
		void SetRenderFlags(unsigned) override { ADD_FAILURE(); }
		unsigned GetRenderFlags() const override
		{
			ADD_FAILURE();
			return 0;
		}
		void TextEx(CTextCursor *, const char *, int) override { ADD_FAILURE(); }
		bool CreateTextContainer(STextContainerIndex &, CTextCursor *, const char *, int) override
		{
			ADD_FAILURE();
			return false;
		}
		void AppendTextContainer(STextContainerIndex, CTextCursor *, const char *, int) override { ADD_FAILURE(); }
		bool CreateOrAppendTextContainer(STextContainerIndex &, CTextCursor *, const char *, int) override
		{
			ADD_FAILURE();
			return false;
		}
		void RecreateTextContainer(STextContainerIndex &, CTextCursor *, const char *, int) override { ADD_FAILURE(); }
		void RecreateTextContainerSoft(STextContainerIndex &, CTextCursor *, const char *, int) override { ADD_FAILURE(); }
		void DeleteTextContainer(STextContainerIndex &) override { ADD_FAILURE(); }
		void UploadTextContainer(STextContainerIndex) override { ADD_FAILURE(); }
		void RenderTextContainer(STextContainerIndex, const ColorRGBA &, const ColorRGBA &) override { ADD_FAILURE(); }
		void RenderTextContainer(STextContainerIndex, const ColorRGBA &, const ColorRGBA &, float, float) override { ADD_FAILURE(); }
		STextBoundingBox GetBoundingBoxTextContainer(STextContainerIndex) override
		{
			ADD_FAILURE();
			return {};
		}
		void UploadEntityLayerText(const CImageInfo &, int, int, const char *, int, float, float, int) override { ADD_FAILURE(); }
		int AdjustFontSize(const char *, int, int, int) const override
		{
			ADD_FAILURE();
			return 0;
		}
		float GetGlyphOffsetX(int, char) const override
		{
			ADD_FAILURE();
			return 0;
		}
		int CalculateTextWidth(const char *, int, int, int) const override
		{
			ADD_FAILURE();
			return 0;
		}
		void TextOutlineColor(float, float, float, float) override { ADD_FAILURE(); }
		void TextOutlineColor(ColorRGBA) override { ADD_FAILURE(); }
		void TextSelectionColor(float, float, float, float) override { ADD_FAILURE(); }
		void TextSelectionColor(ColorRGBA) override { ADD_FAILURE(); }
		STextBoundingBox TextBoundingBox(float, const char *, int, float, float, int) override
		{
			ADD_FAILURE();
			return {};
		}
		ColorRGBA GetTextOutlineColor() const override
		{
			ADD_FAILURE();
			return {};
		}
		ColorRGBA GetTextSelectionColor() const override
		{
			ADD_FAILURE();
			return {};
		}
		void OnPreWindowResize() override { ADD_FAILURE(); }
		void OnWindowResize() override { ADD_FAILURE(); }
	};

	TEST_F(CEditorHistoryRuntime, SpecialOverlaySubmissionsMatchNativeAcrossChunkRows)
	{
		struct CRestoreConfig
		{
			int m_Size = g_Config.m_ClTextEntitiesSize, m_Density = g_Config.m_GfxTextOverlay;
			~CRestoreConfig()
			{
				g_Config.m_ClTextEntitiesSize = m_Size;
				g_Config.m_GfxTextOverlay = m_Density;
			}
		} RestoreConfig;
		g_Config.m_ClTextEntitiesSize = 100;
		g_Config.m_GfxTextOverlay = 16;
		constexpr int Width = 65, Height = 2;
		constexpr float Alpha = 0.6f;
		const CScreenRect Screen(28 * 32, 0, 8 * 32, 3 * 32);
		const auto Check = [&]<typename TValue, typename TNative, typename F>(TValue, const std::vector<TNative> &vNative, F Render, const std::vector<COverlaySubmissionText::CLabel> &vExpectedLabels, bool Arrows) {
			CEditorTilePlane<TValue> Plane(Width, Height);
			Plane.Assign(std::span<const TNative>(vNative));
			auto pNativeGraphics = std::make_unique<COverlaySubmissionGraphics>();
			auto pSparseGraphics = std::make_unique<COverlaySubmissionGraphics>();
			pNativeGraphics->MapScreen(Screen);
			pSparseGraphics->MapScreen(Screen);
			COverlaySubmissionText NativeText, SparseText;
			CRenderMap NativeRenderer, SparseRenderer;
			NativeRenderer.Init(pNativeGraphics.get(), &NativeText);
			SparseRenderer.Init(pSparseGraphics.get(), &SparseText);
			Render(NativeRenderer, vNative.data());
			Render(SparseRenderer, CRenderTileSource<TNative>(Plane));
			EXPECT_EQ(SparseText.m_vLabels, NativeText.m_vLabels);
			ASSERT_EQ(SparseText.m_vLabels.size(), vExpectedLabels.size());
			for(std::size_t Index = 0; Index < vExpectedLabels.size(); ++Index)
			{
				EXPECT_EQ(SparseText.m_vLabels[Index].m_Text, vExpectedLabels[Index].m_Text);
				EXPECT_EQ(SparseText.m_vLabels[Index].m_aColor, vExpectedLabels[Index].m_aColor);
				for(int Axis = 0; Axis < 3; ++Axis)
					EXPECT_NEAR(SparseText.m_vLabels[Index].m_aGeometry[Axis], vExpectedLabels[Index].m_aGeometry[Axis], 0.00001f);
			}
			EXPECT_EQ(SparseText.GetTextColor(), SparseText.DefaultTextColor());
			EXPECT_EQ(SparseText.GetTextColor(), NativeText.GetTextColor());
			EXPECT_EQ(pSparseGraphics->m_vArrows, pNativeGraphics->m_vArrows);
			EXPECT_EQ(pSparseGraphics->m_State, pNativeGraphics->m_State);
			EXPECT_EQ(pSparseGraphics->m_aStateChanges, pNativeGraphics->m_aStateChanges);
			EXPECT_EQ(pSparseGraphics->GetScreen().m_TopLeft, Screen.m_TopLeft);
			EXPECT_EQ(pSparseGraphics->GetScreen().m_BottomRight, Screen.m_BottomRight);
			EXPECT_FALSE(pSparseGraphics->m_State.m_Drawing);
			if(Arrows)
			{
				const std::array<std::array<float, 3>, 4> aCentersAngles{{{976, 16, -90}, {1008, 16, 45}, {1040, 16, 180}, {1040, 48, 270}}};
				ASSERT_EQ(pSparseGraphics->m_vArrows.size(), aCentersAngles.size());
				for(std::size_t Index = 0; Index < aCentersAngles.size(); ++Index)
				{
					const auto &Arrow = pSparseGraphics->m_vArrows[Index];
					EXPECT_EQ(Arrow.m_aGeometry, (std::array<float, 3>{aCentersAngles[Index][0], aCentersAngles[Index][1], 35}));
					EXPECT_FLOAT_EQ(Arrow.m_State.m_Rotation, aCentersAngles[Index][2] * (pi / 180.0f));
					EXPECT_EQ(Arrow.m_State.m_Sprite, SPRITE_SPEEDUP_ARROW);
					EXPECT_EQ(Arrow.m_State.m_Texture, g_pData->m_aImages[IMAGE_SPEEDUP_ARROW].m_Id.Id());
					EXPECT_EQ(Arrow.m_State.m_aColor, (std::array<float, 4>{1, 1, 1, Alpha}));
					EXPECT_TRUE(Arrow.m_State.m_Drawing);
				}
				auto FinalState = pSparseGraphics->m_vArrows.back().m_State;
				FinalState.m_Drawing = false;
				EXPECT_EQ(pSparseGraphics->m_State, FinalState);
			}
			else
			{
				EXPECT_TRUE(pSparseGraphics->m_vArrows.empty());
				EXPECT_EQ(pSparseGraphics->m_State, COverlaySubmissionGraphics::CState{});
			}
		};
		const auto Label = [&](const char *pText, float X, float Y, float Size) { return COverlaySubmissionText::CLabel{pText, {X, Y, Size}, {1, 1, 1, Alpha}}; };
		constexpr int Flags = OVERLAYRENDERFLAG_TEXT | OVERLAYRENDERFLAG_EDITOR;
		std::vector<CTeleTile> vTele(Width * Height);
		vTele[30] = {7, TILE_TELEIN};
		vTele[31] = {12, TILE_TELEOUT};
		vTele[32] = {123, TILE_TELEIN};
		vTele[Width + 31] = {42, TILE_TELEIN};
		vTele[33] = {99, TILE_TELECHECKIN}; // This type must not emit a number.
		Check(CTeleTileValues{}, vTele, [&](auto &Renderer, auto Source) { Renderer.RenderTeleOverlay(Source, Width, Height, 32, Flags, Alpha); }, {Label("7", 968, 0, 32), Label("12", 992, 0, 32), Label("123", 1024, 16.f / 3, 64.f / 3), Label("42", 992, 32, 32)}, false);
		std::vector<CSpeedupTile> vSpeedup(Width * Height);
		vSpeedup[30] = {7, 0, TILE_SPEED_BOOST_OLD, 0, -90};
		vSpeedup[31] = {12, 34, TILE_SPEED_BOOST, 0, 45};
		vSpeedup[32] = {0, 56, TILE_SPEED_BOOST, 0, 180};
		vSpeedup[Width + 31] = {9, 8, 255, 0, -135};
		vSpeedup[Width + 32] = {0, 0, TILE_SPEED_BOOST, 0, 270};
		Check(CSpeedupTileValues{}, vSpeedup, [&](auto &Renderer, auto Source) { Renderer.RenderSpeedupOverlay(Source, Width, Height, 32, Flags, Alpha); }, {Label("7", 960, 16, 16), Label("12", 992, 16, 16), Label("34", 992, 0, 16), Label("0", 1024, 16, 16), Label("56", 1024, 0, 16), Label("9", 992, 32, 32.f / 3), Label("8", 992, 32 + 32.f / 3, 32.f / 3), Label("-135", 992, 32 + 64.f / 3, 32.f / 3), Label("0", 1024, 48, 16)}, true);
		std::vector<CSwitchTile> vSwitch(Width * Height);
		vSwitch[30] = {7, TILE_SWITCHTIMEDOPEN, 0, 3};
		vSwitch[31] = {12, TILE_SWITCHTIMEDOPEN, 0, 0};
		vSwitch[32] = {123, TILE_SWITCHTIMEDOPEN, 0, 56};
		vSwitch[Width + 31] = {42, TILE_SWITCHTIMEDOPEN, 0, 8};
		Check(CSwitchTileValues{}, vSwitch, [&](auto &Renderer, auto Source) { Renderer.RenderSwitchOverlay(Source, Width, Height, 32, Flags, Alpha); }, {Label("7", 960, 0, 16), Label("3", 960, 16, 16), Label("12", 992, 0, 16), Label("123", 1024, 0, 16), Label("56", 1024, 16, 16), Label("42", 992, 32, 16), Label("8", 992, 48, 16)}, false);
		std::vector<CTuneTile> vTune(Width * Height);
		vTune[30] = {7, TILE_TUNE};
		vTune[31] = {12, TILE_TUNE};
		vTune[32] = {123, TILE_TUNE};
		vTune[Width + 31] = {42, TILE_TUNE};
		Check(CTuneTileValues{}, vTune, [&](auto &Renderer, auto Source) { Renderer.RenderTuneOverlay(Source, Width, Height, 32, Flags, Alpha); }, {Label("7", 972, 8, 16), Label("12", 1000, 8, 16), Label("123", 1028, 8, 16), Label("42", 1000, 40, 16)}, false);
	}

	TEST_F(CEditorHistoryRuntime, TilePassSelectionPreservesStateAndSkipsImpossibleOpaqueRows)
	{
		const std::array<CTile, 2> aNative{{{17, TILEFLAG_XFLIP | TILEFLAG_OPAQUE, 0, 0}, {18, TILEFLAG_YFLIP, 0, 0}}};
		CEditorTilePlane<CTileValues> Plane(2, 1);
		Plane.Assign(std::span<const CTile>(aNative));
		std::array<unsigned char, 256> aOpacity{};
		aOpacity[17] = TILEFLAG_OPAQUE;
		CRenderTileSource<CTile> Source(Plane, aOpacity.data());
		auto pGraphics = std::make_unique<CTileSubmissionGraphics>();
		pGraphics->MapScreen(CScreenRect(0, 0, 64, 32));
		CRenderMap Renderer;
		Renderer.Init(pGraphics.get(), Editor().TextRender());
		constexpr float OpaqueThreshold = 254.0f / 255.0f;
		for(const bool Arrays : {false, true})
			for(const float Alpha : {0.4f, OpaqueThreshold, std::nextafter(OpaqueThreshold, 1.0f), 1.0f})
				for(const int Pass : std::array<int, 4>{0, LAYERRENDERFLAG_OPAQUE, LAYERRENDERFLAG_TRANSPARENT, LAYERRENDERFLAG_OPAQUE | LAYERRENDERFLAG_TRANSPARENT})
				{
					pGraphics->m_TextureArrays = Arrays;
					const auto Color = ColorRGBA(0.2f, 0.4f, 0.6f, Alpha);
					pGraphics->m_vSubmitted.clear();
					Renderer.RenderTilemap(aNative.data(), 2, 1, 32, Color, Pass);
					const auto Native = pGraphics->m_vSubmitted;
					pGraphics->m_vSubmitted.clear();
					pGraphics->m_aStateChanges.fill(0);
					const auto Reads = Renderer.EditorTileReads();
					const auto Spans = Renderer.EditorTileSpans();
					Renderer.RenderTilemap(Source, 2, 1, 32, Color, Pass);
					EXPECT_EQ(pGraphics->m_vSubmitted, Native);
					EXPECT_EQ(pGraphics->m_aStateChanges, (std::array<int, 4>{1, 1, 1, 1}));
					if(Alpha <= OpaqueThreshold && !(Pass & LAYERRENDERFLAG_TRANSPARENT))
					{
						EXPECT_EQ(Renderer.EditorTileReads(), Reads);
						EXPECT_EQ(Renderer.EditorTileSpans(), Spans);
					}
					std::size_t ExpectedCount = 0;
					for(std::size_t Index = 0; Index < aNative.size(); ++Index)
					{
						const bool Opaque = Index == 0 && Alpha > OpaqueThreshold;
						if(!(Pass & (Opaque ? LAYERRENDERFLAG_OPAQUE : LAYERRENDERFLAG_TRANSPARENT)))
							continue;
						ASSERT_LT(ExpectedCount, pGraphics->m_vSubmitted.size());
						const auto &Submitted = pGraphics->m_vSubmitted[ExpectedCount++];
						EXPECT_EQ(Submitted.m_aRect, (std::array<float, 4>{static_cast<float>(Index * 32), 0, 32, 32}));
						EXPECT_EQ(Submitted.m_aColor, (std::array<float, 4>{Color.r, Color.g, Color.b, Alpha}));
						if(Arrays)
						{
							EXPECT_EQ(Submitted.m_TextureIndex, aNative[Index].m_Index);
							const std::array<float, 8> ExpectedUv = Index == 0 ? std::array<float, 8>{1, 0, 0, 0, 0, 1, 1, 1} : std::array<float, 8>{0, 1, 1, 1, 1, 0, 0, 0};
							EXPECT_EQ(Submitted.m_aUv, ExpectedUv);
						}
					}
					EXPECT_EQ(pGraphics->m_vSubmitted.size(), ExpectedCount);
				}
	}

	TEST_F(CEditorHistoryRuntime, FreshLoadedImageRenderingMatchesNativeSubmissions)
	{
		int Owner;
		std::vector<CTile> vNative(65 * 35);
		for(const int Y : {0, 1, 31, 32, 34})
			for(int Orientation = 0; Orientation < 8; ++Orientation)
			{
				const auto Flags = static_cast<unsigned char>((Orientation & 3) | (Orientation & 4 ? TILEFLAG_ROTATE : 0));
				vNative[Y * 65 + 29 + Orientation] = {static_cast<unsigned char>(17 + Y % 2), Flags, 0, 0};
			}
		vNative[0] = {17, TILEFLAG_ROTATE, 0, 0};
		vNative[34 * 65 + 64] = {18, TILEFLAG_XFLIP | TILEFLAG_YFLIP, 0, 0};
		ASSERT_TRUE(Timeline().Edit(&Owner, "Fresh image render fixture", editor_history::ECategory::MAP, [&] {
			auto pImage = std::make_shared<CEditorImage>(&Map());
			str_copy(pImage->m_aName, "render-opacity");
			pImage->m_Width = pImage->m_Height = 16;
			pImage->m_Format = CImageInfo::FORMAT_RGBA;
			std::vector<std::uint8_t> vPixels(16 * 16 * 4, 255);
			vPixels[18 * 4 + 3] = 249;
			pImage->m_Content.emplace(vPixels);
			Map().m_vpImages.push_back(pImage);
			auto pLayer = std::make_shared<CLayerTiles>(&Map(), 65, 35);
			pLayer->m_Image = Map().ImageReference(Map().m_vpImages.size() - 1);
			pLayer->m_Tiles.Assign(std::span<const CTile>(vNative));
			Map().m_pGameGroup->AddLayer(pLayer);
		}));
		CImageInfo Pixels = Map().m_vpImages.back()->ImageCopy();
		ASSERT_TRUE(CImageLoader::SavePng(Editor().Storage()->OpenFile("mapres/render-opacity.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "render-opacity.png", Pixels));
		Pixels.Free();
		auto pGraphics = std::make_unique<CTileSubmissionGraphics>();
		CRenderMap Renderer;
		Renderer.Init(pGraphics.get(), Editor().TextRender());
		for(const int ImageMode : {0, 1, 2})
		{
			if(ImageMode == 1)
			{
				ASSERT_TRUE(Timeline().Edit(&Owner, "External render image", editor_history::ECategory::MAP, [&] { Map().m_vpImages.back()->m_External = 1; }));
			}
			ASSERT_TRUE(Editor().Save("maps/fresh-render.map"));
			DrainSaves();
			if(ImageMode == 2)
			{
				ASSERT_TRUE(Editor().Storage()->RemoveFile("mapres/render-opacity.png", IStorage::TYPE_SAVE));
			}
			CEditorMap Loaded(&Editor());
			int Warnings = 0;
			ASSERT_TRUE(Loaded.Load("maps/fresh-render.map", IStorage::TYPE_SAVE, [&](const char *pError) {
				++Warnings;
				EXPECT_EQ(ImageMode, 2) << pError;
				EXPECT_NE(str_find(pError, "render-opacity"), nullptr);
			}));
			EXPECT_EQ(Warnings, ImageMode == 2 ? 1 : 0);
			auto pLayer = std::static_pointer_cast<CLayerTiles>(Loaded.m_pGameGroup->m_vpLayers.back());
			auto pImage = Loaded.m_vpImages[Loaded.ImageIndex(pLayer->m_Image)];
			EXPECT_EQ(pImage->TileFlagsAnalyses(), 0u);
			auto vExpected = vNative;
			for(auto &Tile : vExpected)
				if(ImageMode != 2 && Tile.m_Index != 18)
					Tile.m_Flags |= TILEFLAG_OPAQUE;
			for(const bool Arrays : {false, true})
				for(const bool Extend : {false, true})
					for(const float Alpha : {1.0f, 0.4f})
						for(const auto &Screen : {CScreenRect(-96, -64, 2304, 1248), CScreenRect(29 * 32, 30 * 32, 9 * 32, 6 * 32), CScreenRect(-256, -256, 64, 64), CScreenRect(2200, 1200, 64, 64)})
						{
							pGraphics->m_TextureArrays = Arrays;
							pGraphics->MapScreen(Screen);
							for(const int Pass : {LAYERRENDERFLAG_OPAQUE, LAYERRENDERFLAG_TRANSPARENT})
							{
								const auto Color = ColorRGBA(0.2f, 0.4f, 0.6f, Alpha);
								const int Flags = Pass | (Extend ? TILERENDERFLAG_EXTEND : 0);
								pGraphics->m_vSubmitted.clear();
								Renderer.RenderTilemap(vExpected.data(), 65, 35, 32, Color, Flags);
								const auto Expected = pGraphics->m_vSubmitted;
								pGraphics->m_vSubmitted.clear();
								Renderer.RenderTilemap(pLayer->TilesForRender(&Loaded), 65, 35, 32, Color, Flags);
								EXPECT_EQ(pGraphics->m_vSubmitted, Expected) << ImageMode << " " << Arrays << " " << Extend << " " << Pass;
							}
						}
			EXPECT_EQ(pImage->TileFlagsAnalyses(), 1u);
		}
	}

	TEST_F(CEditorHistoryRuntime, ImageOpacityCacheFollowsOwnershipGeometryAndFree)
	{
		CEditorImage Image(&Map());
		EXPECT_EQ(Image.TileFlags(), (std::array<unsigned char, 256>{}));
		std::vector<std::uint8_t> vPixels(32 * 32 * 4, 255);
		vPixels[3] = 249;
		Image.m_Content.emplace(vPixels);
		Image.m_Width = Image.m_Height = 32;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		EXPECT_EQ(Image.TileFlags()[0], 0);
		EXPECT_EQ(Image.TileFlags()[1], TILEFLAG_OPAQUE);
		const auto Scans = Image.TileFlagsAnalyses();
		const auto Same = Image.m_Content;
		Image.m_Content = Same;
		Image.m_External = 1;
		str_copy(Image.m_aName, "renamed");
		for(int Repeat = 0; Repeat < 100; ++Repeat)
			EXPECT_EQ(Image.TileFlags()[1], TILEFLAG_OPAQUE);
		EXPECT_EQ(Image.TileFlagsAnalyses(), Scans);
		Image.m_Width = 16;
		EXPECT_EQ(Image.TileFlags()[1], 0);
		Image.m_Height = 16;
		EXPECT_EQ(Image.TileFlags()[1], TILEFLAG_OPAQUE);
		Image.m_Format = CImageInfo::FORMAT_RGB;
		EXPECT_EQ(Image.TileFlags()[1], 0);
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		EXPECT_EQ(Image.TileFlags()[1], TILEFLAG_OPAQUE);
		// Recycled raw allocations must never reuse an obsolete owner identity.
		for(int Repeat = 0; Repeat < 100; ++Repeat)
		{
			Image.m_Content.reset();
			vPixels[1 * 4 + 3] = Repeat % 2 ? 255 : 0;
			Image.m_Content.emplace(vPixels);
			EXPECT_EQ(Image.TileFlags()[1], Repeat % 2 ? TILEFLAG_OPAQUE : 0);
		}
		CImageInfo Replacement;
		Replacement.m_Width = Replacement.m_Height = 16;
		Replacement.m_Format = CImageInfo::FORMAT_RGBA;
		Replacement.Allocate();
		std::fill_n(Replacement.m_pData, Replacement.DataSize(), 255);
		Image = std::move(Replacement); // Same path as add/replace/tile art.
		EXPECT_EQ(Image.TileFlags()[0], TILEFLAG_OPAQUE);
		// Retained pixels with malformed geometry must not imply opacity or be read.
		for(const auto &[Width, Height] : {std::pair{0, 16}, std::pair{16, 0}, std::pair{0, 0}, std::pair{15, 15}})
		{
			Image.m_Width = Width;
			Image.m_Height = Height;
			EXPECT_EQ(Image.TileFlags(), (std::array<unsigned char, 256>{}));
		}
		Image.m_Width = Image.m_Height = 16;
		EXPECT_EQ(Image.TileFlags()[0], TILEFLAG_OPAQUE);
		Image.m_Content.emplace(std::vector<std::uint8_t>(16 * 16 * 4 - 1, 255));
		EXPECT_EQ(Image.TileFlags(), (std::array<unsigned char, 256>{}));
		Image.Free();
		EXPECT_EQ(Image.TileFlags(), (std::array<unsigned char, 256>{}));
		const auto EmptyScans = Image.TileFlagsAnalyses();
		Image.TileFlags();
		EXPECT_EQ(Image.TileFlagsAnalyses(), EmptyScans);
	}

	template<typename TValue>
	struct CCheckedRenderPlane
	{
		const CEditorTilePlane<TValue> &m_Plane;
		mutable std::size_t m_Reads = 0;
		int Width() const { return m_Plane.Width(); }
		auto ReadRowCached(std::size_t X, std::size_t Y, std::size_t EndX, CTileChunkCursor &Cursor) const
		{
			++m_Reads;
			EXPECT_GE(Y, 29999u);
			EXPECT_LT(Y, 30003u);
			return m_Plane.ReadRowCached(X, Y, EndX, Cursor);
		}
		auto ReadRow(std::size_t X, std::size_t Y, std::size_t EndX) const
		{
			++m_Reads;
			EXPECT_GE(Y, 29999u);
			EXPECT_LT(Y, 30003u);
			return m_Plane.ReadRow(X, Y, EndX);
		}
	};

	TEST_F(CEditorHistoryRuntime, BorrowedRenderSourcesAddressRowsBeyondSignedIndexRange)
	{
		const auto Screen = Editor().Graphics()->GetScreen();
		Editor().Graphics()->MapScreen(CScreenRect(0, 30000 * 32.f, 128, 64));
		CLayerTiles Layer(&Map(), 100000, 100000);
		Layer.SetTile(2, 30000, CTile{17, TILEFLAG_ROTATE, 0, 0});
		EXPECT_EQ(Layer.GetTile(2, 30000).m_Index, 17);
		EXPECT_EQ(Layer.GetTile(2, 30000).m_Flags, TILEFLAG_ROTATE);
		CCheckedRenderPlane<CTileValues> Tiles{Layer.m_Tiles};
		Editor().Graphics()->TextureClear();
		Editor().RenderMap()->RenderTilemap(CRenderTileSource<CTile>(Tiles), 100000, 100000, 32.f, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_TRANSPARENT);
		EXPECT_GT(Tiles.m_Reads, 0u);
		CEditorTilePlane<CTeleTileValues> Tele(100000, 100000);
		CEditorTilePlane<CSpeedupTileValues> Speedup(100000, 100000);
		CEditorTilePlane<CSwitchTileValues> Switch(100000, 100000);
		CEditorTilePlane<CTuneTileValues> Tune(100000, 100000);
		constexpr std::size_t Cell = 30000ull * 100000 + 2;
		Tele.Set(Cell, CTeleTile{7, TILE_TELEIN});
		Speedup.Set(Cell, CSpeedupTile{8, 90, TILE_SPEED_BOOST, 0, 135});
		Switch.Set(Cell, CSwitchTile{9, TILE_SWITCHOPEN, TILEFLAG_ROTATE, 17});
		Tune.Set(Cell, CTuneTile{10, TILE_TUNE});
		CCheckedRenderPlane<CTeleTileValues> TeleSource{Tele};
		CCheckedRenderPlane<CSpeedupTileValues> SpeedupSource{Speedup};
		CCheckedRenderPlane<CSwitchTileValues> SwitchSource{Switch};
		CCheckedRenderPlane<CTuneTileValues> TuneSource{Tune};
		const int Flags = OVERLAYRENDERFLAG_TEXT | OVERLAYRENDERFLAG_EDITOR;
		Editor().RenderMap()->RenderTeleOverlay(CRenderTileSource<CTeleTile>(TeleSource), 100000, 100000, 32.f, Flags);
		Editor().RenderMap()->RenderSpeedupOverlay(CRenderTileSource<CSpeedupTile>(SpeedupSource), 100000, 100000, 32.f, Flags);
		Editor().RenderMap()->RenderSwitchOverlay(CRenderTileSource<CSwitchTile>(SwitchSource), 100000, 100000, 32.f, Flags);
		Editor().RenderMap()->RenderTuneOverlay(CRenderTileSource<CTuneTile>(TuneSource), 100000, 100000, 32.f, Flags);
		EXPECT_GT(TeleSource.m_Reads, 0u);
		EXPECT_GT(SpeedupSource.m_Reads, 0u);
		EXPECT_GT(SwitchSource.m_Reads, 0u);
		EXPECT_GT(TuneSource.m_Reads, 0u);
		Editor().Graphics()->Swap();
		Editor().Graphics()->WaitForIdle();
		Editor().Graphics()->MapScreen(Screen);
	}

	TEST_F(CEditorHistoryRuntime, BorrowedTileSourceTracksValuesOpacityAndTraversalAcrossChunks)
	{
		int Owner;
		ASSERT_TRUE(Timeline().Edit(&Owner, "Render source fixture", editor_history::ECategory::MAP, [&] {
			auto pImage = std::make_shared<CEditorImage>(&Map());
			str_copy(pImage->m_aName, "render-source");
			pImage->m_Width = pImage->m_Height = 16;
			pImage->m_Format = CImageInfo::FORMAT_RGBA;
			pImage->m_Content.emplace(std::vector<std::uint8_t>(16 * 16 * 4, 255));
			Map().m_vpImages.push_back(pImage);
			auto pLayer = std::make_shared<CLayerTiles>(&Map(), 65, 33);
			pLayer->m_Image = Map().ImageReference(Map().m_vpImages.size() - 1);
			pLayer->m_Tiles.Set(31, CTile{17, TILEFLAG_XFLIP, 0, 0});
			pLayer->m_Tiles.Set(32, CTile{17, TILEFLAG_YFLIP, 0, 0});
			pLayer->m_Tiles.Set(32 * 65 + 64, CTile{17, TILEFLAG_ROTATE, 0, 0});
			Map().m_pGameGroup->AddLayer(pLayer);
		}));
		ASSERT_TRUE(Timeline().Clear());
		auto pGraphics = std::make_unique<CTileSubmissionGraphics>();
		pGraphics->m_TextureArrays = true;
		pGraphics->MapScreen(CScreenRect(0, 0, 65 * 32, 33 * 32));
		CRenderMap Renderer;
		Renderer.Init(pGraphics.get(), Editor().TextRender());
		const auto AssertSource = [&](bool Changed) {
			auto pLayer = std::static_pointer_cast<CLayerTiles>(Map().m_pGameGroup->m_vpLayers.back());
			auto Source = pLayer->TilesForRender(&Map());
			for(const std::size_t Cell : {31u, 32u, 32u * 65u + 64u, 0u, 32u, 31u})
			{
				const auto Tile = Source[Cell];
				const int Index = Cell == 0 ? 0 : Changed && Cell == 31 ? 18 :
											  17;
				const int Flags = Cell == 0 ? 0 : Cell == 31 ? Changed ? TILEFLAG_ROTATE : TILEFLAG_XFLIP :
							  Cell == 32         ? TILEFLAG_YFLIP :
									       TILEFLAG_ROTATE;
				EXPECT_EQ(Tile.m_Index, Index);
				EXPECT_EQ(Tile.m_Flags, Flags | (!Changed || Index != 17 ? TILEFLAG_OPAQUE : 0));
				EXPECT_EQ(Tile.m_Skip, 0);
				EXPECT_EQ(Tile.m_MustBe0, 0);
				EXPECT_EQ(pLayer->m_Tiles[Cell].m_Flags, Flags);
			}
			std::vector<CTile> vNative(65 * 33);
			vNative[31] = {static_cast<unsigned char>(Changed ? 18 : 17), static_cast<unsigned char>((Changed ? TILEFLAG_ROTATE : TILEFLAG_XFLIP) | TILEFLAG_OPAQUE), 0, 0};
			vNative[32] = {17, static_cast<unsigned char>(TILEFLAG_YFLIP | (Changed ? 0 : TILEFLAG_OPAQUE)), 0, 0};
			vNative[32 * 65 + 64] = {17, static_cast<unsigned char>(TILEFLAG_ROTATE | (Changed ? 0 : TILEFLAG_OPAQUE)), 0, 0};
			for(const int Flags : {LAYERRENDERFLAG_OPAQUE, LAYERRENDERFLAG_TRANSPARENT})
			{
				pGraphics->m_vSubmitted.clear();
				Renderer.RenderTilemap(vNative.data(), 65, 33, 32, ColorRGBA(1, 1, 1, 1), Flags);
				const auto Expected = pGraphics->m_vSubmitted;
				pGraphics->m_vSubmitted.clear();
				Renderer.RenderTilemap(pLayer->TilesForRender(&Map()), 65, 33, 32, ColorRGBA(1, 1, 1, 1), Flags);
				EXPECT_EQ(pGraphics->m_vSubmitted, Expected);
			}
		};
		AssertSource(false);
		ASSERT_TRUE(Timeline().Edit(&Owner, "Change render inputs", editor_history::ECategory::MAP, [&] {
			auto &Image = *Map().m_vpImages.back();
			std::vector<std::uint8_t> vPixels(16 * 16 * 4, 255);
			vPixels[17 * 4 + 3] = 0;
			Image.m_Content.emplace(vPixels);
			std::static_pointer_cast<CLayerTiles>(Map().m_pGameGroup->m_vpLayers.back())->m_Tiles.Set(31, CTile{18, TILEFLAG_ROTATE, 0, 0});
		}));
		AssertSource(true);
		ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
		AssertSource(false);
		ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
		AssertSource(true);
		ASSERT_TRUE(Timeline().Begin(&Owner, "Cancel visible render inputs", editor_history::ECategory::MAP));
		Map().m_vpImages.back()->m_Content.emplace(std::vector<std::uint8_t>(16 * 16 * 4, 255));
		std::static_pointer_cast<CLayerTiles>(Map().m_pGameGroup->m_vpLayers.back())->m_Tiles.Set(31, CTile{17, TILEFLAG_XFLIP, 0, 0});
		AssertSource(false); // Draft input must be visible before publication.
		ASSERT_TRUE(Timeline().Cancel(CEditorDocumentHistory::ECancellation::ESCAPE));
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		AssertSource(true);
	}

	TEST_F(CEditorHistoryRuntime, MassiveSparseViewportAndTraversalAvoidDenseRenderStorage)
	{
		const auto Screen = Editor().Graphics()->GetScreen();
		editor_history::CStorageUsage BaselineCaches;
		Map().AccountRuntimeCaches(BaselineCaches);
		for(const int Edge : {256, 4096, 32768})
		{
			int Owner;
			std::uint64_t LayerId = 0;
			ASSERT_TRUE(Timeline().Edit(&Owner, "Massive sparse layer", editor_history::ECategory::MAP, [&] {
				auto pLayer = std::make_shared<CLayerTiles>(&Map(), Edge, Edge);
				LayerId = pLayer->m_Id;
				pLayer->SetTile(1, 1, CTile{1, 0, 0, 0});
				Map().m_pGameGroup->AddLayer(pLayer);
			}));
			const auto Render = [&] {
				Editor().Graphics()->MapScreen(CScreenRect(0, 0, 640, 480));
				const auto Reads = Editor().RenderMap()->EditorTileReads();
				for(const auto &pLayer : Map().m_pGameGroup->m_vpLayers)
					if(pLayer->m_Id == LayerId)
						pLayer->Render(&Map());
				EXPECT_GT(Editor().RenderMap()->EditorTileReads() - Reads, 0u);
				EXPECT_LE(Editor().RenderMap()->EditorTileReads() - Reads, 2u * 22u * 17u);
				Editor().Graphics()->Swap();
				Editor().Graphics()->WaitForIdle();
			};
			Render();
			ASSERT_TRUE(Timeline().Edit(&Owner, "Visible sparse stroke", editor_history::ECategory::MAP, [&] {
				for(const auto &pLayer : Map().m_pGameGroup->m_vpLayers)
					if(pLayer->m_Id == LayerId)
						std::static_pointer_cast<CLayerTiles>(pLayer)->SetTile(2, 2, CTile{2, 0, 0, 0});
			}));
			Render();
			ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
			Render();
			ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
			Render();
			Editor().OnRender();
			Editor().Graphics()->Swap();
			Editor().Graphics()->WaitForIdle();
			editor_history::CStorageUsage Caches;
			Map().AccountRuntimeCaches(Caches);
			EXPECT_LE(Caches.Bytes(), BaselineCaches.Bytes() + 64u * 1024u);
		}
		Editor().Graphics()->MapScreen(Screen);
	}

	TEST_F(CEditorHistoryRuntime, ExplicitZoomsKeepSparseRenderWorkBoundedWhilePanning)
	{
		const auto Screen = Editor().Graphics()->GetScreen();
		const auto Zoom = *Editor().MapView()->Zoom();
		const auto WorldOffset = Editor().MapView()->GetWorldOffset();
		const auto EditorOffset = Editor().MapView()->GetEditorOffset();
		Editor().MapView()->SetEditorOffset(vec2(0, 0));
		for(const int Edge : {1000, 10000, 100000})
		{
			CLayerTiles Layer(&Map(), Edge, Edge);
			Layer.SetTile(1, 1, CTile{17, TILEFLAG_ROTATE, 0, 0});
			for(const float Level : {200.0f, 900.0f, 2000.0f})
			{
				Editor().MapView()->Zoom()->SetValueInstant(Level);
				Editor().MapView()->UpdateZoom();
				EXPECT_FLOAT_EQ(Editor().MapView()->GetWorldZoom(), Level / 100.0f);
				for(const bool Pan : {false, true})
				{
					Editor().MapView()->SetWorldOffset(Pan ? vec2(800 * 32, 800 * 32) : vec2(0, 0));
					const auto View = Map().m_pGameGroup->Mapping();
					Editor().Graphics()->MapScreen(View);
					const auto Reads = Editor().RenderMap()->EditorTileReads();
					const auto Spans = Editor().RenderMap()->EditorTileSpans();
					Layer.Render(&Map());
					const auto ReadCells = Editor().RenderMap()->EditorTileReads() - Reads;
					EXPECT_LE(ReadCells, 2u * 32u * 32u);
					if(Pan)
						EXPECT_EQ(ReadCells, 0u);
					else
						EXPECT_GT(ReadCells, 0u);
					const auto VisibleRows = static_cast<std::size_t>(View.Height() / 32 + 3);
					EXPECT_LE(Editor().RenderMap()->EditorTileSpans() - Spans, 2u * VisibleRows * (16u * Layer.m_Tiles.Plane().TreeDepth() + 2u));
					Editor().Graphics()->Swap();
					Editor().Graphics()->WaitForIdle();
				}
			}
		}
		*Editor().MapView()->Zoom() = Zoom;
		Editor().MapView()->UpdateZoom();
		Editor().MapView()->SetWorldOffset(WorldOffset);
		Editor().MapView()->SetEditorOffset(EditorOffset);
		Editor().Graphics()->MapScreen(Screen);
	}

	class CTestEditor final : public CEditor
	{
		bool m_Ran = false;

	public:
		void Init() override
		{
			gs_pKernel = Kernel();
			gs_ControlledEngine.m_pReal = gs_pKernel->RequestInterface<IEngine>();
			std::unique_ptr<IKernel> pInitializationKernel(IKernel::Create());
			const auto Register = [&]<typename T>() {
				auto *pInterface = gs_pKernel->RequestInterface<T>();
				pInitializationKernel->RegisterInterface<T>(pInterface, false);
				gs_pKernel->ReregisterInterface<T>(pInterface);
			};
			Register.operator()<IInput>();
			Register.operator()<IClient>();
			Register.operator()<IConfigManager>();
			Register.operator()<IGraphics>();
			Register.operator()<ITextRender>();
			Register.operator()<IStorage>();
			Register.operator()<ISound>();
			pInitializationKernel->RegisterInterface<IEngine>(&gs_ControlledEngine, false);
			pInitializationKernel->RegisterInterface<IEditor>(this, false);
			CEditor::Init();
			gs_pKernel->ReregisterInterface<IEditor>(this);
			gs_pKernel->ReregisterInterface<IEngine>(&gs_ControlledEngine);
			gs_ReleaseProbe.OnInit(this);
			MapView()->RegisterSubComponent(gs_ReleaseProbe);
		}
		void OnUpdate() override
		{
			CEditor::OnUpdate();
			if(m_Ran || str_comp(Map()->m_aFilename, "maps/fixture.map") != 0)
				return;
			m_Ran = true;
			CloseMap(0, false); // discard the startup template, keep the loaded fixture
			dbg_assert(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "headless SDL input initialization failed");
			gs_pEditor = this;
			std::string Error;
			gs_Initial = Map()->CaptureDocument(nullptr, Error);
			dbg_assert(gs_Initial.has_value(), "fixture capture failed");
			int Argc = 1;
			char aName[] = "editor-testrunner";
			char *apArgs[] = {aName, nullptr};
			::testing::InitGoogleTest(&Argc, apArgs);
			const int Result = RUN_ALL_TESTS();
			log_info("editor-history-tests", "%s", Result == 0 && ::testing::UnitTest::GetInstance()->test_to_run_count() > 0 ? "PASS" : "FAIL");
			gs_Initial.reset();
			gs_pEditor = nullptr;
			Client()->Quit();
		}
	};
} // namespace

IEditor *CreateEditor() { return new CTestEditor; }
