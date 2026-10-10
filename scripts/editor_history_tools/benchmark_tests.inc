// Inserted into the permanent runtime harness in an isolated optimized copy.
// No timing checks are registered in ordinary correctness runs.
using CBenchClock = std::chrono::steady_clock;
struct CBenchSamples
{
	std::string m_Name;
	std::vector<double> m_Times, m_Allocations, m_Transient;
	bool m_TrackAllocations;
	explicit CBenchSamples(const char *pName, std::size_t Capacity = 64, bool TrackAllocations = true) :
		m_Name(pName), m_TrackAllocations(TrackAllocations)
	{
		m_Times.reserve(Capacity);
		m_Allocations.reserve(Capacity);
		m_Transient.reserve(Capacity);
	}
	template<typename F>
	void Run(bool Record, F &&Function)
	{
		const auto Allocations = benchmark_allocations::g_Count.load();
		const auto Live = benchmark_allocations::g_Live.load();
		if(m_TrackAllocations)
			benchmark_allocations::g_Peak.store(Live);
		const auto Start = CBenchClock::now();
		Function();
		const auto Time = std::chrono::duration<double, std::milli>(CBenchClock::now() - Start).count();
		const auto Peak = benchmark_allocations::g_Peak.load();
		if(Record)
		{
			m_Times.push_back(Time);
			m_Allocations.push_back(static_cast<double>(benchmark_allocations::g_Count.load() - Allocations));
			m_Transient.push_back(static_cast<double>(Peak > Live ? Peak - Live : 0));
		}
	}
	void Report()
	{
		for(auto *pValues : {&m_Times, &m_Allocations, &m_Transient})
			std::sort(pValues->begin(), pValues->end());
		const auto Size = m_Times.size();
		ASSERT_GT(Size, 0u);
		const auto P95 = (Size * 95 + 99) / 100 - 1;
		if(!m_TrackAllocations)
		{
			std::printf("BENCH phase=%s samples=%zu p50_ms=%.6f p95_ms=%.6f max_ms=%.6f\n", m_Name.c_str(), Size, m_Times[Size / 2], m_Times[P95], m_Times.back());
			return;
		}
		std::printf("BENCH phase=%s samples=%zu p50_ms=%.6f p95_ms=%.6f max_ms=%.6f alloc_p50=%.0f alloc_p95=%.0f alloc_max=%.0f transient_cpp_p95=%.0f transient_cpp_max=%.0f\n",
			m_Name.c_str(), Size, m_Times[Size / 2], m_Times[P95], m_Times.back(),
			m_Allocations[Size / 2], m_Allocations[P95], m_Allocations.back(), m_Transient[P95], m_Transient.back());
	}
};

class CEditorHistoryBenchmark : public CEditorHistoryRuntime
{
protected:
	int m_Owner = 0;
	void RenderFrame()
	{
		Editor().OnRender();
		Editor().Graphics()->Swap();
		Editor().Graphics()->WaitForIdle();
	}
	void Limits()
	{
		g_Config.m_ClEditorMaxHistory = 50;
		g_Config.m_ClEditorHistoryMemory = 256;
		Timeline().ApplyPreferences();
	}
	void Memory(const char *pPhase)
	{
		auto Diagnostics = Timeline().MemoryUsage();
		const auto Deadline = CBenchClock::now() + std::chrono::seconds(10);
		while(!Diagnostics.m_Current && CBenchClock::now() < Deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			Diagnostics = Timeline().MemoryUsage();
		}
		ASSERT_TRUE(Diagnostics.m_Current);
		editor_history::CStorageUsage Retained, Total;
		Timeline().History()->AccountRetainedPayload(Retained);
		Timeline().History()->AccountRetainedPayload(Total);
		Map().AccountLiveStorage(Total);
		Timeline().History()->AccountDraft(Total);
		for(const auto &pJob : Editor().m_WriterFinishJobs)
			if(Map().OwnsSave(pJob->Ticket()) && Total.Add(pJob->Ticket().m_pRoot.get(), sizeof(CEditorDocumentValues)))
				pJob->Ticket().m_pRoot->Account(Total);
		Map().AccountRuntimeCaches(Total);
		std::printf("MEMORY phase=%s revisions=%zu retained_bytes=%zu payload_bytes=%zu payload_allocations=%zu live_bytes=%zu draft_extra_bytes=%zu save_pinned_bytes=%zu runtime_cache_bytes=%zu total_document_union=%zu total_union_allocations=%zu cpp_live=%zu oversized=%d\n",
			pPhase, Timeline().History()->Revisions().size(), Timeline().History()->RetainedBytes(), Retained.Bytes(), Retained.Allocations(),
			Diagnostics.m_LiveBytes, Diagnostics.m_DraftExtraBytes, Diagnostics.m_SavePinnedBytes, Diagnostics.m_CacheBytes,
			Total.Bytes(), Total.Allocations(), benchmark_allocations::g_Live.load(), Timeline().History()->Oversized());
	}
	void AssertPublished(const std::shared_ptr<const CEditorDocumentValues> &Expected)
	{
		EXPECT_TRUE(Snapshot() == *Expected);
	}
};

