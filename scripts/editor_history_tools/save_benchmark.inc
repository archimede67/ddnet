// Included only in an isolated save-performance exercise, without allocation hooks.
namespace save_benchmark
{
	using CClock = std::chrono::steady_clock;
	struct CSample
	{
		std::string m_Phase;
		double m_Milliseconds;
	};
	std::vector<CSample> g_vSamples;
	template<typename F>
	void Measure(const std::string &Phase, F &&Function)
	{
		const auto Start = CClock::now();
		Function();
		const auto Elapsed = std::chrono::duration<double, std::milli>(CClock::now() - Start).count();
		g_vSamples.push_back({Phase, Elapsed});
	}
}

class CEditorHistorySaveBenchmark : public CEditorHistoryRuntime
{
protected:
	int m_Owner;
	unsigned m_NextCell = 0;
	void VerifySave(const std::shared_ptr<CDataFileWriterFinishJob> &pJob)
	{
		ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
		ASSERT_STREQ(pJob->ErrorMessage(), "");
		CDataFileReader Reader;
		ASSERT_TRUE(Reader.Open(Editor().Storage(), pJob->RealFilename(), IStorage::TYPE_SAVE));
		int Start, Count;
		Reader.GetType(MAPITEMTYPE_LAYER, &Start, &Count);
		EXPECT_GT(Count, 0);
	}
	void RenderFrame()
	{
		Editor().OnUpdate();
		Editor().OnRender();
		Editor().Graphics()->Swap();
		Editor().Graphics()->WaitForIdle();
	}
	void Edit()
	{
		ASSERT_TRUE(Timeline().Edit(&m_Owner, "Save benchmark stroke", editor_history::ECategory::MAP, [&] {
			auto &Plane = Map().m_pGameLayer->m_Tiles;
			Plane.Update((static_cast<std::size_t>(m_NextCell++) * 67) % Plane.Size(), [](auto &Tile) { Tile.m_Index = Tile.m_Index == 1 ? 3 : 1; });
		}));
	}
	void PumpSaving(const std::string &Kind)
	{
		const auto Deadline = save_benchmark::CClock::now() + std::chrono::minutes(5);
		unsigned Frame = 0;
		while(!Editor().m_WriterFinishJobs.empty())
		{
			ASSERT_LT(save_benchmark::CClock::now(), Deadline);
			const auto Start = save_benchmark::CClock::now();
			const std::size_t Before = Editor().m_WriterFinishJobs.size();
			const char *pAction = Frame % 4 == 0 ? "edit" : Frame % 4 == 1 ? "undo" :
								Frame % 4 == 2         ? "redo" :
											 "idle";
			save_benchmark::Measure(Kind + "_" + pAction + "_frame", [&] {
				if(Frame % 4 == 0)
					Edit();
				else if(Frame % 4 == 1)
					ASSERT_TRUE(Timeline().Undo() && Timeline().PublishAtSafePoint());
				else if(Frame % 4 == 2)
					ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
				RenderFrame();
			});
			if(Editor().m_WriterFinishJobs.size() != Before)
				save_benchmark::g_vSamples.push_back({Kind + "_completion_frame", save_benchmark::g_vSamples.back().m_Milliseconds});
			ASSERT_EQ(Timeline().History()->Revisions().size(), 51u);
			++Frame;
			std::this_thread::sleep_until(Start + std::chrono::milliseconds(16));
		}
		// Always leave the cursor at the tip before another measurement/save.
		if(Timeline().CanRedo())
			save_benchmark::Measure(Kind + "_finish_redo_frame", [&] {
				ASSERT_TRUE(Timeline().Redo() && Timeline().PublishAtSafePoint());
				RenderFrame();
			});
	}
};

TEST_F(CEditorHistorySaveBenchmark, NativeBaselineExportParity)
{
	ASSERT_TRUE(Map().SaveWithKind("maps/normalized-baseline.map", editor_history::ESaveKind::COPY, [](const char *pError) { ADD_FAILURE() << pError; }));
	DrainSaves();
}

