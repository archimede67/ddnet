#ifndef GAME_EDITOR_HISTORY_SAVE_JOB_H
#define GAME_EDITOR_HISTORY_SAVE_JOB_H

#include "save_queue.h"
#include "save_state.h"

#include <base/io.h>

#include <engine/shared/datafile.h>
#include <engine/shared/jobs.h>

#include <game/editor/mapitems/document_graph.h>

using CEditorSaveState = editor_history::CSaveState<CEditorDocumentValues>;

/**
 * Pins one immutable revision for an ordered, non-abortable background save.
 * Export, compression, writing and replacement happen on the worker; it never
 * reads live editor objects. The main thread applies successful ticket completion
 * to the original map lifetime. Queuing a job alone does not mark that map saved.
 */
class CDataFileWriterFinishJob : public IJob
{
	IStorage *m_pStorage;
	char m_aRealFilename[IO_MAX_PATH_LENGTH];
	char m_aTempFilename[IO_MAX_PATH_LENGTH];
	char m_aErrorMessage[2 * IO_MAX_PATH_LENGTH + 128];
	CEditorSaveState::CTicket m_Ticket;
	std::shared_ptr<const CEditorDocumentValues> m_pExportRoot;
	std::shared_ptr<editor_history::CSaveQueue::CTurn> m_pWriteTurn;
	std::string m_AutosaveName;
	int m_AutosaveMax = 0;
	void Run() override;
	void Export();

public:
	CDataFileWriterFinishJob(IStorage *pStorage, const char *pTempFilename, CEditorSaveState::CTicket Ticket);
	void SetWriteTurn(std::shared_ptr<editor_history::CSaveQueue::CTurn> pTurn) { m_pWriteTurn = std::move(pTurn); }
	void SetAutosaveName(std::string Name, int Maximum)
	{
		m_AutosaveName = std::move(Name);
		m_AutosaveMax = Maximum;
	}
	const std::string &AutosaveName() const { return m_AutosaveName; }
	const char *RealFilename() const { return m_aRealFilename; }
	const char *ErrorMessage() const { return m_aErrorMessage; }
	const CEditorSaveState::CTicket &Ticket() const { return m_Ticket; }
	/** Snapshot the worker-owned input; completion metadata never retains it. */
	std::shared_ptr<const CEditorDocumentValues> ExportRoot() const { return std::atomic_load(&m_pExportRoot); }
};

#endif