class CEditorHistoryMassiveBenchmark : public CEditorHistoryBenchmark
{
	void TearDown() override
	{
		// The massive protocol releases the fixture-only baseline pin. Restore a
		// fixture for any subsequently selected tests, outside measured regions.
		if(!gs_Initial)
			gs_Initial = Snapshot();
		CEditorHistoryBenchmark::TearDown();
	}
};

TEST_F(CEditorHistoryMassiveBenchmark, SparseRevisionsAndRenderedFrames)
{
	gs_Initial.reset();
	Limits();
	g_Config.m_ClEditorMaxHistory = 49; // Baseline plus 49 undo entries.
	Timeline().ApplyPreferences();
	std::uint64_t OrdinaryId = 0;
	std::size_t Largest = 0, Metadata = Map().m_vpGroups.size() + Map().m_vpImages.size() + Map().m_vpSounds.size() + Map().m_vpEnvelopes.size();
	for(const auto &pGroup : Map().m_vpGroups)
		for(const auto &pLayer : pGroup->m_vpLayers)
		{
			++Metadata;
			if(pLayer->m_Type != LAYERTYPE_TILES || pLayer->IsEntitiesLayer())
				continue;
			const auto pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
			if(pTiles->m_Tiles.Size() > Largest)
			{
				Largest = pTiles->m_Tiles.Size();
				OrdinaryId = pTiles->m_Id;
			}
		}
	const auto GameId = Map().m_pGameLayer->m_Id;
	std::vector<std::uint64_t> vLayers{GameId};
	if(OrdinaryId)
		vLayers.push_back(OrdinaryId);
	else
		std::puts("FIXTURE massive kind=ordinary status=absent");
	for(const auto Id : vLayers)
		for(const bool HistoryPanel : {false, true})
		{
			Editor().m_ActiveExtraEditor = HistoryPanel ? CEditor::EXTRAEDITOR_HISTORY : CEditor::EXTRAEDITOR_NONE;
			ASSERT_TRUE(Timeline().Clear());
			const auto FindLayer = [&]() -> std::shared_ptr<CLayerTiles> {
				for(std::size_t G = 0; G < Map().m_vpGroups.size(); ++G)
					for(std::size_t L = 0; L < Map().m_vpGroups[G]->m_vpLayers.size(); ++L)
						if(Map().m_vpGroups[G]->m_vpLayers[L]->m_Id == Id)
						{
							Map().SelectLayer(L, G);
							return std::static_pointer_cast<CLayerTiles>(Map().m_vpGroups[G]->m_vpLayers[L]);
						}
				return nullptr;
			};
			ASSERT_NE(FindLayer(), nullptr);
			const auto Write = [&](int Sample) {
				auto pLayer = FindLayer();
				const auto Cell = (static_cast<std::size_t>(Sample) * 67) % pLayer->m_Tiles.Size();
				pLayer->m_Tiles.Update(Cell, [](auto &Tile) { Tile.m_Index = Tile.m_Index == 1 ? 3 : 1; });
			};
			for(int Sample = 0; Sample < 49; ++Sample)
				ASSERT_TRUE(Timeline().Edit(&m_Owner, "Prefill massive history", editor_history::ECategory::MAP, [&] { Write(Sample); }));
			ASSERT_EQ(Timeline().History()->Revisions().size(), 50u);
			CEditorDocumentFingerprint Fingerprint;
			EXPECT_EQ(Fingerprint.Key(Timeline().History()->Current()), Timeline().History()->Revisions().back().m_PersistedKey);
			const auto BeforeReads = Editor().RenderMap()->EditorTileReads();
			RenderFrame();
			const auto FrameReads = Editor().RenderMap()->EditorTileReads() - BeforeReads;
			ASSERT_GT(FrameReads, 0u);
			editor_history::CStorageUsage BaselineCaches;
			Map().AccountRuntimeCaches(BaselineCaches);
			const std::string Kind = std::string(Id == GameId ? "game" : "ordinary") + (HistoryPanel ? "_history_panel" : "_default");
			CBenchSamples Begin(("massive_" + Kind + "_begin").c_str(), 64, false);
			CBenchSamples WriteSamples(("massive_" + Kind + "_write").c_str(), 64, false);
			CBenchSamples Commit(("massive_" + Kind + "_commit").c_str(), 64, false);
			CBenchSamples Edit(("massive_" + Kind + "_edit_and_frame").c_str());
			CBenchSamples Undo(("massive_" + Kind + "_undo_and_frame").c_str());
			CBenchSamples Redo(("massive_" + Kind + "_redo_and_frame").c_str());
			std::size_t MaxAccounting = 0, MaxLeaves = 0, MaxBranches = 0, MaxReads = 0;
			for(int Sample = 0; Sample < 55; ++Sample)
			{
				const bool Record = Sample >= 5;
				const auto Reads = Editor().RenderMap()->EditorTileReads();
				ASSERT_EQ(Timeline().History()->Revisions().size(), 50u);
				Edit.Run(Record, [&] {
					Begin.Run(Record, [&] { ASSERT_TRUE(Timeline().Begin(&m_Owner, "Massive sparse stroke", editor_history::ECategory::MAP)); });
					WriteSamples.Run(Record, [&] { ASSERT_TRUE(Timeline().Update(&m_Owner, [&] { Write(Sample + 49); })); });
					Commit.Run(Record, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
					RenderFrame();
				});
				ASSERT_EQ(Timeline().History()->Revisions().size(), 50u);
				MaxReads = std::max(MaxReads, Editor().RenderMap()->EditorTileReads() - Reads);
				MaxAccounting = std::max(MaxAccounting, Timeline().History()->AccountingVisits());
				const auto Key = Timeline().History()->Revisions().back().m_PersistedKey;
				EXPECT_EQ(Fingerprint.Key(Timeline().History()->Current()), Key);
				MaxLeaves = std::max(MaxLeaves, Fingerprint.Statistics().m_TileLeafHashes);
				MaxBranches = std::max(MaxBranches, Fingerprint.Statistics().m_TileBranchHashes);
				EXPECT_EQ(Fingerprint.Statistics().m_TileLeafHashes, 1u);
				EXPECT_LE(Fingerprint.Statistics().m_TileBranchHashes, FindLayer()->m_Tiles.Plane().TreeDepth());
				Undo.Run(Record, [&] { ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint()); RenderFrame(); });
				ASSERT_EQ(Timeline().History()->Revisions().size(), 50u);
				Redo.Run(Record, [&] { ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint()); RenderFrame(); });
				ASSERT_EQ(Timeline().History()->Revisions().size(), 50u);
				EXPECT_EQ(Timeline().History()->Revisions()[Timeline().History()->Cursor()].m_PersistedKey, Key);
			}
			EXPECT_EQ(Timeline().History()->Revisions().size(), 50u);
			EXPECT_LE(MaxAccounting, 128 * (Metadata + 1));
			EXPECT_LE(MaxReads, FrameReads + 1024);
			editor_history::CStorageUsage Retained, Caches;
			Timeline().History()->AccountRetainedPayload(Retained);
			EXPECT_EQ(Retained.Bytes(), Timeline().History()->RetainedPayloadBytes());
			Map().AccountRuntimeCaches(Caches);
			EXPECT_LE(Caches.Bytes(), BaselineCaches.Bytes() + 1024 * 1024);
			editor_history::CStorageUsage Diagnostic;
			Timeline().History()->Current().Account(Diagnostic);
			const auto DocumentBytes = Diagnostic.Bytes();
			Fingerprint.Account(Diagnostic);
			std::printf("FIXTURE massive kind=%s revisions=%zu accounting_visits_max=%zu leaf_hashes_max=%zu branch_hashes_max=%zu viewport_reads_max=%zu baseline_viewport_reads=%zu cache_bytes=%zu diagnostic_fingerprint_extra=%zu\n", Kind.c_str(), Timeline().History()->Revisions().size(), MaxAccounting, MaxLeaves, MaxBranches, MaxReads, FrameReads, Caches.Bytes(), Diagnostic.Bytes() - DocumentBytes);
			Begin.Report();
			WriteSamples.Report();
			Commit.Report();
			Edit.Report();
			Undo.Report();
			Redo.Report();
			Memory(("massive_" + Kind + "_retained").c_str());
			benchmark_allocations::PrintMemory(("massive_" + Kind + "_retained").c_str());
		}
}

