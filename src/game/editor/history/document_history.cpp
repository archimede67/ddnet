#include "document_history.h"

#include <engine/engine.h>
#include <engine/shared/config.h>

#include <game/editor/editor.h>
#include <game/editor/mapitems/map.h>

namespace
{
	template<typename T>
	constexpr T MemoryLimitBytes(int MiB)
	{
		const std::uint64_t Bytes = static_cast<std::uint64_t>(MiB) * 1024ULL * 1024ULL;
		return static_cast<T>(std::min(Bytes, static_cast<std::uint64_t>(std::numeric_limits<T>::max())));
	}
	static_assert(MemoryLimitBytes<std::uint32_t>(4095) == 4293918720ULL);
	static_assert(MemoryLimitBytes<std::uint32_t>(4096) == 4294967295ULL);
	static_assert(MemoryLimitBytes<std::uint32_t>(16384) == 4294967295ULL);
	static_assert(MemoryLimitBytes<std::uint64_t>(16384) == 17179869184ULL);
}

CEditorDocumentHistory::CEditorDocumentHistory(CEditorMap *pMap) :
	CMapObject(pMap) {}
CEditorDocumentHistory::~CEditorDocumentHistory() = default;

bool CEditorDocumentHistory::Initialize()
{
	std::string Error;
	auto Document = Map()->CaptureDocument(nullptr, Error);
	if(!Document)
		return false;
	auto pHistory = CHistory::Create(std::move(*Document), {static_cast<std::size_t>(g_Config.m_ClEditorMaxHistory), MemoryLimitBytes<std::size_t>(g_Config.m_ClEditorHistoryMemory)}, [](const CEditorDocumentValues &Root) { std::string ValidationError; return Root.Validate(ValidationError); }, [this](const CEditorDocumentValues &Root) { return Map()->m_FingerprintCache.Key(Root); }, [](const CEditorDocumentValues &Root, editor_history::CStorageUsage &Usage) { Root.Account(Usage); });
	if(!pHistory || !pHistory->UseSaveState(Map()->m_pSaveState))
		return false;
	m_pCoordinator = std::make_unique<CCoordinator>(std::move(pHistory), [this](const CEditorDocumentValues &Previous, std::string &CaptureError) { return Map()->CaptureDocument(&Previous, CaptureError); }, [this](const CEditorDocumentValues &Root) { return Map()->RestoreDocumentAtSafePoint(Root, [this](const char *pMessage) { Editor()->ShowFileDialogError("%s", pMessage); }); }, [this] { Map()->RememberDocumentSession(); }, [this](const char *pMessage) { Editor()->ShowFileDialogError("%s", pMessage); });
	m_Owner.reset();
	m_pOwnerKey = nullptr;
	RefreshPresentation();
	return true;
}

bool CEditorDocumentHistory::Begin(const void *pOwner, const char *pLabel, ECategory Category)
{
	if(!Ready() || !pOwner || Pending() || Publishing())
		return false;
	if(Owns(pOwner))
		return true;
	if(Active() && !CompleteActive(ECompletion::TOOL_SWITCH))
		return false;
	m_Owner = m_pCoordinator->BeginEdit(pLabel, Category);
	if(!m_Owner)
		return false;
	m_pOwnerKey = pOwner;
	InvalidateMemoryUsage();
	m_HeldKey = 0;
	m_pCaptureItem = nullptr;
	m_ControlOwner = false;
	return true;
}

void CEditorDocumentHistory::StartFrame()
{
	m_ControlRendered = false;
	ApplyPreferences();
	if(m_MemoryRequested)
		MemoryUsage();
}

void CEditorDocumentHistory::ApplyPreferences()
{
	if(!Ready() || Active() || Pending())
		return;
	const editor_history::CRetentionLimits Limits{static_cast<std::size_t>(g_Config.m_ClEditorMaxHistory), MemoryLimitBytes<std::size_t>(g_Config.m_ClEditorHistoryMemory)};
	const auto Previous = History()->Limits();
	if(Previous.m_Entries == Limits.m_Entries && Previous.m_Bytes == Limits.m_Bytes)
		return;
	if(m_pCoordinator->SetRetentionLimits(Limits))
		PruneSession();
}

