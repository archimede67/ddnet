// Benchmark executable only. Counts C++ allocations; native malloc, Rust,
// driver allocations and allocator bookkeeping are outside this counter.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace benchmark_allocations
{
	struct CHeader
	{
		void *m_pBase;
		std::size_t m_Bytes;
	};
	std::atomic<std::size_t> g_Count{0}, g_Live{0}, g_Peak{0};
	std::atomic<std::size_t> g_Captures{0};
	void *Allocate(std::size_t Bytes, std::size_t Alignment)
	{
		Alignment = std::max(Alignment, alignof(CHeader));
		void *pBase = std::malloc(Bytes + Alignment + sizeof(CHeader));
		if(!pBase)
			std::abort();
		const auto Address = (reinterpret_cast<std::uintptr_t>(pBase) + sizeof(CHeader) + Alignment - 1) & ~(Alignment - 1);
		auto *pHeader = reinterpret_cast<CHeader *>(Address) - 1;
		*pHeader = {pBase, Bytes};
		g_Count.fetch_add(1, std::memory_order_relaxed);
		const auto Live = g_Live.fetch_add(Bytes, std::memory_order_relaxed) + Bytes;
		auto Peak = g_Peak.load(std::memory_order_relaxed);
		while(Peak < Live && !g_Peak.compare_exchange_weak(Peak, Live, std::memory_order_relaxed)) {}
		return reinterpret_cast<void *>(Address);
	}
	void Free(void *p)
	{
		if(!p)
			return;
		const auto *pHeader = static_cast<CHeader *>(p) - 1;
		g_Live.fetch_sub(pHeader->m_Bytes, std::memory_order_relaxed);
		std::free(pHeader->m_pBase);
	}
}
void *operator new(std::size_t n) { return benchmark_allocations::Allocate(n, alignof(std::max_align_t)); }
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { benchmark_allocations::Free(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
void *operator new(std::size_t n, std::align_val_t a) { return benchmark_allocations::Allocate(n, static_cast<std::size_t>(a)); }
void *operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void *p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return ::operator new(n); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return ::operator new(n); }
void operator delete(void *p, const std::nothrow_t &) noexcept { ::operator delete(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { ::operator delete(p); }