TEST_F(CEditorHistoryBenchmark, NativeBaselineExportParity)
{
	const auto Before = Snapshot();
	ASSERT_TRUE(Map().SaveWithKind("maps/normalized-baseline.map", editor_history::ESaveKind::COPY, [](const char *pError) { ADD_FAILURE() << pError; }));
	DrainSaves();
	EXPECT_TRUE(Snapshot() == Before);
}

TEST_F(CEditorHistoryBenchmark, ColdAndSparseProtocol)
{
	Limits();
	CBenchSamples Cold("cold_fingerprint");
	const auto Initial = Timeline().History()->PinCurrent();
	const auto InitialKey = Timeline().History()->Revisions().back().m_PersistedKey;
	for(int Sample = 0; Sample < 10; ++Sample)
		Cold.Run(true, [&] {
			CEditorDocumentFingerprint Cache;
			EXPECT_EQ(Cache.Key(*Initial), InitialKey);
		});
	Cold.Report();
	RenderFrame();
	Memory("baseline_rendered");
	for(const int Updates : {20, 200, 1000})
	{
		ASSERT_TRUE(Timeline().Clear());
		CBenchSamples Begin(("begin_" + std::to_string(Updates)).c_str());
		CBenchSamples Update(("update_" + std::to_string(Updates)).c_str(), 50u * Updates);
		CBenchSamples Commit(("sparse_commit_" + std::to_string(Updates)).c_str());
		CBenchSamples Restore(("restore_rendered_frame_" + std::to_string(Updates)).c_str());
		CBenchSamples Cancel(("cancel_rendered_frame_" + std::to_string(Updates)).c_str());
		for(int Sample = 0; Sample < 55; ++Sample)
		{
			const bool Record = Sample >= 5;
			const auto Before = Timeline().History()->PinCurrent();
			const auto Captures = benchmark_allocations::g_Captures.load();
			Begin.Run(Record, [&] { ASSERT_TRUE(Timeline().Begin(&m_Owner, "Measured sparse gesture", editor_history::ECategory::MAP)); });
			for(int Event = 0; Event < Updates; ++Event)
				Update.Run(Record, [&] { ASSERT_TRUE(Timeline().Update(&m_Owner, [&] {
								 const auto Size = Map().m_pGameLayer->m_Tiles.Size();
								 const auto Index = (Sample * 67u + Event * 31u) % Size;
								 Map().m_pGameLayer->m_Tiles.Update(Index, [](auto &Tile) { Tile.m_Index = Tile.m_Index == 1 ? 3 : 1; });
							 })); });
			if(Sample == 54)
				Memory("active_sparse_draft");
			EXPECT_EQ(benchmark_allocations::g_Captures.load(), Captures);
			Commit.Run(Record, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::POINTER_RELEASE)); });
			EXPECT_EQ(benchmark_allocations::g_Captures.load(), Captures + 1);
			const auto After = Timeline().History()->PinCurrent();
			ASSERT_FALSE(*Before == *After);
			Restore.Run(Record, [&] { ASSERT_TRUE(Timeline().Undo()); ASSERT_TRUE(Timeline().PublishAtSafePoint()); RenderFrame(); });
			AssertPublished(Before);
			ASSERT_TRUE(Timeline().Redo());
			ASSERT_TRUE(Timeline().PublishAtSafePoint());
			RenderFrame();
			AssertPublished(After);
			ASSERT_TRUE(Timeline().Begin(&m_Owner, "Measured cancellation", editor_history::ECategory::MAP));
			ASSERT_TRUE(Timeline().Update(&m_Owner, [&] { Map().m_pGameLayer->m_Tiles.Update(0, [](auto &Tile) { Tile.m_Index = Tile.m_Index == 1 ? 3 : 1; }); }));
			Cancel.Run(Record, [&] { ASSERT_TRUE(Timeline().Cancel(editor_history::EEditCancellation::ESCAPE)); ASSERT_TRUE(Timeline().PublishAtSafePoint()); RenderFrame(); });
			AssertPublished(After);
		}
		Begin.Report();
		Update.Report();
		Commit.Report();
		Restore.Report();
		Cancel.Report();
		Memory(("sparse_final_" + std::to_string(Updates)).c_str());
		EXPECT_EQ(EditorDocumentPersistedKey(Timeline().History()->Current()), Timeline().History()->Revisions()[Timeline().History()->Cursor()].m_PersistedKey);
	}
}