class CEditorDocumentHistory::CMemoryJob : public IJob
{
	void Run() override
	{
#if defined(__cpp_exceptions)
		try
		{
#endif
			editor_history::CStorageUsage Live, Draft, Saves, Caches;
			m_Live.Account(Live);
			m_Result.m_LiveBytes = Live.Bytes();
			if(m_Active)
			{
				for(const auto &pRoot : m_vRetained)
					if(Draft.Add(pRoot.get(), sizeof(CEditorDocumentValues)))
						pRoot->Account(Draft);
				const auto Retained = Draft.Bytes();
				m_Live.Account(Draft);
				m_Draft.Account(Draft);
				m_Result.m_DraftExtraBytes = Draft.Bytes() - Retained;
			}
			for(const auto &pRoot : m_vSaves)
				if(Saves.Add(pRoot.get(), sizeof(CEditorDocumentValues)))
					pRoot->Account(Saves);
			m_Result.m_SavePinnedBytes = Saves.Bytes();
			m_Caches.Account(Caches);
			m_Result.m_CacheBytes = Caches.Bytes();
			m_Result.m_Sampled = true;
#if defined(__cpp_exceptions)
		}
		catch(const std::exception &)
		{
			m_Result.m_Failed = true;
		}
#endif
		// Retire potentially large ownership graphs on this worker, even if the
		// document closed while measuring. The UI only collects scalar results.
		m_Live = {};
		m_Draft = {};
		m_Caches = {};
		m_vRetained.clear();
		m_vSaves.clear();
	}

public:
	SMemoryUsage m_Result;
	bool m_Active = false;
	editor_history::CStorageObservation m_Live, m_Draft, m_Caches;
	std::vector<std::shared_ptr<const CEditorDocumentValues>> m_vRetained, m_vSaves;
};

CEditorDocumentHistory::SMemoryUsage CEditorDocumentHistory::MemoryUsage() const
{
	m_MemoryRequested = true;
	if(!Ready())
		return {};
	if(m_FingerprintGeneration != Map()->m_FingerprintCache.Generation())
	{
		InvalidateMemoryUsage();
		m_FingerprintGeneration = Map()->m_FingerprintCache.Generation();
	}
	// Completion can retire an export pin before the editor collects its ticket.
	// Observe only this map's scalar job states, without acquiring temporary pins.
	const auto CompletedSaves = std::count_if(Editor()->m_WriterFinishJobs.begin(), Editor()->m_WriterFinishJobs.end(), [&](const auto &pSave) {
		return Map()->OwnsSave(pSave->Ticket()) && pSave->State() == IJob::STATE_DONE;
	});
	if(m_CompletedSaves != static_cast<std::size_t>(CompletedSaves))
	{
		m_CompletedSaves = CompletedSaves;
		InvalidateMemoryUsage();
	}
	if(m_pMemoryJob && m_pMemoryJob->State() == IJob::STATE_DONE)
	{
		const auto &Result = m_pMemoryJob->m_Result;
		if(Result.m_Generation == m_MemoryGeneration && Result.m_Sampled)
		{
			const auto SamplesStarted = m_MemoryUsage.m_SamplesStarted;
			m_MemoryUsage = Result;
			m_MemoryUsage.m_SamplesStarted = SamplesStarted;
			m_NextMemoryRetry = {};
		}
		else if(Result.m_Failed)
		{
			m_MemoryUsage.m_Failed = true;
			m_NextMemoryRetry = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		}
		m_pMemoryJob.reset();
	}
	m_MemoryUsage.m_Current = m_MemoryUsage.m_Sampled && m_MemoryUsage.m_Generation == m_MemoryGeneration;
	if(!m_pMemoryJob && !m_MemoryUsage.m_Current && std::chrono::steady_clock::now() >= m_NextMemoryRetry)
	{
#if defined(__cpp_exceptions)
		try
		{
#endif
			auto pJob = std::make_shared<CMemoryJob>();
			pJob->m_Result.m_Generation = m_MemoryGeneration;
			Map()->ObserveLiveStorage(pJob->m_Live);
			Map()->ObserveRuntimeCaches(pJob->m_Caches);
			pJob->m_Active = Active();
			if(pJob->m_Active)
			{
				for(const auto &Revision : History()->Revisions())
					pJob->m_vRetained.push_back(Revision.m_pRoot);
				History()->ObserveDraft(pJob->m_Draft);
			}
			pJob->m_vSaves.reserve(Editor()->m_WriterFinishJobs.size());
			for(const auto &pSave : Editor()->m_WriterFinishJobs)
				if(Map()->OwnsSave(pSave->Ticket()))
				{
					auto pRoot = pSave->ExportRoot();
					if(pRoot)
						pJob->m_vSaves.push_back(std::move(pRoot));
				}
			Editor()->Engine()->AddJob(pJob);
			m_pMemoryJob = std::move(pJob);
			++m_MemoryUsage.m_SamplesStarted;
#if defined(__cpp_exceptions)
		}
		catch(const std::exception &)
		{
			m_MemoryUsage.m_Failed = true;
			m_NextMemoryRetry = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		}
#endif
	}
	m_MemoryUsage.m_Updating = m_pMemoryJob != nullptr;
	return m_MemoryUsage;
}

