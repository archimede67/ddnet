#include <game/editor/history/revision_history.h>
#include <game/editor/history/tile_plane.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

using namespace editor_history;

namespace
{

	using CClock = std::chrono::steady_clock;

	class CCell
	{
	public:
		std::uint16_t m_Index = 0;
		std::uint16_t m_Flags = 0;
		bool operator==(const CCell &) const = default;
	};

	constexpr std::size_t WIDTH = 1024;
	constexpr std::size_t HEIGHT = 1024;
	constexpr std::size_t LAYERS = 4;
	constexpr std::size_t RESOURCE_BYTES = 8 * 1024 * 1024;
	constexpr int SAMPLES = 60;

	CCell FixtureCell(std::size_t Layer, std::size_t X, std::size_t Y)
	{
		return {static_cast<std::uint16_t>((X * 17 + Y * 31 + Layer * 71) % 256), static_cast<std::uint16_t>((X + Y + Layer) % 8)};
	}

	class CFullDocument
	{
	public:
		int m_Value = 0;
		std::array<std::vector<CCell>, LAYERS> m_aLayers;
		std::vector<unsigned char> m_vResource;
		bool operator==(const CFullDocument &) const = default;

		CFullDocument() :
			m_vResource(RESOURCE_BYTES, 127)
		{
			for(std::size_t Layer = 0; Layer < LAYERS; ++Layer)
			{
				m_aLayers[Layer].resize(WIDTH * HEIGHT);
				for(std::size_t Y = 0; Y < HEIGHT; ++Y)
					for(std::size_t X = 0; X < WIDTH; ++X)
						m_aLayers[Layer][Y * WIDTH + X] = FixtureCell(Layer, X, Y);
			}
		}

		CCell At(std::size_t Layer, std::size_t X, std::size_t Y) const { return m_aLayers[Layer][Y * WIDTH + X]; }
		void Set(std::size_t Layer, std::size_t X, std::size_t Y, CCell Cell) { m_aLayers[Layer][Y * WIDTH + X] = Cell; }
		const std::vector<unsigned char> &Resource() const { return m_vResource; }

		void Account(CStorageUsage &Usage) const
		{
			for(const auto &vLayer : m_aLayers)
				Usage.Add(vLayer.data(), vLayer.capacity() * sizeof(CCell));
			Usage.Add(m_vResource.data(), m_vResource.capacity());
		}
	};

	template<std::size_t ChunkEdge>
	class CSharedDocument
	{
	public:
		int m_Value = 0;
		std::array<CTilePlane<CCell, ChunkEdge>, LAYERS> m_aLayers;
		CSharedVector<unsigned char> m_Resource{std::vector<unsigned char>(RESOURCE_BYTES, 127)};
		bool operator==(const CSharedDocument &) const = default;

		CSharedDocument()
		{
			for(std::size_t Layer = 0; Layer < LAYERS; ++Layer)
			{
				m_aLayers[Layer] = *CTilePlane<CCell, ChunkEdge>::Create(WIDTH, HEIGHT);
				for(std::size_t Y = 0; Y < HEIGHT; ++Y)
					for(std::size_t X = 0; X < WIDTH; ++X)
						m_aLayers[Layer].Set(X, Y, FixtureCell(Layer, X, Y));
			}
		}

		CCell At(std::size_t Layer, std::size_t X, std::size_t Y) const { return m_aLayers[Layer].At(X, Y); }
		void Set(std::size_t Layer, std::size_t X, std::size_t Y, CCell Cell) { m_aLayers[Layer].Set(X, Y, Cell); }
		const std::vector<unsigned char> &Resource() const { return m_Resource.Read(); }

		void Account(CStorageUsage &Usage) const
		{
			for(const auto &Layer : m_aLayers)
				Layer.Account(Usage);
			m_Resource.Account(Usage);
		}
	};

	template<typename T>
	std::string FullScanKey(const T &Document)
	{
		// A reproducible semantic checksum for the benchmark, not the production
		// persisted-content fingerprint. This intentionally measures a full scan.
		std::uint64_t Hash = 14695981039346656037ULL;
		const auto Add = [&](std::uint64_t Value) { Hash = (Hash ^ Value) * 1099511628211ULL; };
		Add(Document.m_Value);
		for(std::size_t Layer = 0; Layer < LAYERS; ++Layer)
			for(std::size_t Y = 0; Y < HEIGHT; ++Y)
				for(std::size_t X = 0; X < WIDTH; ++X)
				{
					const auto Cell = Document.At(Layer, X, Y);
					Add(Cell.m_Index);
					Add(Cell.m_Flags);
				}
		for(const auto Byte : Document.Resource())
			Add(Byte);
		return std::to_string(Hash);
	}