TEST_F(CEditorHistoryBenchmark, PropertyAndResourcesProtocol)
{
	Limits();
	CBenchSamples Property("property_commit");
	for(int Sample = 0; Sample < 55; ++Sample)
	{
		ASSERT_TRUE(Timeline().Begin(&m_Owner, "Measured property", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Update(&m_Owner, [&] { str_format(Map().m_MapInfo.m_Author.Buffer(), sizeof(Map().m_MapInfo.m_Author.Buffer()), "Benchmark author %d", Sample); }));
		Property.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
	}
	Property.Report();
	Memory("property_final");
	ASSERT_FALSE(Map().m_vpImages.empty());
	ASSERT_TRUE(Timeline().Clear());
	CBenchSamples Prepare("resource_domain_prepare_16MiB");
	CBenchSamples Commit("resource_prepared_commit_16MiB");
	std::vector<std::uint8_t> Bytes(2048u * 2048u * 4u, 255);
	for(int Sample = 0; Sample < 55; ++Sample)
	{
		Bytes[0] = static_cast<std::uint8_t>(Sample);
		std::optional<editor_history::CResourceBlob> Blob;
		Prepare.Run(Sample >= 5, [&] { Blob.emplace(Bytes); });
		ASSERT_TRUE(Timeline().Begin(&m_Owner, "Measured resource replacement", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Update(&m_Owner, [&] {
			auto &Image = *Map().m_vpImages.front();
			Image.m_External = 0;
			Image.m_Width = 2048;
			Image.m_Height = 2048;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_Content = std::move(Blob);
		}));
		Commit.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
	}
	Prepare.Report();
	Commit.Report();
	Memory("resource_final");
	EXPECT_LE(Timeline().History()->RetainedBytes(), 256u * 1024u * 1024u);
	EXPECT_EQ(EditorDocumentPersistedKey(Timeline().History()->Current()), Timeline().History()->Revisions()[Timeline().History()->Cursor()].m_PersistedKey);
	// Real queued writer retains its exact root even after a changed commit and clear.
	ASSERT_TRUE(Map().SaveWithKind("maps/benchmark-pinned.map", editor_history::ESaveKind::COPY, [](const char *pError) { ADD_FAILURE() << pError; }));
	ASSERT_FALSE(Editor().m_WriterFinishJobs.empty());
	Memory("queued_save");
	Bytes[0] ^= 1;
	ASSERT_TRUE(Timeline().Edit(&m_Owner, "After save", editor_history::ECategory::MAP, [&] { Map().m_vpImages.front()->m_Content.emplace(std::span<const std::uint8_t>(Bytes)); }));
	ASSERT_TRUE(Timeline().Clear());
	Memory("clear_with_save_pin");
	DrainSaves();
	Memory("after_save_completion");
	g_Config.m_ClEditorHistoryMemory = 1;
	Timeline().ApplyPreferences();
	ASSERT_TRUE(Timeline().Edit(&m_Owner, "Oversized pair", editor_history::ECategory::MAP, [&] { Map().m_vpGroups.front()->m_OffsetX += 1; }));
	EXPECT_TRUE(Timeline().History()->Oversized());
	EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
	Memory("oversized_minimum_pair");
}

TEST_F(CEditorHistoryBenchmark, DenseProtocol)
{
	if(!std::getenv("DDNET_BENCH_DENSE"))
	{
		GTEST_SKIP();
	}
	Limits();
	Editor().AddGroup();
	auto *pGroup = Map().m_vpGroups.back().get();
	ASSERT_TRUE(Timeline().Edit(&m_Owner, "Dense fixture", editor_history::ECategory::MAP, [&] {
		for(int Index = 0; Index < 4; ++Index)
			pGroup->AddLayer(std::make_shared<CLayerTiles>(&Map(), 1024, 1024));
	}));
	ASSERT_TRUE(Timeline().Clear());
	CBenchSamples Fill("dense_fill_domain"), Commit("dense_fill_history_commit"), Resize("dense_resize_domain"), ResizeCommit("dense_resize_history_commit");
	for(int Sample = 0; Sample < 55; ++Sample)
	{
		ASSERT_TRUE(Timeline().Begin(&m_Owner, "Dense fill", editor_history::ECategory::MAP));
		Fill.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Update(&m_Owner, [&] {
						    for(const auto &pLayer : pGroup->m_vpLayers)
						    {
							    auto &Tiles = std::static_pointer_cast<CLayerTiles>(pLayer)->m_Tiles;
							    for(std::size_t Cell = 0; Cell < Tiles.Size(); ++Cell)
								    Tiles.Set(Cell, CTile{static_cast<unsigned char>(Sample + 1), TILEFLAG_XFLIP, 0, 0});
						    }
					    })); });
		Commit.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
		ASSERT_TRUE(Timeline().Begin(&m_Owner, "Dense resize", editor_history::ECategory::MAP));
		Resize.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Update(&m_Owner, [&] {
						      for(const auto &pLayer : pGroup->m_vpLayers)
							      std::static_pointer_cast<CLayerTiles>(pLayer)->Resize(Sample % 2 ? 1024 : 1056, Sample % 2 ? 1024 : 1056);
					      })); });
		ResizeCommit.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
	}
	Fill.Report();
	Commit.Report();
	Resize.Report();
	ResizeCommit.Report();
	Memory("dense_final");
	EXPECT_LE(Timeline().History()->RetainedBytes(), 256u * 1024u * 1024u);
	EXPECT_EQ(EditorDocumentPersistedKey(Timeline().History()->Current()), Timeline().History()->Revisions()[Timeline().History()->Cursor()].m_PersistedKey);
}