TEST_F(CEditorHistorySaveBenchmark, ForegroundLatencyDuringSaves)
{
	g_Config.m_EdAutosaveInterval = 0;
	g_Config.m_ClEditorMaxHistory = 50;
	Timeline().ApplyPreferences();
	ASSERT_TRUE(Timeline().Clear());
	SelectGame();
	Editor().m_ActiveExtraEditor = CEditor::EXTRAEDITOR_HISTORY;
	for(int Entry = 0; Entry < 50; ++Entry)
		Edit();
	ASSERT_EQ(Timeline().History()->Revisions().size(), 51u);
	for(int Frame = 0; Frame < 8; ++Frame)
		RenderFrame();
	benchmark_allocations::PrintMemory("save_ready");
	save_benchmark::g_vSamples.clear();
	save_benchmark::g_vSamples.reserve(65536);
	const int Repeats = std::getenv("DDNET_SAVE_BENCH_REPEATS") ? std::atoi(std::getenv("DDNET_SAVE_BENCH_REPEATS")) : 3;
	ASSERT_GT(Repeats, 0);
	for(const bool Automatic : {false, true})
		for(int Repeat = 0; Repeat < Repeats; ++Repeat)
		{
			const std::string Kind = Automatic ? "automatic" : "manual";
			const auto Start = save_benchmark::CClock::now();
			save_benchmark::Measure(Kind + "_enqueue", [&] {
				if(Automatic)
					ASSERT_TRUE(Map().PerformAutosave([](const char *pError) { ADD_FAILURE() << pError; }));
				else
					ASSERT_TRUE(Editor().Save("maps/manual.map"));
			});
			ASSERT_FALSE(Editor().m_WriterFinishJobs.empty());
			const auto pJob = Editor().m_WriterFinishJobs.back();
			PumpSaving(Kind);
			const auto Elapsed = std::chrono::duration<double, std::milli>(save_benchmark::CClock::now() - Start).count();
			save_benchmark::g_vSamples.push_back({Kind + "_total", Elapsed});
			VerifySave(pJob);
			EXPECT_TRUE(Map().m_Modified);
			benchmark_allocations::PrintMemory((Kind + "_complete").c_str());
		}
	if(!std::getenv("DDNET_SAVE_BENCH_NO_OVERLAP"))
	{
		const auto Start = save_benchmark::CClock::now();
		std::vector<std::shared_ptr<CDataFileWriterFinishJob>> vpJobs;
		for(int Index = 0; Index < 3; ++Index)
		{
			Edit();
			save_benchmark::Measure("overlap_enqueue", [&] {
				ASSERT_TRUE(Map().SaveWithKind(Index < 2 ? "maps/overlap.map" : "maps/other.map", editor_history::ESaveKind::COPY, [](const char *pError) { ADD_FAILURE() << pError; }));
			});
			ASSERT_FALSE(Editor().m_WriterFinishJobs.empty());
			vpJobs.push_back(Editor().m_WriterFinishJobs.back());
		}
		PumpSaving("overlap");
		save_benchmark::g_vSamples.push_back({"overlap_total", std::chrono::duration<double, std::milli>(save_benchmark::CClock::now() - Start).count()});
		for(const auto &pJob : vpJobs)
			VerifySave(pJob);
		benchmark_allocations::PrintMemory("overlap_complete");
	}
	for(int Frame = 0; Frame < 250; ++Frame)
	{
		save_benchmark::Measure("settle_frame", [&] { RenderFrame(); });
		if(Frame >= 31 && (Timeline().MemoryUsage().m_Current || std::getenv("DDNET_SAVE_BENCH_LEGACY_MEMORY")))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(4));
	}
	if(!std::getenv("DDNET_SAVE_BENCH_LEGACY_MEMORY"))
		EXPECT_TRUE(Timeline().MemoryUsage().m_Current);
	for(const auto &Sample : save_benchmark::g_vSamples)
		std::printf("SAVE_SAMPLE phase=%s milliseconds=%.6f\n", Sample.m_Phase.c_str(), Sample.m_Milliseconds);
	benchmark_allocations::PrintMemory("save_settled");
}
