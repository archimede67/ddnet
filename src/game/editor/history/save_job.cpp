#include "save_job.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/shared/filecollection.h>
#include <engine/storage.h>

#include <game/editor/mapitems/document_export.h>

#include <exception>

void CDataFileWriterFinishJob::Run()
{
	const editor_history::CSaveQueue::CGuard WriteOrder(m_pWriteTurn);
	Export();
	// Diagnostics acquire their own immutable pin atomically. The export's last
	// owner is retired here, including open/export/replacement failure paths.
	std::atomic_store(&m_pExportRoot, std::shared_ptr<const CEditorDocumentValues>{});
}

void CDataFileWriterFinishJob::Export()
{
	CDataFileWriter Writer;
#if defined(__cpp_exceptions)
	try
	{
#endif
		if(!Writer.Open(m_pStorage, m_aTempFilename))
		{
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), "Saving failed: Could not open temporary map file '%s'.", m_aTempFilename);
			log_error("editor/save", "%s", m_aErrorMessage);
			return;
		}
		Writer.SetEagerCompression();
		std::string ExportError;
		if(!WriteEditorDocument(*std::atomic_load(&m_pExportRoot), Writer, ExportError))
		{
			Writer.Abort();
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), "Saving failed: %s.", ExportError.c_str());
			log_error("editor/save", "%s", m_aErrorMessage);
			m_pStorage->RemoveFile(m_aTempFilename, IStorage::TYPE_SAVE);
			return;
		}
		if(!Writer.Finish())
		{
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), "Saving failed: Could not completely write temporary map file '%s'.", m_aTempFilename);
			log_error("editor/save", "%s", m_aErrorMessage);
			m_pStorage->RemoveFile(m_aTempFilename, IStorage::TYPE_SAVE);
			return;
		}

		char aTempPath[IO_MAX_PATH_LENGTH];
		char aRealPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_aTempFilename, aTempPath, sizeof(aTempPath));
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_aRealFilename, aRealPath, sizeof(aRealPath));
		if(fs_replace(aTempPath, aRealPath) != 0)
		{
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), "Saving failed: Could not move temporary map file '%s' to '%s'.", m_aTempFilename, m_aRealFilename);
			log_error("editor/save", "%s", m_aErrorMessage);
			m_pStorage->RemoveFile(m_aTempFilename, IStorage::TYPE_SAVE);
			return;
		}

		log_trace("editor/save", "Saved map to '%s'.", m_aRealFilename);
#if defined(__cpp_exceptions)
	}
	catch(const std::exception &)
	{
		Writer.Abort();
		str_format(m_aErrorMessage, sizeof(m_aErrorMessage), "Saving failed: Could not finish temporary map file '%s'.", m_aTempFilename);
		log_error("editor/save", "%s", m_aErrorMessage);
		m_pStorage->RemoveFile(m_aTempFilename, IStorage::TYPE_SAVE);
		return;
	}
#endif
	// Retention scans/deletions are disk work too. They must not delay a frame,
	// and a cleanup failure cannot undo the successful final replacement.
#if defined(__cpp_exceptions)
	try
	{
#endif
		if(m_Ticket.Kind() == editor_history::ESaveKind::AUTOMATIC && m_AutosaveMax > 0 && !m_AutosaveName.empty())
		{
			CFileCollection AutosavedMaps;
			AutosavedMaps.Init(m_pStorage, "maps/auto", m_AutosaveName.c_str(), ".map", m_AutosaveMax);
		}
#if defined(__cpp_exceptions)
	}
	catch(const std::exception &)
	{
		log_warn("editor/save", "Could not clean up older autosaves.");
	}
#endif
}

CDataFileWriterFinishJob::CDataFileWriterFinishJob(IStorage *pStorage, const char *pTempFilename, CEditorSaveState::CTicket Ticket) :
	m_pStorage(pStorage),
	m_Ticket(std::move(Ticket)),
	m_pExportRoot(std::exchange(m_Ticket.m_pRoot, nullptr))
{
	str_copy(m_aRealFilename, m_Ticket.m_Destination.c_str());
	str_copy(m_aTempFilename, pTempFilename);
	m_aErrorMessage[0] = '\0';
}