TEST_F(CEditorHistoryBenchmark, ObjectHeavyAndTypedPlanesProtocol)
{
	if(!std::getenv("DDNET_BENCH_DENSE"))
	{
		GTEST_SKIP();
	}
	Limits();
	ASSERT_TRUE(Timeline().Edit(&m_Owner, "Object-heavy fixture", editor_history::ECategory::MAP, [&] {
		auto pGroup = Map().NewGroup();
		auto pQuads = std::make_shared<CLayerQuads>(&Map());
		auto pSounds = std::make_shared<CLayerSounds>(&Map());
		pGroup->AddLayer(pQuads);
		pGroup->AddLayer(pSounds);
		auto pEnvelope = Map().NewEnvelope(CEnvelope::EType::POSITION);
		pEnvelope->m_vPoints.clear();
		for(int Point = 0; Point < 1000; ++Point)
			pEnvelope->AddPoint(CFixedTime(Point * 100), {Point, Point * 2, Point * 3, Point * 4});
		for(int Quad = 0; Quad < 10000; ++Quad)
			pQuads->NewQuad(Quad * 2, Quad * 3, 64, 64)->m_PosEnv = {pEnvelope->m_Id};
		for(int Sound = 0; Sound < 1000; ++Sound)
			pSounds->NewSource(Sound * 2, Sound * 3)->m_PosEnv = {pEnvelope->m_Id};
	}));
	ASSERT_TRUE(Timeline().Clear());
	std::printf("FIXTURE object-heavy quads=10000 sources=1000 envelope_points=1000 key=%s\n", EditorDocumentPersistedKey(Timeline().History()->Current()).c_str());
	CBenchSamples Commit("object_heavy_commit"), Restore("object_heavy_restore_rendered_frame");
	for(int Sample = 0; Sample < 55; ++Sample)
	{
		const auto Before = Timeline().History()->PinCurrent();
		ASSERT_TRUE(Timeline().Begin(&m_Owner, "Object-heavy transform and reorder", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Update(&m_Owner, [&] {
			auto &Group = *Map().m_vpGroups.back();
			for(const auto &pLayer : Group.m_vpLayers)
			{
				if(auto *pQuads = dynamic_cast<CLayerQuads *>(pLayer.get()))
				{
					for(auto &Quad : pQuads->m_vQuads)
						for(auto &Point : Quad.m_aPoints)
							++Point.x;
					std::reverse(pQuads->m_vQuads.begin(), pQuads->m_vQuads.end());
				}
				if(auto *pSounds = dynamic_cast<CLayerSounds *>(pLayer.get()))
					for(auto &Sound : pSounds->m_vSources)
						++Sound.m_Position.y;
			}
			for(auto &Point : Map().m_vpEnvelopes.back()->m_vPoints)
				++Point.m_aValues[0];
			std::reverse(Group.m_vpLayers.begin(), Group.m_vpLayers.end());
		}));
		Commit.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
		const auto After = Timeline().History()->PinCurrent();
		Restore.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Undo()); ASSERT_TRUE(Timeline().PublishAtSafePoint()); RenderFrame(); });
		AssertPublished(Before);
		ASSERT_TRUE(Timeline().Redo());
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		AssertPublished(After);
	}
	Commit.Report();
	Restore.Report();
	Memory("object_heavy_final");
	ASSERT_TRUE(Map().m_pTeleLayer && Map().m_pSpeedupLayer && Map().m_pSwitchLayer && Map().m_pTuneLayer);
	CBenchSamples Typed("all_special_planes_commit");
	for(int Sample = 0; Sample < 55; ++Sample)
	{
		const auto Before = Timeline().History()->PinCurrent();
		ASSERT_TRUE(Timeline().Begin(&m_Owner, "All special planes", editor_history::ECategory::MAP));
		ASSERT_TRUE(Timeline().Update(&m_Owner, [&] {
			const auto Count = Map().m_pGameLayer->m_Tiles.Size();
			for(int Event = 0; Event < 200; ++Event)
			{
				const auto Cell = (Sample * 67u + Event * 31u) % Count;
				Map().m_pTeleLayer->m_TeleTiles.Update(Cell, [&](auto &Tile) { Tile.m_Number = Sample + 1; Tile.m_Type = TILE_TELEIN; });
				Map().m_pSpeedupLayer->m_SpeedupTiles.Update(Cell, [&](auto &Tile) { Tile.m_Force = Sample + 1; Tile.m_MaxSpeed = 100; Tile.m_Type = TILE_SPEED_BOOST; Tile.m_Angle = 135; });
				Map().m_pSwitchLayer->m_SwitchTiles.Update(Cell, [&](auto &Tile) { Tile.m_Number = Sample + 1; Tile.m_Delay = 25; Tile.m_Type = TILE_SWITCHOPEN; });
				Map().m_pTuneLayer->m_TuneTiles.Update(Cell, [&](auto &Tile) { Tile.m_Number = Sample + 1; Tile.m_Type = TILE_TUNE; });
			}
		}));
		Typed.Run(Sample >= 5, [&] { ASSERT_TRUE(Timeline().Complete(&m_Owner, editor_history::EEditCompletion::ACCEPT)); });
		const auto After = Timeline().History()->PinCurrent();
		ASSERT_TRUE(Timeline().Undo());
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		AssertPublished(Before);
		ASSERT_TRUE(Timeline().Redo());
		ASSERT_TRUE(Timeline().PublishAtSafePoint());
		AssertPublished(After);
	}
	Typed.Report();
	Memory("typed_planes_final");
	EXPECT_EQ(EditorDocumentPersistedKey(Timeline().History()->Current()), Timeline().History()->Revisions()[Timeline().History()->Cursor()].m_PersistedKey);
}
