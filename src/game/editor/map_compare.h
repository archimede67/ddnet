#ifndef GAME_EDITOR_MAP_COMPARE_H
#define GAME_EDITOR_MAP_COMPARE_H

#include "map_diff.h"

#include <engine/input.h>

#include <game/client/ui_listbox.h>

#include <atomic>
#include <future>
#include <memory>

class CEditor;
class CEditorMap;
class CLayerGroup;

class CMapCompare
{
	enum class EState
	{
		CLOSED,
		SELECT,
		CAPTURE,
		COMPUTING,
		READY,
		UNAVAILABLE,
		CLOSING
	};
	EState m_State = EState::CLOSED;
	const CEditorMap *m_apSources[2]{};
	struct CData
	{
		MapDiff::CSnapshot m_aSnapshots[2];
		std::atomic<bool> m_Cancel{false};
	};
	std::shared_ptr<CData> m_pData;
	std::future<MapDiff::CResult> m_Future;
	std::vector<std::future<MapDiff::CResult>> m_vRetiredWork;
	struct CRenderCache;
	std::unique_ptr<CRenderCache> m_pRenderCache;
	MapDiff::CResult m_Result;
	std::string m_Message;
	bool m_ByGroup = false;
	enum
	{
		CONTEXT_OFF,
		CONTEXT_SELECTION,
		CONTEXT_MAP,
	};
	int m_Context = CONTEXT_OFF;
	bool m_UnchangedExpanded = false;
	int m_UnchangedCount = 0;
	char m_UnchangedButtonId{};
	bool m_ShowLayers = false;
	struct CLayerState
	{
		bool m_Visible = true;
		bool m_Expanded = true;
		size_t m_Changes = 0; // Groups include their children's differences.
	};
	std::vector<CLayerState> m_vLayerStates;
	CScrollRegion m_LayerTree;
	bool m_ScrollToLayer = false;
	bool m_LayerTreeActive = true;
	bool m_Panning = false;
	vec2 m_LastMouse = vec2(0, 0);
	vec2 m_Center = vec2(0, 0);
	vec2 m_ParallaxOrigin = vec2(0, 0);
	float m_Zoom = 4.0f;
	CUIRect m_aPanes[2]{};
	int m_SelectedEntry = -1;
	int m_SelectedDetail = -1;
	int m_FocusEntry = -1;
	struct CDetail
	{
		int m_Entry;
		int m_Kind; // 0: property, 1: tile, 2: object field, 3: issue, 4: order/structure
		int m_Index;
	};
	std::vector<CDetail> m_vDetails;
	std::vector<MapDiff::CPropertyChange> m_vValueRows;
	int m_ValueRowsEntry = -2;
	int m_ValueRowsDetail = -2;
	std::vector<int> m_vNavigator;
	std::vector<int> m_vChangedNavigator;
	CListBox m_Navigator;
	CListBox m_Details;
	CScrollRegion m_Values;
	CListBox m_aPairLists[2];
	char m_aButtonIds[15]{};
	bool ValidSources(const CEditor &Editor) const;
	void Start(CEditor &Editor);
	void CancelWork();
	void ReleaseBuffers();
	vec2 ProjectionCenter(const CLayerGroup &Group) const;
	void RebuildNavigator(bool KeepSelection = false);
	bool LayerVisible(int Entry) const;
	void RevealLayerSelection();
	void RenderLayerTree(CEditor &Editor, CUIRect View);
	void Select(int Entry);
	void RebuildDetails();
	bool IsDifference(const CDetail &Detail) const;
	std::vector<MapDiff::CPropertyChange> DetailValues(const CDetail &Detail) const;
	void Navigate(int Direction);
	void FitSelection();
	void RenderSelector(CEditor &Editor, CUIRect View);
	void RenderPane(CEditor &Editor, CUIRect Pane, int Side);
	void RenderInspector(CEditor &Editor, CUIRect View);

public:
	CMapCompare();
	~CMapCompare();
	bool Active() const { return m_State != EState::CLOSED; }
	void OpenSelector(CEditor &Editor);
	void Open(CEditor &Editor, const CEditorMap *pBefore, const CEditorMap *pAfter);
	void SourceClosing(const CEditorMap *pSource);
	void Invalidate(const char *pReason);
	void Close();
	void OnInput(CEditor &Editor, const IInput::CEvent &Event);
	void OnUpdate(CEditor &Editor);
	void Render(CEditor &Editor);
	// Called after input was cleared and UI rendering finished, so closing
	// input never reaches editor handlers, even in the closing frame.
	void EndFrame(CEditor &Editor);
};
#endif