	double Elapsed(CClock::time_point Start)
	{
		return std::chrono::duration<double, std::milli>(CClock::now() - Start).count();
	}

	void Report(const std::string &Variant, const char *pPhase, std::vector<double> vTimes)
	{
		std::sort(vTimes.begin(), vTimes.end());
		std::printf("history-benchmark variant=%s phase=%s samples=%zu p50_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",
			Variant.c_str(), pPhase, vTimes.size(), vTimes[vTimes.size() / 2], vTimes[vTimes.size() * 95 / 100], vTimes.back());
	}

	template<typename T>
	void Benchmark(const std::string &Variant)
	{
		T Initial;
		std::printf("history-benchmark fixture=synthetic-dense-v1 variant=%s width=%zu height=%zu layers=%zu resource_bytes=%zu semantic_checksum=%s\n",
			Variant.c_str(), WIDTH, HEIGHT, LAYERS, RESOURCE_BYTES, FullScanKey(Initial).c_str());
		auto pHistory = CRevisionHistory<T>::Create(std::move(Initial), {100, 64 * 1024 * 1024}, [](const T &) { return true; }, FullScanKey<T>, [](const T &Document, auto &Usage) { Document.Account(Usage); });
		ASSERT_NE(pHistory, nullptr);
		auto &History = *pHistory;
		const auto BaselineBytes = History.RetainedBytes();
		std::vector<double> vBegin, vUpdate, vCommit, vCancel, vRestore;
		for(int Sample = 0; Sample < SAMPLES; ++Sample)
		{
			auto Start = CClock::now();
			auto Edit = History.BeginEdit("Sparse stroke", ECategory::MAP);
			vBegin.push_back(Elapsed(Start));
			ASSERT_TRUE(Edit);
			Start = CClock::now();
			for(std::size_t Cell = 0; Cell < 64; ++Cell)
				ASSERT_TRUE(History.Update(*Edit, [&](T &Document) {
					Document.Set(Sample % LAYERS, (Sample * 67 + Cell) % WIDTH, (Sample * 19) % HEIGHT, {static_cast<std::uint16_t>(Sample + 256), 7});
				}));
			vUpdate.push_back(Elapsed(Start));
			Start = CClock::now();
			ASSERT_EQ(History.Commit(*Edit), ECommitResult::CHANGED);
			vCommit.push_back(Elapsed(Start));
			Start = CClock::now();
			ASSERT_TRUE(History.RequestUndo());
			ASSERT_TRUE(History.PublishPending());
			ASSERT_TRUE(History.RequestRedo());
			ASSERT_TRUE(History.PublishPending());
			vRestore.push_back(Elapsed(Start));
			Edit = History.BeginEdit("Cancelled sparse edit", ECategory::MAP);
			ASSERT_TRUE(Edit);
			ASSERT_TRUE(History.Update(*Edit, [](T &Document) { Document.m_Value = 999; }));
			Start = CClock::now();
			Edit->Cancel();
			vCancel.push_back(Elapsed(Start));
		}
		CStorageUsage Usage;
		History.Current().Account(Usage);
		std::printf("history-benchmark variant=%s baseline_bytes=%zu retained_bytes=%zu retained_states=%zu current_payload_allocations=%zu oversized=%d\n",
			Variant.c_str(), BaselineBytes, History.RetainedBytes(), History.Revisions().size(), Usage.Allocations(), History.Oversized());
		Report(Variant, "begin", std::move(vBegin));
		Report(Variant, "64_updates", std::move(vUpdate));
		Report(Variant, "commit_including_full_scan_key_and_retention", std::move(vCommit));
		Report(Variant, "cancel", std::move(vCancel));
		Report(Variant, "undo_redo_publication", std::move(vRestore));
	}

} // namespace

// Run explicitly in an optimized build. Ordinary correctness runs never make
// timing assertions or spend time on the benchmark workload.
TEST(EditorHistoryBenchmark, DISABLED_SparseStorageComparison)
{
	Benchmark<CFullDocument>("independent-copy");
	Benchmark<CSharedDocument<8>>("chunk-8");
	Benchmark<CSharedDocument<16>>("chunk-16");
	Benchmark<CSharedDocument<32>>("chunk-32");
	Benchmark<CSharedDocument<64>>("chunk-64");
}
