#ifndef GAME_EDITOR_MAP_COMPARE_H
#define GAME_EDITOR_MAP_COMPARE_H

#include "map_diff.h"

#include <engine/input.h>

#include <game/client/ui_listbox.h>

#include <atomic>
#include <future>
#include <map>
#include <memory>

class CEditor;
class CEditorMap;
class CLayerGroup;
class CMapCompareTabs;

class CMapCompare
{
	friend class CMapCompareTabs;
	friend class CEditor;
	CMapCompareTabs *m_pOwner = nullptr;
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
	uint64_t m_aRevisions[2]{};
	bool m_HasResult = false;
	bool m_Selector = false;
	// Weak ownership, rather than numeric indices or addresses, survives reordering
	// without confusing a replacement object with the selected object.
	struct CBookmark
	{
		MapDiff::EEntryKind m_Kind = MapDiff::EEntryKind::INFO;
		std::weak_ptr<void> m_apObjects[2];
		bool m_Present = false;
	};
	std::vector<CBookmark> m_vBookmarks;
	struct CSectionState
	{
		int m_Entry = -1;
		int m_Detail = -1;
		vec2 m_Center = vec2(0, 0);
		float m_Zoom = 4;
		bool m_ByGroup = false;
	};
	struct CDetailBookmark
	{
		int m_Kind = -1;
		int m_TileX = 0, m_TileY = 0;
		std::string m_Property;
		CBookmark m_Entry;
	};
	struct CSavedView
	{
		CBookmark m_Selection;
		CBookmark m_Focus;
		CBookmark m_aSections[4];
		CSectionState m_aStates[4];
		CListBox m_aNavigators[4];
		CListBox m_Details;
		CScrollRegion m_Settings, m_Layers, m_Values;
		CDetailBookmark m_Detail;
		CDetailBookmark m_aSectionDetails[4];
		std::vector<std::pair<CBookmark, std::pair<bool, bool>>> m_vLayers;
	};
	CSavedView m_SavedView;
	char m_TabSelectButtonId{};
	char m_TabCloseButtonId{};
	struct CRenderCache;
	std::unique_ptr<CRenderCache> m_pRenderCache;
	struct CObjectViewCache;
	std::unique_ptr<CObjectViewCache> m_pObjectViewCache;
	CScrollRegion m_Settings;
	bool m_ScrollToSetting = false;
	bool m_FitAfterSettings = false;
	int m_EnvelopeChannels = 15;
	char m_aChannelIds[2][4]{};
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
	enum
	{
		SECTION_CHANGES,
		SECTION_LAYERS,
		SECTION_ENVELOPES,
		SECTION_SETTINGS,
		NUM_SECTIONS,
	};
	int m_Section = SECTION_CHANGES;
	float m_SidebarWidth = 225.0f;
	CUIRect m_Divider{}; // Stable address for the native drag bar's UI identity.
	CSectionState m_aSectionStates[NUM_SECTIONS];
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
	CListBox m_aNavigators[NUM_SECTIONS];
	CListBox &NavigatorList() { return m_aNavigators[m_Section]; }
	CListBox m_Details;
	CScrollRegion m_Values;
	CListBox m_aPairLists[2];
	std::map<const CEditorMap *, char> m_aSelectorIds[2];
	char m_aButtonIds[17]{};
	bool ValidSources(const CEditor &Editor) const;
	bool Current(const CEditor &Editor) const;
	CBookmark Bookmark(const MapDiff::CEntry &Entry) const;
	CDetailBookmark DetailBookmark(int Detail) const;
	void SaveView();
	void RestoreView();
	void Start(CEditor &Editor);
	void CancelWork();
	void ReleaseBuffers();
	vec2 ProjectionCenter(const CLayerGroup &Group, bool Inspection = false) const;
	bool InSection(const MapDiff::CEntry &Entry) const;
	void SetSection(int Section);
	void RebuildNavigator(bool KeepSelection = false);
	bool LayerVisible(int Entry) const;
	void RevealLayerSelection();
	void RenderLayerTree(CEditor &Editor, CUIRect View);
	void Select(int Entry);
	void SelectDetail(int Detail);
	bool MapView() const;
	void RebuildDetails();
	bool IsDifference(const CDetail &Detail) const;
	std::vector<MapDiff::CPropertyChange> DetailValues(const CDetail &Detail) const;
	void Navigate(int Direction);
	void FitSelection();
	void RenderSelector(CEditor &Editor, CUIRect View);
	void RenderPane(CEditor &Editor, CUIRect Pane, int Side);
	void RenderInspector(CEditor &Editor, CUIRect View);
	void RenderSettings(CEditor &Editor, CUIRect View);
	void RenderEnvelope(CEditor &Editor, CUIRect Pane, int Side);

public:
	CMapCompare();
	~CMapCompare();
	bool Active() const { return m_State != EState::CLOSED; }
	void OpenSelector(CEditor &Editor);
	void Open(CEditor &Editor, const CEditorMap *pBefore, const CEditorMap *pAfter);
	void Invalidate(const char *pReason);
	void Close();
	void OnInput(CEditor &Editor, const IInput::CEvent &Event);
	void OnUpdate(CEditor &Editor);
	void Render(CEditor &Editor, CUIRect View);
};

// Stable viewer ownership and a single worker shared by all comparison tabs.
// Closed history contains only identities and inspection preferences.
class CMapCompareTabs
{
	friend class CEditor;
	friend class CMapCompare;
	std::vector<std::unique_ptr<CMapCompare>> m_vpDocuments;
	CMapCompare *m_pSelected = nullptr;
	CMapCompare *m_pPending = nullptr;
	const CEditorMap *m_pPendingMap = nullptr;
	const CEditorMap *m_pCloseMap = nullptr;
	bool m_Transition = false;
	std::future<MapDiff::CResult> m_RetiredWork;
	struct CClosed
	{
		const CEditorMap *m_apSources[2];
		CMapCompare::CSavedView m_View;
		vec2 m_Center, m_Origin;
		float m_Zoom, m_SidebarWidth;
		int m_Context, m_Section, m_EnvelopeChannels;
		bool m_ByGroup, m_UnchangedExpanded, m_HasResult, m_LayerTreeActive;
	};
	std::vector<CClosed> m_vClosed;
	void Retire(CMapCompare &Document);
	void Select(CEditor &Editor, CMapCompare *pDocument);
	void Dispose(CEditor &Editor, CMapCompare *pDocument, bool Remember);

public:
	~CMapCompareTabs();
	bool Active() const { return m_pSelected != nullptr; }
	bool Transitioning() const { return m_Transition; }
	CMapCompare *Selected() const { return m_pSelected; }
	void OpenSelector(CEditor &Editor);
	void Open(CEditor &Editor, const CEditorMap *pBefore, const CEditorMap *pAfter);
	void SelectMap(CEditor &Editor, const CEditorMap *pMap);
	void CloseMap(CEditor &Editor, const CEditorMap *pMap);
	void SourceClosing(CEditor &Editor, const CEditorMap *pSource);
	bool CanReopen() const { return !m_vClosed.empty(); }
	void Reopen(CEditor &Editor);
	void OnUpdate(CEditor &Editor);
	void OnInput(CEditor &Editor, const IInput::CEvent &Event);
	void Render(CEditor &Editor, CUIRect View);
	void EndFrame(CEditor &Editor);
};
#endif