void CEditorDocumentHistory::FinishFrame()
{
	if(Active() && m_pCaptureItem && Editor()->Ui()->ActiveItem() != m_pCaptureItem)
		Cancel(ECancellation::CAPTURE_LOST);
	if(Active() && m_ControlOwner && !m_ControlRendered)
		Cancel(ECancellation::OWNER_DESTROYED);
}

void CEditorDocumentHistory::KeyReleased(int Key)
{
	if(Active() && m_HeldKey == Key)
		CompleteActive(ECompletion::KEY_RELEASE);
}

bool CEditorDocumentHistory::Complete(const void *pOwner, ECompletion Reason)
{
	return Owns(pOwner) && CompleteActive(Reason);
}

bool CEditorDocumentHistory::BeginControl(const void *pOwner, const char *pLabel, EEditState State, ECategory Category)
{
	if(State == EEditState::CANCELLED)
	{
		if(Owns(pOwner))
			Cancel(ECancellation::INVALID_INPUT);
		return false;
	}
	if(State == EEditState::NONE)
		return false;
	if(!Begin(pOwner, pLabel, Category))
		return false;
	InvalidateMemoryUsage();
	m_ControlOwner = true;
	m_ControlRendered = true;
	return true;
}

void CEditorDocumentHistory::EndControl(const void *pOwner, EEditState State)
{
	if(State == EEditState::END || State == EEditState::ONE_GO)
		Complete(pOwner, ECompletion::ACCEPT);
}

bool CEditorDocumentHistory::CompleteActive(ECompletion Reason)
{
	if(!Active())
		return !Pending();
	const auto Result = m_pCoordinator->CompleteActive(Reason);
	if(Result == editor_history::ECommitResult::INVALID_OWNER)
		return false;
	m_Owner.reset();
	m_pOwnerKey = nullptr;
	RefreshPresentation();
	PruneSession();
	ApplyPreferences();
	return Result != editor_history::ECommitResult::FAILED;
}

bool CEditorDocumentHistory::Cancel(ECancellation Reason)
{
	return Ready() && m_pCoordinator->CancelActive(Reason);
}

bool CEditorDocumentHistory::Undo() { return Ready() && m_pCoordinator->RequestUndo(); }
bool CEditorDocumentHistory::Redo() { return Ready() && m_pCoordinator->RequestRedo(); }
bool CEditorDocumentHistory::CanUndo() const { return Ready() && !Pending() && History()->CanUndo(); }
bool CEditorDocumentHistory::CanRedo() const { return Ready() && !Pending() && History()->CanRedo(); }
bool CEditorDocumentHistory::GoTo(std::size_t Revision) { return Ready() && m_pCoordinator->RequestRevision(Revision); }

bool CEditorDocumentHistory::Clear()
{
	if(!Ready() || !m_pCoordinator->Clear())
		return false;
	PruneSession();
	return true;
}

bool CEditorDocumentHistory::PublishAtSafePoint()
{
	if(!Ready())
		return false;
	if(!m_pCoordinator->PublishAtSafePoint())
	{
		// Failed navigation is dismissible; cancellation/rollback remains pending.
		m_pCoordinator->AbandonPendingTraversal();
		return false;
	}
	m_Owner.reset();
	m_pOwnerKey = nullptr;
	RefreshPresentation();
	PruneSession();
	return true;
}

void CEditorDocumentHistory::RefreshPresentation()
{
	InvalidateMemoryUsage();
	if(!Ready())
		return;
	Map()->m_Modified = m_pCoordinator->Dirty(editor_history::ESaveKind::MANUAL);
	Map()->m_ModifiedAuto = m_pCoordinator->Dirty(editor_history::ESaveKind::AUTOMATIC);
	Map()->m_pLastCapturedDocument = History()->PinCurrent();
}

void CEditorDocumentHistory::PruneSession()
{
	InvalidateMemoryUsage();
	if(!Ready() || Active() || Pending())
		return;
#if defined(__cpp_exceptions)
	try
	{
#endif
		std::vector<const CEditorDocumentValues *> vpRoots;
		for(const auto &Revision : History()->Revisions())
			vpRoots.push_back(Revision.m_pRoot.get());
		Map()->PruneDocumentSession(vpRoots);
#if defined(__cpp_exceptions)
	}
	catch(const std::exception &)
	{
		// Tombstones can be pruned at the next successful maintenance boundary.
		// A completed document publication must remain successful.
	}
#endif
}
