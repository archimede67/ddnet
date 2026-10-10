#include <base/io.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/client/graphics_threaded.h>
#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/editor/editor.h>
#include <game/editor/map_diff_snapshot.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_front.h>
#include <game/editor/mapitems/layer_game.h>
#include <game/editor/mapitems/layer_quads.h>
#include <game/editor/mapitems/layer_sounds.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_tune.h>
#include <game/editor/mapitems/sound.h>

#include <SDL.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <tuple>

// Explicit instantiation permits test-only access without modifying product headers.
template<class Tag, typename Tag::Type Member>
struct CAccess
{
	friend typename Tag::Type Access(Tag) { return Member; }
};
#define ACCESSOR(Tag, Class, Member, ValueType) \
	struct Tag \
	{ \
		using Type = ValueType Class::*; \
		friend Type Access(Tag); \
	}; \
	template struct CAccess<Tag, &Class::Member>;
ACCESSOR(TCompare, CEditor, m_MapCompare, CMapCompareTabs)
ACCESSOR(TPendingCompare, CMapCompareTabs, m_pPending, CMapCompare *)
ACCESSOR(TCompareDocuments, CMapCompareTabs, m_vpDocuments, std::vector<std::unique_ptr<CMapCompare>>)
ACCESSOR(TTabScroll, CEditor, m_MapTabsScrollRegion, CScrollRegion)
using TRevisionArray = uint64_t[2];
ACCESSOR(TCapturedRevisions, CMapCompare, m_aRevisions, TRevisionArray)
ACCESSOR(TWorker, CMapCompare, m_Future, std::future<MapDiff::CResult>)
ACCESSOR(TRetiredWorker, CMapCompareTabs, m_RetiredWork, std::future<MapDiff::CResult>)
ACCESSOR(TMaps, CEditor, m_vpMaps, std::vector<std::unique_ptr<CEditorMap>>)
ACCESSOR(TSelectedMap, CEditor, m_SelectedMap, size_t)
ACCESSOR(TResult, CMapCompare, m_Result, MapDiff::CResult)
ACCESSOR(TContext, CMapCompare, m_Context, int)
ACCESSOR(TUnchangedExpanded, CMapCompare, m_UnchangedExpanded, bool)
ACCESSOR(TSection, CMapCompare, m_Section, int)
ACCESSOR(TSidebarWidth, CMapCompare, m_SidebarWidth, float)
ACCESSOR(TDivider, CMapCompare, m_Divider, CUIRect)
ACCESSOR(TPanning, CMapCompare, m_Panning, bool)
ACCESSOR(TToolboxWidth, CEditor, m_ToolBoxWidth, float)
ACCESSOR(TPrompt, CEditor, m_Prompt, CPrompt)
ACCESSOR(TPromptResults, CPrompt, m_vpFilteredPromptList, std::vector<CQuickAction *>)
ACCESSOR(TFileBrowser, CEditor, m_FileBrowser, CFileBrowser)
ACCESSOR(TFileFilter, CFileBrowser, m_FilterInput, CLineInputBuffered<IO_MAX_PATH_LENGTH>)
ACCESSOR(TFileList, CFileBrowser, m_ListBox, CListBox)
ACCESSOR(TFileSelection, CFileBrowser, m_SelectedFileIndex, int)
int CursorType(CEditor &);
template<auto Member>
struct CCursorAccess
{
	friend int CursorType(CEditor &Obj) { return static_cast<int>(Obj.*Member); }
};
template struct CCursorAccess<&CEditor::m_CursorType>;
ACCESSOR(TLayerTree, CMapCompare, m_LayerTree, CScrollRegion)
ACCESSOR(TTreeActive, CMapCompare, m_LayerTreeActive, bool)
using TNavArray = CListBox[4];
ACCESSOR(TNavLists, CMapCompare, m_aNavigators, TNavArray)
using TPairListArray = CListBox[2];
ACCESSOR(TPairLists, CMapCompare, m_aPairLists, TPairListArray)
ACCESSOR(TEnvelopeEditor, CEditor, m_EnvelopeEditor, CEnvelopeEditor)
struct TEnvelopeX
{
	using Type = float (CEnvelopeEditor::*)(const CUIRect &, float) const;
	friend Type Access(TEnvelopeX);
};
template struct CAccess<TEnvelopeX, &CEnvelopeEditor::EnvelopeToScreenX>;
struct TEnvelopeY
{
	using Type = float (CEnvelopeEditor::*)(const CUIRect &, float) const;
	friend Type Access(TEnvelopeY);
};
template struct CAccess<TEnvelopeY, &CEnvelopeEditor::EnvelopeToScreenY>;
ACCESSOR(TListScroll, CListBox, m_ScrollRegion, CScrollRegion)
ACCESSOR(TScrollPos, CScrollRegion, m_ScrollPos, float)
ACCESSOR(TScrollOffset, CScrollRegion, m_ContentScrollOffset, float)
ACCESSOR(TScrollRect, CScrollRegion, m_ContentAreaRect, CUIRect)
ACCESSOR(TChangedNavigator, CMapCompare, m_vChangedNavigator, std::vector<int>)
ACCESSOR(TGroup, CMapCompare, m_ByGroup, bool)
ACCESSOR(TSelectedEntry, CMapCompare, m_SelectedEntry, int)
ACCESSOR(TSelectedDetail, CMapCompare, m_SelectedDetail, int)
ACCESSOR(TFocusEntry, CMapCompare, m_FocusEntry, int)
ACCESSOR(TNavigator, CMapCompare, m_vNavigator, std::vector<int>)
ACCESSOR(TCenter, CMapCompare, m_Center, vec2)
ACCESSOR(TOrigin, CMapCompare, m_ParallaxOrigin, vec2)
ACCESSOR(TZoom, CMapCompare, m_Zoom, float)
ACCESSOR(TValues, CMapCompare, m_Values, CScrollRegion)
ACCESSOR(TSettings, CMapCompare, m_Settings, CScrollRegion)
ACCESSOR(TDetails, CMapCompare, m_Details, CListBox)
ACCESSOR(TEnvelopeChannels, CMapCompare, m_EnvelopeChannels, int)
ACCESSOR(TContentSize, CScrollRegion, m_ContentSize, float)
ACCESSOR(TValueRows, CMapCompare, m_vValueRows, std::vector<MapDiff::CPropertyChange>)
ACCESSOR(TCommandBuffer, CGraphics_Threaded, m_pCommandBuffer, CCommandBuffer *)
ACCESSOR(TTileBuffering, CGraphics_Threaded, m_GLTileBufferingEnabled, bool)
using TPaneArray = CUIRect[2];
ACCESSOR(TPanes, CMapCompare, m_aPanes, TPaneArray)
struct TFit
{
	using Type = void (CMapCompare::*)();
	friend Type Access(TFit);
};
template struct CAccess<TFit, &CMapCompare::FitSelection>;
struct TRenderPane
{
	using Type = void (CMapCompare::*)(CEditor &, CUIRect, int);
	friend Type Access(TRenderPane);
};
template struct CAccess<TRenderPane, &CMapCompare::RenderPane>;
bool HasSnapshot(CMapCompare &);
const void *SnapshotIdentity(CMapCompare &);
const MapDiff::CSnapshot &CapturedSnapshot(CMapCompare &, int);
template<auto Member>
struct CDataAccess
{
	friend bool HasSnapshot(CMapCompare &Obj) { return static_cast<bool>(Obj.*Member); }
	friend const void *SnapshotIdentity(CMapCompare &Obj) { return (Obj.*Member).get(); }
	friend const MapDiff::CSnapshot &CapturedSnapshot(CMapCompare &Obj, int Side) { return (Obj.*Member)->m_aSnapshots[Side]; }
};
template struct CDataAccess<&CMapCompare::m_pData>;
bool HasRenderCache(CMapCompare &);
template<auto Member>
struct CRenderCacheAccess
{
	friend bool HasRenderCache(CMapCompare &Obj) { return static_cast<bool>(Obj.*Member); }
};
template struct CRenderCacheAccess<&CMapCompare::m_pRenderCache>;

using TSourceArray = const CEditorMap *[2];
ACCESSOR(TSources, CMapCompare, m_apSources, TSourceArray)
struct TSelect
{
	using Type = void (CMapCompare::*)(int);
	friend Type Access(TSelect);
};
template struct CAccess<TSelect, &CMapCompare::Select>;
struct TSetSection
{
	using Type = void (CMapCompare::*)(int);
	friend Type Access(TSetSection);
};
template struct CAccess<TSetSection, &CMapCompare::SetSection>;
struct TSelectDetail
{
	using Type = void (CMapCompare::*)(int);
	friend Type Access(TSelectDetail);
};
template struct CAccess<TSelectDetail, &CMapCompare::SelectDetail>;
int CompareState(CMapCompare &);
template<auto Member>
struct CStateAccess
{
	friend int CompareState(CMapCompare &Obj) { return static_cast<int>(Obj.*Member); }
};
template struct CStateAccess<&CMapCompare::m_State>;
std::array<int, 3> SelectedDetail(CMapCompare &);
template<auto Member>
struct CDetailAccess
{
	friend std::array<int, 3> SelectedDetail(CMapCompare &Obj)
	{
		const int Index = Obj.*Access(TSelectedDetail{});
		if(Index < 0)
			return {-1, -1, -1};
		const auto &Detail = (Obj.*Member)[Index];
		return {Detail.m_Entry, Detail.m_Kind, Detail.m_Index};
	}
};
template struct CDetailAccess<&CMapCompare::m_vDetails>;
std::tuple<bool, bool, size_t> LayerState(CMapCompare &, int);
void SetLayerVisible(CMapCompare &, int, bool);
template<auto Member>
struct CLayerStateAccess
{
	friend void SetLayerVisible(CMapCompare &Obj, int Index, bool Visible) { (Obj.*Member)[Index].m_Visible = Visible; }
	friend std::tuple<bool, bool, size_t> LayerState(CMapCompare &Obj, int Index)
	{
		const auto &State = (Obj.*Member)[Index];
		return {State.m_Visible, State.m_Expanded, State.m_Changes};
	}
};
template struct CLayerStateAccess<&CMapCompare::m_vLayerStates>;

size_t ClosedComparisonCount(CMapCompareTabs &);
template<auto Member>
struct CClosedComparisonAccess
{
	friend size_t ClosedComparisonCount(CMapCompareTabs &Tabs) { return (Tabs.*Member).size(); }
};
template struct CClosedComparisonAccess<&CMapCompareTabs::m_vClosed>;

class CSmokeAction : public IEditorAction
{
public:
	explicit CSmokeAction(CEditorMap *pMap) :
		IEditorAction(pMap) { str_copy(m_aDisplayText, "Smoke undo sentinel"); }
	void Undo() override { str_copy(Map()->m_MapInfo.m_aAuthor, "UNEXPECTED UNDO"); }
	void Redo() override { str_copy(Map()->m_MapInfo.m_aAuthor, "UNEXPECTED REDO"); }
};

class CSmokeEditor : public CEditor
{
	std::vector<std::function<bool()>> m_vSteps;
	size_t m_Step = 0;
	int m_Frames = 0;
	int m_StepFrames = 0;
	int m_Failures = 0;
	int m_SavedEntry = -1;
	bool m_MeasureRender = false;
	int m_TargetContext = 0;
	uint64_t m_CaptureCount = 0;
	double m_LastRenderMs = 0;
	vec2 m_SavedCenter;
	float m_SavedScroll = 0;
	std::vector<bool> m_aVisibility[2];
	std::vector<std::shared_ptr<CLayerTiles>> m_vPixelLayers;
	CEditorMap *m_pBefore = nullptr, *m_pAfter = nullptr;
	MapDiff::CSnapshot m_aSaved[2];
	struct CContext
	{
		bool m_Modified, m_ModifiedAuto;
		int m_Group;
		std::vector<int> m_Layers, m_Quads;
		vec2 m_World, m_Editor;
		float m_Zoom, m_Anim;
		size_t m_Undo, m_Redo;
		uint64_t m_Revision;
	} m_aContexts[2];
	std::shared_ptr<CLayerGroup> m_pSavedBrush;
	std::vector<int> m_SavedBrushCells;
	std::ofstream m_Log;
	CMapCompare m_ClosedCompare;
	CMapCompareTabs &Tabs() { return this->*Access(TCompare{}); }
	CMapCompare &Compare()
	{
		auto *p = Tabs().Selected();
		if(!p)
			p = Tabs().*Access(TPendingCompare{});
		return p ? *p : m_ClosedCompare;
	}
	void OpenComparison(CEditor &, const CEditorMap *pBefore, const CEditorMap *pAfter)
	{
		const bool Group = Compare().*Access(TGroup{});
		const float Width = Compare().*Access(TSidebarWidth{});
		Tabs().Open(*this, pBefore, pAfter);
		auto *pDocument = Tabs().*Access(TPendingCompare{});
		if(!pDocument)
			pDocument = Tabs().Selected();
		if(pDocument)
		{
			pDocument->*Access(TGroup{}) = Group;
			pDocument->*Access(TSidebarWidth{}) = Width;
		}
	}
	vec2 SectionPosition(int Section)
	{
		const float Width = Compare().*Access(TSidebarWidth{});
		if(Width < 180)
			return vec2(6 + (Width - 4) / 2, 104 + 24 * Section + 12);
		return vec2(6 + (Width - 4) * (Section % 2 ? 0.75f : 0.25f), 104 + 24 * (Section / 2) + 12);
	}
	float NavigatorTop() { return 104 + ((Compare().*Access(TSidebarWidth{})) < 180 ? 96 : 48); }
	void CloseComparison()
	{
		if(Tabs().Selected())
			Tabs().Selected()->Close();
		else if(auto *p = Tabs().*Access(TPendingCompare{}))
			p->Close();
	}
	auto &Maps() { return this->*Access(TMaps{}); }
	int State() { return CompareState(Compare()); }
	const MapDiff::CResult &Result() { return Compare().*Access(TResult{}); }
	void Check(bool Ok, const std::string &What)
	{
		m_Log << (Ok ? "PASS " : "FAIL ") << What << std::endl;
		log_info("compare-smoke", "%s %s", Ok ? "PASS" : "FAIL", What.c_str());
		if(!Ok)
			++m_Failures;
	}
	void Do(std::function<void()> Fn)
	{
		m_vSteps.push_back([Fn] { Fn(); return true; });
	}
	void Delay(int Frames = 4)
	{
		m_vSteps.push_back([Frames, i = 0]() mutable { return ++i >= Frames; });
	}
	void WaitReady()
	{
		m_vSteps.push_back([this] { return !Tabs().Transitioning() && State() == 4; });
	}
	void Mouse(int Button, bool Down)
	{
		SDL_Event E{};
		E.type = Down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		E.button.button = Button;
		E.button.state = Down ? SDL_PRESSED : SDL_RELEASED;
		SDL_PushEvent(&E);
	}
	void Key(SDL_Keycode Key, bool Down)
	{
		SDL_Event E{};
		E.type = Down ? SDL_KEYDOWN : SDL_KEYUP;
		E.key.keysym.sym = Key;
		E.key.keysym.scancode = SDL_GetScancodeFromKey(Key);
		E.key.state = Down ? SDL_PRESSED : SDL_RELEASED;
		SDL_PushEvent(&E);
	}
	void TypeText(const char *pText)
	{
		Do([Text = std::string(pText)] { SDL_Event Event{}; Event.type = SDL_TEXTINPUT; str_copy(Event.text.text, Text.c_str()); SDL_PushEvent(&Event); });
		Delay();
	}
	void Click(std::function<vec2()> Pos, int Button = SDL_BUTTON_LEFT)
	{
		auto pTarget = std::make_shared<std::pair<vec2, const void *>>();
		const int MouseIndex = Button == SDL_BUTTON_RIGHT ? 1 : Button == SDL_BUTTON_MIDDLE ? 2 :
												      0;
		Do([this, Pos, pTarget] { const auto P=Pos(); pTarget->first=P; Ui()->OnCursorMove((P.x-Ui()->MouseX())*Graphics()->WindowWidth()/Ui()->Screen()->w, (P.y-Ui()->MouseY())*Graphics()->WindowHeight()/Ui()->Screen()->h); });
		Delay();
		Do([this, Button, pTarget] {
			char aCursorCheck[192];
			str_format(aCursorCheck, sizeof(aCursorCheck), "scripted click cursor stays on its target (actual %.2f,%.2f target %.2f,%.2f)", Ui()->MouseX(), Ui()->MouseY(), pTarget->first.x, pTarget->first.y);
			Check(distance(Ui()->MousePos(), pTarget->first) < 0.1f, aCursorCheck);
			pTarget->second = Ui()->HotItem();
			Mouse(Button, true);
		});
		Delay();
		Do([this, Button, MouseIndex, pTarget] { Check(Input()->KeyIsPressed(KEY_MOUSE_1 + MouseIndex) && Ui()->MouseButton(MouseIndex), "scripted mouse press reaches native input and UI"); Check(!pTarget->second || Ui()->ActiveItem() == pTarget->second, "scripted mouse press activates the hovered target"); Mouse(Button, false); });
		Delay();
	}
	void DoubleClick(std::function<vec2()> Pos)
	{
		// The editor uses a real-time 0.5 s window. Frame-count expiry and two
		// full Click helpers can exceed it on slow frames and leave a tree folded.
		m_vSteps.push_back([this, Start = -1.0f]() mutable {
			if(Start < 0)
				Start = Client()->GlobalTime();
			return Client()->GlobalTime() - Start > 0.55f;
		});
		Do([this, Pos] { const auto P = Pos(); Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h); });
		Delay(2); // Establish hover once; keep both complete clicks close together.
		auto pTarget = std::make_shared<const void *>();
		auto pFirstRelease = std::make_shared<float>();
		for(int ClickIndex = 0; ClickIndex < 2; ++ClickIndex)
		{
			Do([this, pTarget, ClickIndex] { if(ClickIndex == 0) *pTarget = Ui()->HotItem(); Check(*pTarget && Ui()->HotItem() == *pTarget, "double-click retains the same hovered target"); Mouse(SDL_BUTTON_LEFT, true); });
			Delay(1);
			Do([this, pTarget] { Check(Input()->KeyIsPressed(KEY_MOUSE_1) && Ui()->MouseButton(0) && Ui()->ActiveItem() == *pTarget, "double-click press reaches its native UI target"); Mouse(SDL_BUTTON_LEFT, false); });
			Delay(1);
			Do([this, pFirstRelease, ClickIndex] { Check(!Input()->KeyIsPressed(KEY_MOUSE_1) && !Ui()->MouseButton(0), "double-click release reaches native input and UI"); if(ClickIndex == 0) *pFirstRelease = Client()->GlobalTime(); else Check(Client()->GlobalTime() - *pFirstRelease < 0.5f, "double-click releases fit the editor time window"); });
		}
	}
	void Screenshot(const std::string &Name)
	{
		Do([this, Name] { CImageInfo Image; static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image); const std::string Path="screenshots/"+Name+".png"; Check(Image.m_pData && CImageLoader::SavePng(Storage()->OpenFile(Path.c_str(),IOFLAG_WRITE,IStorage::TYPE_SAVE),Path.c_str(),Image),"render screenshot "+Name); Image.Free(); });
		Delay();
	}
	void ToggleMapContext()
	{
		Do([this] {
			m_TargetContext = (Compare().*Access(TContext{})) == 0 ? 2 : 0;
			Ui()->OnCursorMove((240 - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (62 - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
		});
		Delay();
		for(int i = 0; i < 2; ++i)
		{
			Do([this] { if((Compare().*Access(TContext{})) != m_TargetContext) Mouse(SDL_BUTTON_LEFT, true); });
			Delay();
			Do([this] { Mouse(SDL_BUTTON_LEFT, false); });
			Delay();
		}
	}
	void CaptureSources()
	{
		for(int i = 0; i < 2; ++i)
		{
			auto *p = i ? m_pAfter : m_pBefore;
			m_aVisibility[i].clear();
			for(const auto &pGroup : p->m_vpGroups)
			{
				m_aVisibility[i].push_back(pGroup->m_Visible);
				for(const auto &pLayer : pGroup->m_vpLayers)
					m_aVisibility[i].push_back(pLayer->m_Visible);
			}
			Check(MapDiff::Capture(*p, m_aSaved[i]), "capture source");
			m_aContexts[i] = {p->m_Modified, p->m_ModifiedAuto, p->m_SelectedGroup, p->m_vSelectedLayers, p->m_vSelectedQuads, p->m_MapViewState.m_WorldOffset, p->m_MapViewState.m_EditorOffset, p->m_MapViewState.m_WorldZoom, p->m_EnvelopeEvaluator.m_AnimateTime, p->m_EditorHistory.m_vpUndoActions.size(), p->m_EditorHistory.m_vpRedoActions.size(), p->m_ContentRevision};
		}
		m_pSavedBrush = m_pBrush;
		m_SavedBrushCells.clear();
		for(const auto &pLayer : m_pBrush->m_vpLayers)
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const auto &T = static_cast<const CLayerTiles &>(*pLayer);
				for(int i = 0; i < T.m_Width * T.m_Height; ++i)
					m_SavedBrushCells.push_back(T.m_pTiles[i].m_Index);
			}
	}
	void CheckSources(const std::string &Label)
	{
		for(int i = 0; i < 2; ++i)
		{
			auto *p = i ? m_pAfter : m_pBefore;
			std::vector<bool> Visibility;
			for(const auto &pGroup : p->m_vpGroups)
			{
				Visibility.push_back(pGroup->m_Visible);
				for(const auto &pLayer : pGroup->m_vpLayers)
					Visibility.push_back(pLayer->m_Visible);
			}
			Check(Visibility == m_aVisibility[i], Label + " source visibility " + std::to_string(i));
			MapDiff::CSnapshot Now;
			Check(MapDiff::Capture(*p, Now) && MapDiff::Compare(m_aSaved[i], Now).ChangeCount() == 0, Label + " source contents " + std::to_string(i));
			const auto &C = m_aContexts[i];
			if(p->m_MapViewState.m_WorldZoom != C.m_Zoom)
				m_Log << "CONTEXT " << Label << " side=" << i << " world_zoom=" << p->m_MapViewState.m_WorldZoom << " expected=" << C.m_Zoom << " smooth_zoom=" << p->m_MapViewState.m_Zoom.GetValue() << std::endl;
			Check(p->m_Modified == C.m_Modified && p->m_ModifiedAuto == C.m_ModifiedAuto && p->m_SelectedGroup == C.m_Group && p->m_vSelectedLayers == C.m_Layers && p->m_vSelectedQuads == C.m_Quads && p->m_MapViewState.m_WorldOffset == C.m_World && p->m_MapViewState.m_EditorOffset == C.m_Editor && p->m_MapViewState.m_WorldZoom == C.m_Zoom && p->m_EnvelopeEvaluator.m_AnimateTime == C.m_Anim && p->m_EditorHistory.m_vpUndoActions.size() == C.m_Undo && p->m_EditorHistory.m_vpRedoActions.size() == C.m_Redo, Label + " editor context " + std::to_string(i));
		}
		Check(m_pBrush == m_pSavedBrush, Label + " brush identity");
		std::vector<int> Cells;
		for(const auto &pLayer : m_pBrush->m_vpLayers)
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const auto &T = static_cast<const CLayerTiles &>(*pLayer);
				for(int n = 0; n < T.m_Width * T.m_Height; ++n)
					Cells.push_back(T.m_pTiles[n].m_Index);
			}
		Check(m_pBefore->m_ContentRevision == m_aContexts[0].m_Revision && m_pAfter->m_ContentRevision == m_aContexts[1].m_Revision, Label + " source content revisions");
		Check(Cells == m_SavedBrushCells, Label + " nonempty brush contents");
	}
	void LoadPair(const std::string &Name)
	{
		Compare().*Access(TGroup{}) = false;
		Check(!Compare().Active(), "previous comparison closed");
		Check(Load(("maps/" + Name + ".map").c_str(), IStorage::TYPE_ALL), "load Before " + Name);
		m_pBefore = Map();
		Check(Load(("maps/" + Name + ".map").c_str(), IStorage::TYPE_ALL), "load After " + Name);
		m_pAfter = Map();
		this->*Access(TSelectedMap{}) = Maps().size() - 2;
		MapDiff::CSnapshot A, B;
		const auto CaptureStart = std::chrono::steady_clock::now();
		MapDiff::Capture(*m_pBefore, A);
		MapDiff::Capture(*m_pAfter, B);
		const auto CompareStart = std::chrono::steady_clock::now();
		const auto Diff = MapDiff::Compare(A, B);
		const auto Finish = std::chrono::steady_clock::now();
		m_Log << "TIMING " << Name << " capture_pair_ms=" << std::chrono::duration<double, std::milli>(CompareStart - CaptureStart).count() << " compare_ms=" << std::chrono::duration<double, std::milli>(Finish - CompareStart).count() << std::endl;
		Check(Diff.ChangeCount() == 0, "timed identity comparison " + Name);
	}
	void Mutate()
	{
		auto *p = m_pAfter;
		str_copy(p->m_MapInfo.m_aAuthor, "Comparison smoke");
		CEditorMapSetting Setting("sv_test_cmds 1");
		p->m_vSettings.push_back(Setting);
		p->m_vSettings.push_back(Setting);
		p->m_pGameLayer->m_pTiles[2 + 2 * p->m_pGameLayer->m_Width].m_Index = TILE_FREEZE;
		if(p->m_pTeleLayer)
			for(int n = 0; n < p->m_pTeleLayer->m_Width * p->m_pTeleLayer->m_Height; ++n)
				if(p->m_pTeleLayer->m_pTeleTile[n].m_Type)
				{
					++p->m_pTeleLayer->m_pTeleTile[n].m_Number;
					break;
				}
		auto Group = p->NewGroup();
		str_copy(Group->m_aName, "Smoke");
		Group->m_OffsetX = 40;
		Group->m_ParallaxX = 75;
		Group->m_UseClipping = 1;
		Group->m_ClipX = 0;
		Group->m_ClipY = 0;
		Group->m_ClipW = 350;
		Group->m_ClipH = 300;
		auto Tiles = std::make_shared<CLayerTiles>(p, 12, 10);
		str_copy(Tiles->m_aName, "Added tiles");
		Tiles->m_pTiles[2 + 2 * 12].m_Index = 1;
		if(!p->m_vpImages.empty())
			Tiles->m_Image = 0;
		Group->AddLayer(Tiles);
		auto Quads = std::make_shared<CLayerQuads>(p);
		str_copy(Quads->m_aName, "Added quad");
		Quads->NewQuad(150, 150, 100, 80);
		if(!p->m_vpImages.empty())
			Quads->m_Image = 0;
		Group->AddLayer(Quads);
		if(!p->m_vpImages.empty())
			str_copy(p->m_vpImages[0]->m_aName, "smoke_image");
		p->OnModify();
		p->m_EditorHistory.RecordAction(std::make_shared<CSmokeAction>(p));
		auto Brush = std::make_shared<CLayerTiles>(m_pBrush->Map(), 2, 2);
		Brush->m_pTiles[0].m_Index = TILE_FREEZE;
		m_pBrush->m_vpLayers = {Brush};
	}
	void SelectKind(MapDiff::EEntryKind Kind, const std::string &Name = "")
	{
		if(Kind == MapDiff::EEntryKind::SETTINGS || Kind == MapDiff::EEntryKind::ENVELOPE || (Compare().*Access(TSection{})) >= 2)
			(Compare().*Access(TSetSection{}))(Kind == MapDiff::EEntryKind::SETTINGS ? 3 : Kind == MapDiff::EEntryKind::ENVELOPE ? 2 :
																	       0);
		for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
			if(Result().m_vEntries[i].m_Kind == Kind && (Name.empty() || Result().m_vEntries[i].m_Name == Name) && Result().m_vEntries[i].Changed())
			{
				(Compare().*Access(TSelect{}))(i);
				return;
			}
		Check(false, "find changed entry " + Name);
	}

	std::shared_ptr<CEditorImage> WhiteImage(CEditorMap &Map, const char *pName, bool Upload)
	{
		auto Image = std::make_shared<CEditorImage>(&Map);
		Image->m_Width = Image->m_Height = 256;
		Image->m_Format = CImageInfo::FORMAT_RGBA;
		Image->Allocate();
		std::memset(Image->m_pData, 255, Image->DataSize());
		str_copy(Image->m_aName, pName);
		if(Upload)
			Image->m_Texture = Graphics()->LoadTextureRaw(*Image, Graphics()->TextureLoadFlags(), pName);
		Map.m_vpImages.push_back(Image);
		return Image;
	}
	void SnapshotChecks()
	{
		CEditorMap Map(this);
		Map.CreateDefault();
		const int W = Map.m_pGameLayer->m_Width, H = Map.m_pGameLayer->m_Height;
		auto Speed = std::make_shared<CLayerSpeedup>(&Map, W, H);
		Map.MakeSpeedupLayer(Speed);
		Map.m_pGameGroup->m_vpLayers.push_back(Speed);
		auto Tele = std::make_shared<CLayerTele>(&Map, W, H);
		Map.MakeTeleLayer(Tele);
		Map.m_pGameGroup->m_vpLayers.push_back(Tele);
		auto Switch = std::make_shared<CLayerSwitch>(&Map, W, H);
		Map.MakeSwitchLayer(Switch);
		Map.m_pGameGroup->m_vpLayers.push_back(Switch);
		auto Tune = std::make_shared<CLayerTune>(&Map, W, H);
		Map.MakeTuneLayer(Tune);
		Map.m_pGameGroup->m_vpLayers.push_back(Tune);
		Speed->m_pSpeedupTile[2 + 3 * W] = {12, 34, 28, 0, 90};
		Tele->m_pTeleTile[1] = {7, 26};
		Switch->m_pSwitchTile[1] = {8, 22, 0, 9};
		Tune->m_pTuneTile[1] = {10, 68};
		MapDiff::CSnapshot Before, After;
		MapDiff::Capture(Map, Before);
		Speed->m_pSpeedupTile[2 + 3 * W].m_Angle = -45;
		MapDiff::Capture(Map, After);
		auto Diff = MapDiff::Compare(Before, After);
		int Found = 0;
		for(const auto &Entry : Diff.m_vEntries)
			for(const auto &Tile : Entry.m_vTiles)
				if(Tile.m_X == 2 && Tile.m_Y == 3)
				{
					++Found;
					Check(Tile.m_Before.m_Angle == 90 && Tile.m_After.m_Angle == -45 && Tile.m_Before.m_Force == 12 && Tile.m_After.m_MaxSpeed == 34, "actual speedup angle-only payload");
				}
		Check(Found == 1, "actual speedup angle-only capture");
		Before = After;
		Speed->m_pTiles[0].m_Index = 123;
		Tele->m_pTiles[0].m_Index = 123;
		Switch->m_pTiles[0].m_Index = 123;
		Tune->m_pTiles[0].m_Index = 123;
		Speed->m_pSpeedupTile[2 + 3 * W].m_MustBe0 = 99;
		Map.m_pGameLayer->m_pTiles[0].m_Flags = TILEFLAG_OPAQUE;
		Map.m_pGameLayer->m_pTiles[0].m_Skip = 50;
		Map.m_pGameLayer->m_pTiles[0].m_MustBe0 = 22;
		Map.m_pGameLayer->m_Visible = false;
		Map.m_SelectedGroup = 1;
		MapDiff::Capture(Map, After);
		Check(MapDiff::Compare(Before, After).ChangeCount() == 0, "special backing arrays, derived flags, padding and editor state ignored");
		Tele->m_pTeleTile[1].m_Number = 17;
		Switch->m_pSwitchTile[1].m_Delay = 19;
		Tune->m_pTuneTile[1].m_Number = 20;
		MapDiff::Capture(Map, After);
		Diff = MapDiff::Compare(Before, After);
		Found = 0;
		for(const auto &Entry : Diff.m_vEntries)
			Found += Entry.m_vTiles.size();
		Check(Found == 3, "authoritative tele switch tune arrays captured");
		int OwnedOldValues = 0;
		for(const auto &Group : Before.m_vGroups)
			for(const auto &Layer : Group.m_vLayers)
			{
				if(Layer.m_Kind == MapDiff::ELayerKind::TELE)
					OwnedOldValues += Layer.m_vCells.size() == 1 && Layer.m_vCells[0].m_Tile.m_Number == 7;
				if(Layer.m_Kind == MapDiff::ELayerKind::SWITCH)
					OwnedOldValues += Layer.m_vCells.size() == 1 && Layer.m_vCells[0].m_Tile.m_Delay == 9;
				if(Layer.m_Kind == MapDiff::ELayerKind::TUNE)
					OwnedOldValues += Layer.m_vCells.size() == 1 && Layer.m_vCells[0].m_Tile.m_Number == 10;
			}
		Check(OwnedOldValues == 3, "owned snapshot retains exact special values after source edits");
		Map.m_vpImages.clear();
		WhiteImage(Map, "same pixels A", false);
		WhiteImage(Map, "same pixels B", false);
		auto Art = std::make_shared<CLayerTiles>(&Map, 4, 4);
		Art->m_Image = 0;
		Map.m_pGameGroup->m_vpLayers.push_back(Art);
		MapDiff::Capture(Map, Before);
		std::swap(Map.m_vpImages[0], Map.m_vpImages[1]);
		Art->m_Image = 1;
		MapDiff::Capture(Map, After);
		Check(MapDiff::Compare(Before, After).ChangeCount() == 0, "real resource renumbering preserves semantic references");
		Check(Before.m_vGroups.back().m_vLayers.back().m_vReferences[0].m_Index == 0 && After.m_vGroups.back().m_vLayers.back().m_vReferences[0].m_Index == 1, "actual resource reference indices preserved separately");
		auto SavedPixel = After.m_vImages[1].m_vData[0];
		Map.m_vpImages[1]->m_pData[0] = 0;
		Check(After.m_vImages[1].m_vData[0] == SavedPixel, "snapshot owns embedded pixel bytes");
		auto Sounds = std::make_shared<CLayerSounds>(&Map);
		auto *Source = Sounds->NewSource(4, 5);
		Source->m_Shape.m_Type = CSoundShape::SHAPE_CIRCLE;
		Map.m_pGameGroup->m_vpLayers.push_back(Sounds);
		auto Envelope = std::make_shared<CEnvelope>(CEnvelope::EType::SOUND);
		Envelope->AddPoint(CFixedTime(0), {1024, 0, 0, 0});
		Map.m_vpEnvelopes.push_back(Envelope);
		MapDiff::Capture(Map, Before);
		Source->m_Shape.m_Rectangle.m_Height = 999;
		Envelope->m_vPoints[0].m_aValues[3] = 99;
		Envelope->m_vPoints[0].m_Bezier.m_aOutTangentDeltaY[0] = 999;
		MapDiff::Capture(Map, After);
		Check(MapDiff::Compare(Before, After).ChangeCount() == 0, "inactive sound union, envelope channels and unused tangents ignored");
	}
	void PixelPair()
	{
		m_vPixelLayers.clear();
		for(int Side = 0; Side < 2; ++Side)
		{
			auto Map = std::make_unique<CEditorMap>(this);
			Map->CreateDefault();
			str_copy(Map->m_aDisplayName, "Fixture");
			WhiteImage(*Map, "white", true);
			WhiteImage(*Map, "unused identical white", false);
			auto Group = Map->NewGroup();
			str_copy(Group->m_aName, "Pixel");
			Group->m_ParallaxX = Group->m_ParallaxY = 0;
			Group->m_OffsetX = 40;
			Group->m_OffsetY = 60;
			auto Tiles = std::make_shared<CLayerTiles>(Map.get(), 256, 256);
			str_copy(Tiles->m_aName, "Pixel tiles");
			Tiles->m_Image = 0;
			Tiles->m_pTiles[180 + 190 * 256].m_Index = Side ? 2 : 1;
			Group->AddLayer(Tiles);
			m_vPixelLayers.push_back(Tiles);
			auto Unchanged = std::make_shared<CLayerTiles>(Map.get(), 4, 4);
			str_copy(Unchanged->m_aName, "Unchanged");
			Group->AddLayer(Unchanged);
			str_copy(Map->m_MapInfo.m_aAuthor, Side ? "After" : "Before");
			Map->m_vSettings.emplace_back(Side ? "sv_map This deliberately long ordered setting value wraps across many inspector lines. Every old and new character must remain readable while scrolling the narrow inspector. Extra words extend the value near the map setting limit to exercise its final line." : "sv_map short");
			if(Side)
				m_pAfter = Map.get();
			else
				m_pBefore = Map.get();
			Maps().push_back(std::move(Map));
		}
		this->*Access(TSelectedMap{}) = Maps().size() - 2;
		Compare().*Access(TGroup{}) = false;
		OpenComparison(*this, m_pBefore, m_pAfter);
	}
	void CheckPixels(vec2 WorldPosition = vec2(180 * 32 + 16, 190 * 32 + 16))
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "framebuffer read for pixel oracle");
		if(!Image.m_pData)
			return;
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto &Pane = (Compare().*Access(TPanes{}))[Side];
			const auto Center = Compare().*Access(TCenter{});
			const auto Origin = Compare().*Access(TOrigin{});
			const float Zoom = Compare().*Access(TZoom{});
			const auto &Group = *(Side ? m_pAfter : m_pBefore)->m_vpGroups.back();
			const vec2 Projection = Center + vec2(Origin.x * Group.m_ParallaxX / 100.0f + Group.m_OffsetX, Origin.y * Group.m_ParallaxY / 100.0f + Group.m_OffsetY);
			const vec2 Point = Pane.Center() + (WorldPosition - Projection) / Zoom;
			Check(Pane.Inside(Point), "fit/pan reveals sampled tile side " + std::to_string(Side));
			const int X = (int)(Point.x / Ui()->Screen()->w * Image.m_Width), Y = (int)(Point.y / Ui()->Screen()->h * Image.m_Height);
			if(X < 0 || Y < 0 || X >= (int)Image.m_Width || Y >= (int)Image.m_Height)
			{
				Check(false, "pixel sample in framebuffer");
				continue;
			}
			const ColorRGBA C = Image.PixelColor(X, Y);
			m_Log << "PIXEL " << Side << " x=" << X << " y=" << Y << " rgb=" << C.r << "," << C.g << "," << C.b << std::endl;
			const ColorRGBA Expected = Side ? ColorRGBA(0.25f, 1, 0.45f, 1) : ColorRGBA(1, 0.3f, 0.3f, 1);
			constexpr float Tolerance = 2.0f / 255;
			Check(std::abs(C.r - Expected.r) <= Tolerance && std::abs(C.g - Expected.g) <= Tolerance && std::abs(C.b - Expected.b) <= Tolerance, "solid-texture tile interior has expected comparison color " + std::to_string(Side));
		}
		Image.Free();
	}
	void RegressionSteps()
	{
		Do([this] { SnapshotChecks(); PixelPair(); });
		WaitReady();
		Do([this] { SelectKind(MapDiff::EEntryKind::LAYER,"Pixel tiles"); m_SavedEntry=Compare().*Access(TSelectedEntry{}); });
		Delay();
		ToggleMapContext();
		Do([this] {
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::IMAGE && Result().m_vEntries[i].m_Name == "unused identical white")
					(Compare().*Access(TSelect{}))(i);
		});
		Delay();
		Do([this] {
			const auto &Values = Compare().*Access(TValueRows{});
			Check(std::none_of(Values.begin(), Values.end(), [](const auto &Value) { return Value.m_Name == "Affected layer reference"; }), "unused identical resource has no false affected-layer references");
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::IMAGE && Result().m_vEntries[i].m_Name == "white")
					(Compare().*Access(TSelect{}))(i);
		});
		Delay();
		Do([this] { const auto &Values = Compare().*Access(TValueRows{}); Check(std::count_if(Values.begin(), Values.end(), [](const auto &Value) { return Value.m_Name == "Affected layer reference"; }) == 2, "used resource lists its actual two source layer references"); });
		ToggleMapContext();
		Do([this] { SelectKind(MapDiff::EEntryKind::LAYER, "Pixel tiles"); });
		Delay();
		Do([this] { CheckPixels(); Key(SDLK_HOME,true); });
		Delay(2);
		Do([this] { Key(SDLK_HOME,false); Check((Compare().*Access(TSelectedEntry{}))==m_SavedEntry,"Home fits without navigator selection change"); m_SavedCenter=Compare().*Access(TCenter{}); });
		Click([] { return vec2(167, 62); });
		Click([] { return vec2(167, 62); });
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == m_SavedEntry, "group to layer returns to focused child"); });
		ToggleMapContext();
		Do([this] { for(int i=0;i<(int)Result().m_vEntries.size();++i) if(Result().m_vEntries[i].m_Name=="Unchanged") { (Compare().*Access(TSelect{}))(i); break; } });
		ToggleMapContext();
		Do([this] { const auto &Nav=Compare().*Access(TNavigator{}); const int Selected=Compare().*Access(TSelectedEntry{}); Check(std::find(Nav.begin(),Nav.end(),Selected)!=Nav.end(),"hiding context selects visible navigator entry"); Key(SDLK_n,true); });
		Delay(2);
		Do([this] { Key(SDLK_n,false); const auto &Nav=Compare().*Access(TNavigator{}); Check(std::find(Nav.begin(),Nav.end(),Compare().*Access(TSelectedEntry{}))!=Nav.end(),"Next remains coherent after context change"); SelectKind(MapDiff::EEntryKind::SETTINGS); Graphics()->Resize(800,600,0); OnWindowResize(); });
		Delay();
		Screenshot("comparison-settings-wrapped");
		Do([this] { const float Height=(Compare().*Access(TSettings{})).*Access(TContentSize{}); m_Log << "INSPECTOR content_size=" << Height << std::endl; Check(Height>40,"wrapped settings value expands scroll extent"); Graphics()->Resize(1280,800,0); OnWindowResize(); CloseComparison(); });
		Delay();
		Do([this] { Check(!HasSnapshot(Compare()) && Result().m_vEntries.empty(),"closed viewer releases owned snapshots/results");
   m_vPixelLayers[1]->m_pTiles[180+190*256].m_Index=1; str_copy(m_pAfter->m_MapInfo.m_aAuthor,m_pBefore->m_MapInfo.m_aAuthor); m_pAfter->m_vSettings=m_pBefore->m_vSettings;
   OpenComparison(*this,m_pBefore,m_pAfter); });
		WaitReady();
		Do([this] { Check(Result().ChangeCount()==0 && (Compare().*Access(TSelectedEntry{}))==-1,"changed to equal reopening has no stale selection"); CloseComparison(); });
		Delay();
		Do([this] { OpenComparison(*this,m_pBefore,m_pAfter); Tabs().OnUpdate(*this); const auto Start=std::chrono::steady_clock::now(); CloseComparison(); OpenComparison(*this,m_pBefore,m_pAfter); Check(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count()<100,"cancel and immediate reopen do not wait for unfinished future"); });
		WaitReady();
		Do([this] { Check(Result().ChangeCount()==0,"reopened comparison completes after cancelled work is reaped"); CloseComparison(); });
		Delay();
	}
	void FitRegressionSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_copy(pMap->m_aDisplayName, "Fixture");
				WhiteImage(*pMap, "fit", true);
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Fit test");
				auto pHidden = std::make_shared<CLayerTiles>(pMap.get(), 4, 4);
				pHidden->m_Image = 0;
				pHidden->m_pTiles[0].m_Index = 1;
				pGroup->AddLayer(pHidden);
				auto pFar = std::make_shared<CLayerTiles>(pMap.get(), 2048, 1024);
				str_copy(pFar->m_aName, Side ? "Changed" : "Original");
				pFar->m_Image = 0;
				pFar->m_pTiles[2000 + 900 * 2048].m_Index = 1;
				pGroup->AddLayer(pFar);
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			Compare().*Access(TGroup{}) = true;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		auto SelectGroup = [this] {
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::GROUP && Result().m_vEntries[i].m_Name == "Fit test")
					(Compare().*Access(TSelect{}))(i);
		};
		Do(SelectGroup);
		Delay();
		Do([this] { CheckPixels(vec2(2000 * 32 + 16, 900 * 32 + 16)); CloseComparison(); });
		Delay();
		Do([this] {
			for(auto *pMap : {m_pBefore, m_pAfter})
			{
				auto pGroup = pMap->m_vpGroups.back();
				pGroup->m_UseClipping = 1;
				pGroup->m_ClipX = 2000 * 32;
				pGroup->m_ClipY = 900 * 32;
				pGroup->m_ClipW = pGroup->m_ClipH = 64;
				std::static_pointer_cast<CLayerTiles>(pGroup->m_vpLayers.back())->m_pTiles[0].m_Index = 1;
			}
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do(SelectGroup);
		Delay();
		Do([this] { CheckPixels(vec2(2000 * 32 + 16, 900 * 32 + 16)); CloseComparison(); });
		Delay();
	}
	void CheckBatchedMarkers()
	{
		auto *pGraphics = static_cast<CGraphics_Threaded *>(Graphics());
		CImageInfo Image;
		pGraphics->ReadFramebuffer(Image); // Start with an empty command buffer.
		Image.Free();
		auto *pBuffer = pGraphics->*Access(TCommandBuffer{});
		const auto Calls = pBuffer->m_RenderCallCount;
		(Compare().*Access(TRenderPane{}))(*this, (Compare().*Access(TPanes{}))[0], 0);
		Check(pBuffer == pGraphics->*Access(TCommandBuffer{}) && pBuffer->m_RenderCallCount - Calls < 20, "visible changed tile markers share bounded render batches");
	}
	void CheckCachedPane()
	{
		auto *pGraphics = static_cast<CGraphics_Threaded *>(Graphics());
		CImageInfo Image;
		pGraphics->ReadFramebuffer(Image);
		Image.Free();
		auto *pBuffer = pGraphics->*Access(TCommandBuffer{});
		(Compare().*Access(TRenderPane{}))(*this, (Compare().*Access(TPanes{}))[0], 0);
		int Uploads = 0, TileDraws = 0;
		for(const auto *pCommand = pBuffer->Head(); pCommand; pCommand = pCommand->m_pNext)
		{
			Uploads += pCommand->m_Cmd == CCommandBuffer::CMD_CREATE_BUFFER_OBJECT || pCommand->m_Cmd == CCommandBuffer::CMD_RECREATE_BUFFER_OBJECT;
			TileDraws += pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TILE_LAYER;
			if(pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TILE_LAYER)
				Check(static_cast<const CCommandBuffer::SCommand_RenderTileLayer *>(pCommand)->m_State.m_Texture >= 0, "packed tile buffer uses a textured pipeline");
		}
		Check(pBuffer == pGraphics->*Access(TCommandBuffer{}) && Uploads == 0 && TileDraws >= 3, "full-map steady frame reuses GPU tiles without uploads");
	}
	void ContextPixels(bool Context, bool Group, bool FullMap = true)
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "full-map context framebuffer available");
		if(!Image.m_pData)
			return;
		auto Over = [](ColorRGBA Base, ColorRGBA Color, float Alpha) { return ColorRGBA(mix(Base.r, Color.r, Alpha), mix(Base.g, Color.g, Alpha), mix(Base.b, Color.b, Alpha), 1); };
		const ColorRGBA Background(0.08f, 0.09f, 0.11f, 1);
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto Pane = (Compare().*Access(TPanes{}))[Side];
			auto Sample = [&](int X, ColorRGBA Expected, const char *pWhat) {
				const vec2 World(X * 32 + 16, 10 * 32 + 16);
				const vec2 Point = Pane.Center() + (World - (Compare().*Access(TCenter{}))) / (Compare().*Access(TZoom{}));
				Check(Pane.Inside(Point), std::string(pWhat) + " inside pane");
				const auto Actual = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
				Check(std::abs(Actual.r - Expected.r) < 0.012f && std::abs(Actual.g - Expected.g) < 0.012f && std::abs(Actual.b - Expected.b) < 0.012f, std::string(pWhat) + " side " + std::to_string(Side));
			};
			Sample(8, Context && FullMap ? Over(Over(Background, ColorRGBA(1, 0, 0, 1), 0.25f), ColorRGBA(0, 0, 1, 1), 0.25f) : Background, "cross-group context preserves source order");
			Sample(9, Context && FullMap ? Over(Background, ColorRGBA(0, 0, 1, 1), 0.25f) : Background, "unselected changes remain neutral context");
			Sample(10, Context ? Over(FullMap ? Over(Background, ColorRGBA(0, 0, 1, 1), 0.25f) : Background, ColorRGBA(1, 1, 1, 1), 0.65f) : Background, "selected layer context stays prominent above scenery");
			Sample(11, Context && (FullMap || Group) ? Over(Background, ColorRGBA(1, 1, 1, 1), Group ? 0.65f : 0.25f) : Background, "sibling layer follows layer or group focus");
			Sample(12, Side ? ColorRGBA(0.25f, 1, 0.45f, 1) : ColorRGBA(1, 0.3f, 0.3f, 1), "selected change remains visible");
			Sample(14, Context && FullMap ? Over(Background, ColorRGBA(0, 1, 0, 1), 0.25f) : Background, "background group offset and clipping");
			Sample(15, Background, "clipped background tile remains hidden");
			Sample(16, Context && FullMap ? Over(Background, ColorRGBA(0, 1, 1, 1), 0.25f) : Background, "untextured context uses the matching vertex layout");
		}
		Image.Free();
	}
	void CheckContextBufferPixels()
	{
		auto *pGraphics = static_cast<CGraphics_Threaded *>(Graphics());
		CImageInfo Buffered, Immediate;
		pGraphics->ReadFramebuffer(Buffered);
		pGraphics->*Access(TTileBuffering{}) = false;
		for(int Side = 0; Side < 2; ++Side)
			(Compare().*Access(TRenderPane{}))(*this, (Compare().*Access(TPanes{}))[Side], Side);
		pGraphics->ReadFramebuffer(Immediate);
		pGraphics->*Access(TTileBuffering{}) = true;
		Check(Buffered.m_pData && Immediate.m_pData, "buffer comparison framebuffer available");
		if(Buffered.m_pData && Immediate.m_pData)
			for(int Side = 0; Side < 2; ++Side)
			{
				const auto Pane = (Compare().*Access(TPanes{}))[Side];
				bool Equal = true;
				for(int Y = (int)Pane.y + 2; Y < Pane.y + Pane.h - 2; Y += 3)
					for(int X = (int)Pane.x + 2; X < Pane.x + Pane.w - 2; X += 3)
					{
						const int PX = (int)(X / Ui()->Screen()->w * Buffered.m_Width), PY = (int)(Y / Ui()->Screen()->h * Buffered.m_Height);
						const auto A = Buffered.PixelColor(PX, PY), B = Immediate.PixelColor(PX, PY);
						Equal &= std::abs(A.r - B.r) < 0.012f && std::abs(A.g - B.g) < 0.012f && std::abs(A.b - B.b) < 0.012f;
					}
				Check(Equal, "cached and immediate context match after navigation side " + std::to_string(Side));
			}
		Buffered.Free();
		Immediate.Free();
	}
	void CheckParallaxContext()
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "parallax framebuffer available");
		if(!Image.m_pData)
			return;
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto Pane = (Compare().*Access(TPanes{}))[Side];
			const vec2 Camera = (Compare().*Access(TCenter{})) + (Compare().*Access(TOrigin{}));
			const vec2 Projection(Camera.x * (Side ? 0.5f : 0.0f), 0);
			const vec2 World(12 * 32 + 16, 14 * 32 + 16);
			const vec2 Point = Pane.Center() + (World - Projection) / (Compare().*Access(TZoom{}));
			Check(Pane.Inside(Point), "parallax probe inside pane");
			if(Pane.Inside(Point))
			{
				const auto Color = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
				Check(std::abs(Color.r - 0.31f) < 0.012f && std::abs(Color.g - 0.3175f) < 0.012f, "background parallax follows current camera with shared pan/zoom");
			}
		}
		Image.Free();
	}
	int FindEntry(const char *pName)
	{
		for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
			if(Result().m_vEntries[i].m_Name == pName)
				return i;
		Check(false, std::string("find tree entry ") + pName);
		return 0;
	}
	vec2 DetailPosition(int Index)
	{
		const auto &Scroll = (Compare().*Access(TDetails{})).*Access(TListScroll{});
		const auto &Rect = Scroll.*Access(TScrollRect{});
		return vec2(Rect.x + Rect.w / 2, Rect.y + (Scroll.*Access(TScrollOffset{})) + Index * 19 + 9);
	}
	vec2 TreePosition(const char *pName, int Control = 0) // 0: label, 1: eye, 2: fold
	{
		const int EntryIndex = FindEntry(pName);
		const int Parent = Result().m_vEntries[EntryIndex].m_Parent;
		Check(Parent < 0 || std::get<1>(LayerState(Compare(), Parent)), std::string("tree target has an expanded parent ") + pName);
		float Offset = 0;
		bool HadGroup = false;
		for(int i = 0; i <= EntryIndex; ++i)
		{
			const auto &Entry = Result().m_vEntries[i];
			if((Entry.m_Kind == MapDiff::EEntryKind::GROUP || Entry.m_Kind == MapDiff::EEntryKind::LAYER) && (Entry.m_Parent < 0 || std::get<1>(LayerState(Compare(), Entry.m_Parent))))
			{
				if(Entry.m_Kind == MapDiff::EEntryKind::GROUP)
				{
					if(HadGroup)
						Offset += 5;
					HadGroup = true;
				}
				if(i < EntryIndex)
					Offset += 14;
			}
		}
		const auto &Scroll = Compare().*Access(TLayerTree{});
		const auto &Rect = Scroll.*Access(TScrollRect{});
		const vec2 Point(Control == 1 ? Rect.x + (Result().m_vEntries[EntryIndex].m_Kind == MapDiff::EEntryKind::GROUP ? 7 : 19) : Rect.x + std::min(85.0f, Rect.w - 30.0f), Rect.y + (Scroll.*Access(TScrollOffset{})) + Offset + 6);
		Check(Rect.Inside(Point), std::string("tree control visible ") + pName);
		return Point;
	}
	void ContextPoint(int X, ColorRGBA Expected, const char *pMessage)
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "visibility framebuffer available");
		if(!Image.m_pData)
			return;
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto Pane = (Compare().*Access(TPanes{}))[Side];
			const vec2 Point = Pane.Center() + (vec2(X * 32 + 16, 10 * 32 + 16) - (Compare().*Access(TCenter{}))) / (Compare().*Access(TZoom{}));
			Check(Pane.Inside(Point), "visibility probe inside pane");
			const auto Actual = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
			Check(std::abs(Actual.r - Expected.r) < 0.012f && std::abs(Actual.g - Expected.g) < 0.012f && std::abs(Actual.b - Expected.b) < 0.012f, std::string(pMessage) + " side " + std::to_string(Side));
		}
		Image.Free();
	}
	void CheckPaneNotice()
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "notice framebuffer available");
		if(!Image.m_pData)
			return;
		for(const auto &Pane : Compare().*Access(TPanes{}))
		{
			bool Found = false;
			const float Scale = Image.m_Height / Ui()->Screen()->h;
			for(int Y = (int)((Pane.y + 5) * Scale); Y < (Pane.y + 23) * Scale; ++Y)
				for(int X = (int)((Pane.x + 5) * Scale); X < (Pane.x + Pane.w - 5) * Scale; ++X)
				{
					const auto Color = Image.PixelColor(X, Y);
					Found |= Color.r > 0.6f && Color.g > 0.4f && Color.b < 0.6f;
				}
			Check(Found, "both panes explain hidden or unchanged focus");
		}
		Image.Free();
	}
	void CheckTreeNameColors()
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "tree name framebuffer available");
		if(!Image.m_pData)
			return;
		for(const auto &[pName, Kind] : {std::pair{"Selected", 0}, {"Focus", 0}, {"Added quads", 1}, {"Removed", 2}, {"Sibling", 3}})
		{
			const auto Position = TreePosition(pName);
			const auto Rect = (Compare().*Access(TLayerTree{})).*Access(TScrollRect{});
			const int Indent = Result().m_vEntries[FindEntry(pName)].m_Kind == MapDiff::EEntryKind::GROUP ? 15 : 27;
			int Colored = 0;
			const float Scale = Image.m_Height / Ui()->Screen()->h;
			// Inspect only the name glyphs, excluding eyes and status badges.
			for(int Y = (int)((Position.y - 5) * Scale); Y < (Position.y + 5) * Scale; ++Y)
				for(int X = (int)((Rect.x + Indent) * Scale); X < (Rect.x + Indent + 60) * Scale; ++X)
				{
					const auto C = Image.PixelColor(X, Y);
					Colored += Kind == 0 ? C.r > 0.8f && C.g > 0.6f && C.g < 0.85f && C.b < 0.5f :
						   Kind == 1 ? C.g > 0.8f && C.r < 0.4f && C.b < 0.6f :
						   Kind == 2 ? C.r > 0.8f && C.g < 0.4f && C.b < 0.4f :
							       C.r > 0.68f && std::abs(C.r - C.g) < 0.02f && std::abs(C.g - C.b) < 0.02f;
				}
			Check(Colored > 3, std::string("actual tree name status color ") + pName);
		}
		Image.Free();
	}
	void FullContextSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_copy(pMap->m_aDisplayName, "Fixture");
				pMap->m_vpGroups.clear();
				pMap->m_pGameGroup = nullptr;
				pMap->m_MapViewState.m_WorldOffset = vec2(0, 0);
				auto pImage = WhiteImage(*pMap, "context", false);
				for(int Y = 0; Y < 16; ++Y)
					for(int X = 0; X < 16; ++X)
					{
						pImage->SetPixelColor(48 + X, Y, ColorRGBA(X < 8 ? 1 : 0, Y < 8 ? 1 : 0, X >= 8 && Y >= 8 ? 1 : 0, 1));
						pImage->SetPixelColor(64 + X, Y, ColorRGBA(0.3f, X < 8 ? 1 : 0, Y < 8 ? 1 : 0, 1));
					}
				pImage->m_Texture = Graphics()->LoadTextureRaw(*pImage, Graphics()->TextureLoadFlags(), "context");
				auto Add = [&](const std::shared_ptr<CLayerGroup> &pGroup, const char *pName, ColorRGBA Color, std::initializer_list<int> Xs) {
					auto pTiles = std::make_shared<CLayerTiles>(pMap.get(), 64, 64);
					str_copy(pTiles->m_aName, pName);
					pTiles->m_Image = 0;
					pTiles->m_Color = {int(Color.r * 255), int(Color.g * 255), int(Color.b * 255), 255};
					for(int X : Xs)
						pTiles->m_pTiles[X + 10 * 64].m_Index = 1;
					pGroup->AddLayer(pTiles);
					return pTiles;
				};
				auto pSelected = pMap->NewGroup();
				str_copy(pSelected->m_aName, "Selected");
				auto pTiles = Add(pSelected, "Focus", ColorRGBA(1, 1, 1, 1), {10, 12});
				pTiles->m_pTiles[12 + 10 * 64].m_Index = Side ? 2 : 1;
				Add(pSelected, "Sibling", ColorRGBA(1, 1, 1, 1), {11});
				for(int n = 0; n < 3; ++n)
					Add(pSelected, "Overlap", ColorRGBA(1, 1, 1, 1), {12});
				std::shared_ptr<CLayer> pStructural;
				if(Side)
					pStructural = std::make_shared<CLayerQuads>(pMap.get());
				else
					pStructural = std::make_shared<CLayerSounds>(pMap.get());
				str_copy(pStructural->m_aName, Side ? "Added quads" : "Removed");
				pSelected->AddLayer(pStructural);
				Add(pMap->NewGroup(), "Red", ColorRGBA(1, 0, 0, 1), {8});
				pTiles = Add(pMap->NewGroup(), "Blue", ColorRGBA(0, 0, 1, 1), {8, 9, 10, 12});
				pTiles->m_Visible = false; // Viewer visibility must not overwrite source state.
				pTiles->m_pTiles[9 + 10 * 64].m_Index = Side ? 2 : 1;
				auto pClipped = pMap->NewGroup();
				pClipped->m_OffsetX = 32;
				pClipped->m_OffsetY = 16;
				pClipped->m_ParallaxX = pClipped->m_ParallaxY = 0;
				pClipped->m_UseClipping = 1;
				pClipped->m_ClipX = 14 * 32;
				pClipped->m_ClipY = 10 * 32;
				pClipped->m_ClipW = pClipped->m_ClipH = 32;
				auto pClippedTiles = Add(pClipped, "Clipped", ColorRGBA(0, 1, 0, 1), {});
				// Zero-parallax geometry near the origin intersects the game-space
				// clipping rectangle only after the camera has moved to the focus.
				pClippedTiles->m_pTiles[3].m_Index = 1;
				pClippedTiles->m_pTiles[4].m_Index = 1;
				Add(pMap->NewGroup(), "Untextured", ColorRGBA(0, 1, 1, 1), {16})->m_Image = -1;
				auto pPattern = Add(pMap->NewGroup(), "Orientations", ColorRGBA(1, 1, 1, 1), {});
				pPattern->Resize(256, 128);
				for(int n = 0; n < 8; ++n)
				{
					auto &Tile = pPattern->m_pTiles[7 + n + 12 * pPattern->m_Width];
					Tile.m_Index = n % 2 ? 3 : 4;
					Tile.m_Flags = (n & 1 ? TILEFLAG_XFLIP : 0) | (n & 2 ? TILEFLAG_YFLIP : 0) | (n & 4 ? TILEFLAG_ROTATE : 0);
				}
				// Distant occupied tiles expose cache rebuilds after a large pan.
				pPattern->m_pTiles[50 + 12 * pPattern->m_Width].m_Index = 3;
				auto pParallax = pMap->NewGroup();
				pParallax->m_ParallaxX = Side ? 50 : 0;
				pParallax->m_ParallaxY = 0;
				auto pQuads = std::make_shared<CLayerQuads>(pMap.get());
				pQuads->NewQuad(12 * 32 + 16, 14 * 32 + 16, 32, 32);
				for(int q = 0; q < 4096; ++q)
					pQuads->NewQuad(100000 + q * 64, 100000, 32, 32);
				pParallax->AddLayer(pQuads);
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		auto Camera = [this] { Compare().*Access(TCenter{}) = vec2(12 * 32, 10 * 32 + 16); Compare().*Access(TZoom{}) = 1.0f; };
		Do([this, Camera] { SelectKind(MapDiff::EEntryKind::LAYER, "Focus"); Camera(); });
		Delay();
		Do([this] { ContextPixels(false, false); });
		Click([] { return vec2(240, 62); });
		Do([this] { Check((Compare().*Access(TContext{})) == 1, "context cycles to selected layer"); ContextPixels(true, false, false); });
		Screenshot("selection-context-layer");
		Click([] { return vec2(240, 62); });
		Do([this] { ContextPixels(true, false); CheckCachedPane(); CheckContextBufferPixels(); });
		Screenshot("full-map-context-layer");
		Click([this] { return SectionPosition(1); });
		Do([this] {
			Check((Compare().*Access(TSection{})) == 1, "Layers tab opens a separate tree");
			Check(std::get<2>(LayerState(Compare(), FindEntry("Selected"))) > 2 && std::get<2>(LayerState(Compare(), FindEntry("Sibling"))) == 0, "tree aggregates child differences and distinguishes unchanged layers");
			Check(Result().m_vEntries[FindEntry("Added quads")].m_Before < 0 && Result().m_vEntries[FindEntry("Removed")].m_After < 0, "tree fixture includes added and removed layer sides");
			m_SavedCenter = Compare().*Access(TCenter{});
		});
		Screenshot("comparison-layer-tree");
		Do([this] { CheckTreeNameColors(); });
		Click([this] { return TreePosition("Blue", 1); });
		Do([this] { ContextPoint(9, ColorRGBA(0.08f, 0.09f, 0.11f, 1), "hidden background layer is absent"); ContextPoint(8, ColorRGBA(0.31f, 0.0675f, 0.0825f, 1), "other groups retain their source order"); });
		Click([this] { return TreePosition("Blue", 1); });
		Click([this] { return TreePosition("Focus", 1); });
		Do([this] {
			ContextPoint(10, ColorRGBA(0.06f, 0.0675f, 0.3325f, 1), "hidden selected layer removes context and diff rendering");
			Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Focus") && (Compare().*Access(TCenter{})) == m_SavedCenter, "eye click preserves selection and camera");
		});
		Click([this] { return TreePosition("Selected", 1); });
		Do([this] { ContextPoint(11, ColorRGBA(0.08f, 0.09f, 0.11f, 1), "hidden group suppresses all child layers"); });
		Click([this] { return TreePosition("Focus", 1); });
		Do([this] { Check(!std::get<0>(LayerState(Compare(), FindEntry("Focus"))), "parent-hidden eye does not lose the child choice"); });
		Click([this] { return TreePosition("Selected", 1); });
		Do([this] { ContextPoint(10, ColorRGBA(0.06f, 0.0675f, 0.3325f, 1), "showing a group preserves its hidden child"); });
		Click([this] { return TreePosition("Selected"); });
		Click([this] { return DetailPosition(1); });
		Do([this] { m_SavedEntry = Compare().*Access(TSelectedDetail{}); Check(SelectedDetail(Compare())[0] != FindEntry("Selected"), "fold fixture inspects a child detail"); });
		Do([this] {
			m_SavedCenter = Compare().*Access(TCenter{});
			const auto Pos = (Compare().*Access(TPanes{}))[0].Center();
			Ui()->OnCursorMove((Pos.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (Pos.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
			Mouse(SDL_BUTTON_LEFT, true);
		});
		Delay();
		Do([this] { Ui()->OnCursorMove(80, 45); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); Check(distance(Compare().*Access(TCenter{}), m_SavedCenter) > 1, "fold fixture pans the group camera"); m_SavedCenter = Compare().*Access(TCenter{}); });
		Delay();
		DoubleClick([this] { return TreePosition("Selected"); });
		Do([this] { Check(!std::get<1>(LayerState(Compare(), FindEntry("Selected"))) && (Compare().*Access(TCenter{})) == m_SavedCenter, "folding group preserves the panned inspection camera"); Check((Compare().*Access(TSelectedDetail{})) == m_SavedEntry, "folding group preserves selected child detail"); });
		DoubleClick([this] { return TreePosition("Selected"); });
		Click([this] { return vec2(190, Ui()->Screen()->h - 34); });
		Do([this, Camera] { SelectKind(MapDiff::EEntryKind::LAYER, "Focus"); Camera(); });
		Delay();
		Do([this] { ContextPixels(true, false); CheckCachedPane(); CheckSources("layer visibility controls"); });
		Click([this] { return TreePosition("Focus", 1); }, SDL_BUTTON_RIGHT);
		Do([this] { Check(std::get<0>(LayerState(Compare(), FindEntry("Focus"))) && !std::get<0>(LayerState(Compare(), FindEntry("Sibling"))), "right-eye solos layer peers"); });
		Click([this] { return TreePosition("Focus", 1); }, SDL_BUTTON_RIGHT);
		Do([this] { Check(std::get<0>(LayerState(Compare(), FindEntry("Sibling"))), "repeated right-eye restores layer peers"); });
		Click([this] { return TreePosition("Selected", 1); }, SDL_BUTTON_RIGHT);
		Do([this] { Check(!std::get<0>(LayerState(Compare(), Result().m_vEntries[FindEntry("Blue")].m_Parent)), "right-eye solos groups"); });
		Click([this] { return TreePosition("Selected", 1); }, SDL_BUTTON_RIGHT);
		Do([this] { Check(std::get<0>(LayerState(Compare(), Result().m_vEntries[FindEntry("Blue")].m_Parent)), "repeated right-eye restores groups"); CheckSources("solo controls"); });
		Click([this] { return TreePosition("Added quads"); });
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Added quads") && !(Compare().*Access(TGroup{})), "tree selects an added layer for inspection"); });
		Click([this] { return TreePosition("Removed"); });
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Removed"), "tree selects a removed layer for inspection"); });
		Do([this, Camera] { SelectKind(MapDiff::EEntryKind::LAYER, "Focus"); Camera(); });
		Delay();
		Click([this] { return SectionPosition(0); });
		Click([] { return vec2(167, 62); });
		Do(Camera);
		Delay();
		Do([this] { ContextPixels(true, true); });
		Screenshot("full-map-context-group");
		Click([] { return vec2(240, 62); });
		Click([] { return vec2(240, 62); });
		Do([this] { Check((Compare().*Access(TContext{})) == 1, "context cycles to selected group"); ContextPixels(true, true, false); });
		Screenshot("selection-context-group");
		Click([] { return vec2(240, 62); });
		for(const auto &[Center, Zoom] : {std::pair{vec2(50 * 32, 12 * 32), 20.0f}, std::pair{vec2(50 * 32, 12 * 32), 0.5f}, std::pair{vec2(10000, 10000), 1.0f}, std::pair{vec2(12 * 32, 10 * 32 + 16), 1.0f}})
		{
			Do([this, Center, Zoom] { Compare().*Access(TCenter{}) = Center; Compare().*Access(TZoom{}) = Zoom; });
			Delay();
			Do([this] { CheckContextBufferPixels(); });
		}
		// Exercise the sparse immediate fallback against the same pixel oracle.
		Do([this] { static_cast<CGraphics_Threaded *>(Graphics())->*Access(TTileBuffering{}) = false; });
		Delay();
		Do([this] { ContextPixels(true, true); static_cast<CGraphics_Threaded *>(Graphics())->*Access(TTileBuffering{}) = true; CheckSources("full map context"); CloseComparison(); Check(!HasRenderCache(Compare()), "close releases comparison GPU cache"); });
		Delay();
		Do([this] { OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		ToggleMapContext();
		Do([this, Camera] {
			bool Found = false;
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::GROUP && Result().m_vEntries[i].m_Name == "Selected")
				{
					(Compare().*Access(TSelect{}))(i);
					Found = true;
				}
			Check(Found, "select group whose children changed");
			Camera();
			Compare().*Access(TOrigin{}) = vec2(128, 64);
			Compare().*Access(TZoom{}) = 4;
		});
		Delay();
		Do([this] { CheckParallaxContext(); Compare().*Access(TCenter{}) += vec2(32, 64); Compare().*Access(TZoom{}) = 5; });
		Delay();
		Do([this] { CheckParallaxContext(); });
		Do([this] { Check(HasRenderCache(Compare()), "reopen creates comparison cache"); Compare().Invalidate("context cache test"); Check(!HasRenderCache(Compare()), "source invalidation releases comparison cache"); CloseComparison(); });
		Delay();
	}
	void NavigationSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_copy(pMap->m_aDisplayName, "Fixture");
				pMap->m_vpGroups.clear();
				pMap->m_pGameGroup = nullptr;
				pMap->m_MapViewState.m_WorldOffset = vec2(0, 0);
				WhiteImage(*pMap, "navigation", true);
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Navigation");
				pGroup->m_OffsetX = Side * 32;
				for(const char *pName : {"A", "B", "C", "Duplicate", "Duplicate"})
				{
					auto pTiles = std::make_shared<CLayerTiles>(pMap.get(), 64, 64);
					str_copy(pTiles->m_aName, pName);
					pTiles->m_Image = 0;
					if(str_comp(pName, "A") == 0)
					{
						pTiles->m_pTiles[2 + 2 * 64].m_Index = Side + 1;
						pTiles->m_pTiles[50 + 40 * 64].m_Index = Side + 1;
					}
					else if(str_comp(pName, "C") == 0)
						pTiles->m_pTiles[7 + 9 * 64].m_Index = Side + 1;
					else
						pTiles->m_pTiles[0].m_Index = 1;
					pGroup->AddLayer(pTiles);
				}
				auto pQuads = std::make_shared<CLayerQuads>(pMap.get());
				str_copy(pQuads->m_aName, "Q");
				pQuads->NewQuad(800, 640, 32, 32);
				pQuads->m_vQuads[0].m_aColors[0].r = Side ? 128 : 255;
				pGroup->AddLayer(pQuads);
				if(Side)
					str_copy(pMap->NewGroup()->m_aName, "Added empty");
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		auto Find = [this](const char *pName) {
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Name == pName)
					return i;
			return -1;
		};
		auto Target = [this, Find](const char *pName, int Kind, int Index) {
			const auto Detail = SelectedDetail(Compare());
			Check(Detail == std::array{Find(pName), Kind, Index}, std::string("navigation target ") + pName + " kind " + std::to_string(Kind) + " index " + std::to_string(Index));
			Check((Compare().*Access(TFocusEntry{})) == Find(pName), "navigation focus follows the actual difference");
			if(Kind == 1)
			{
				const auto &Tile = Result().m_vEntries[Find(pName)].m_vTiles[Index];
				Check(distance(Compare().*Access(TCenter{}), vec2(Tile.m_X * 32, Tile.m_Y * 32 + 16)) < 0.1f, "navigation centers the changed tile in both offset groups");
			}
			if(Kind == 2)
				Check(distance(Compare().*Access(TCenter{}), vec2(784, 640)) < 0.1f, "navigation centers the changed quad");
		};
		auto Press = [this](SDL_Keycode KeyCode) {
			Do([this, KeyCode] { Key(KeyCode, true); });
			Delay(2);
			Do([this, KeyCode] { Key(KeyCode, false); });
			Delay(2);
		};
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("A")); });
		Delay();
		Click([this] { return DetailPosition(1); });
		Do([this, Target] { Target("A", 1, 1); Compare().*Access(TCenter{}) = vec2(-5000, -5000); });
		Click([this] { return DetailPosition(1); });
		Do([Target] { Target("A", 1, 1); });
		Press(SDLK_UP);
		Do([Target] { Target("A", 1, 0); });
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("Q")); Compare().*Access(TCenter{}) = vec2(-5000, -5000); });
		Delay();
		Click([this] { return DetailPosition(0); });
		Do([Target] { Target("Q", 2, 0); });
		Do([this, Find] {
			Check(Result().ChangeCount() == 16, "navigation fixture includes structural and property differences for the added group");
			Check((Compare().*Access(TChangedNavigator{})).size() == 5, "layer navigator separates group properties and child changes");
			(Compare().*Access(TSelect{}))(Find("Navigation"));
		});
		Click([] { return vec2(240, 62); });
		Do([this] {
			const auto &Rows = Compare().*Access(TNavigator{});
			Check(Rows.size() == 6 && Rows.back() == -1 && !(Compare().*Access(TUnchangedExpanded{})), "unchanged section starts collapsed after changed entries");
		});
		Click([this] { return vec2(100, NavigatorTop() + 5 * 36 + 18); });
		Do([this] {
			const auto &Rows = Compare().*Access(TNavigator{});
			Check((Compare().*Access(TUnchangedExpanded{})) && Rows.size() > 6 && Rows[5] == -1, "unchanged section expands using its header");
			bool OnlyUnchanged = true;
			for(size_t i = 6; i < Rows.size(); ++i)
				OnlyUnchanged &= !Result().m_vEntries[Rows[i]].Changed();
			Check(OnlyUnchanged, "expanded section contains only unchanged items");
		});
		Screenshot("comparison-unchanged-section");
		// Real toolbar and keyboard events must visit each difference exactly once.
		Click([] { return vec2(391, 62); });
		Do([Target] { Target("A", 1, 0); });
		Press(SDLK_n);
		Do([Target] { Target("A", 1, 1); });
		Press(SDLK_n);
		Do([Target] { Target("C", 1, 0); });
		Press(SDLK_p);
		Do([Target] { Target("A", 1, 1); });
		Press(SDLK_n);
		Press(SDLK_n);
		Do([Target] { Target("Q", 2, 0); });
		Press(SDLK_n);
		Do([Target] { Target("Added empty", 4, 0); });
		for(int i = 0; i < 10; ++i)
		{
			Press(SDLK_n);
			Do([Target, i] { Target("Added empty", 0, i); });
		}
		Press(SDLK_n);
		Do([Target] { Target("Navigation", 0, 0); });
		Press(SDLK_p);
		Do([Target] { Target("Added empty", 0, 9); });
		// Arrow navigation may cross the header but must never select it.
		Click([this] { return vec2(100, NavigatorTop() + 4 * 36 + 18); });
		Press(SDLK_DOWN);
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == (Compare().*Access(TNavigator{}))[6], "keyboard skips the unchanged header downwards"); });
		Press(SDLK_UP);
		Do([this, Find] { Check((Compare().*Access(TSelectedEntry{})) == Find("Added empty"), "keyboard skips the unchanged header upwards"); });
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("B")); });
		Press(SDLK_n);
		Do([Target] { Target("Navigation", 0, 0); });
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("B")); });
		Press(SDLK_p);
		Do([Target] { Target("Added empty", 0, 9); });
		// Collapse with an unchanged selection, then enter group navigation.
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("B")); });
		Click([this] { return vec2(100, NavigatorTop() + 5 * 36 + 18); });
		Do([this, Find] { Check(!(Compare().*Access(TUnchangedExpanded{})) && (Compare().*Access(TSelectedEntry{})) == Find("B"), "collapse preserves the inspected unchanged entry"); (Compare().*Access(TSelect{}))(Find("Navigation")); });
		Press(SDLK_END);
		Do([this, Find] { Check((Compare().*Access(TSelectedEntry{})) == Find("Added empty"), "End stops at the last real row before a collapsed header"); (Compare().*Access(TSelect{}))(Find("Navigation")); });
		Press(SDLK_PAGEDOWN);
		Do([this, Find] { Check((Compare().*Access(TSelectedEntry{})) == Find("Added empty"), "Page Down stops at the last real row before a collapsed header"); (Compare().*Access(TSelect{}))(Find("Navigation")); });
		Click([] { return vec2(167, 62); });
		Do([this] { Check((Compare().*Access(TChangedNavigator{})).size() == 2, "group navigator aggregates child changes"); });
		Click([this] { return DetailPosition(2); });
		Do([this, Target, Find] { Target("A", 1, 1); (Compare().*Access(TSelect{}))(Find("Navigation")); });
		for(const auto &[pName, Kind, Index] : {std::tuple{"A", 1, 0}, std::tuple{"A", 1, 1}, std::tuple{"C", 1, 0}, std::tuple{"Q", 2, 0}, std::tuple{"Added empty", 4, 0}})
		{
			Press(SDLK_n);
			Do([Target, pName, Kind, Index] { Target(pName, Kind, Index); });
		}
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("Navigation")); });
		Press(SDLK_p);
		Do([Target] { Target("Added empty", 0, 9); });
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("Added empty")); });
		Press(SDLK_p);
		Do([Target] { Target("Q", 2, 0); });
		Click([] { return vec2(167, 62); });
		Do([this, Find] { Check((Compare().*Access(TSelectedEntry{})) == Find("Q"), "group to layer follows backwards-navigated child"); });
		Do([this] { CheckSources("comparison navigation"); CloseComparison(); });
		Delay();
		Do([this] {
			// A distant sibling must not pull a child property's fit away from its layer.
			for(auto *pMap : {m_pBefore, m_pAfter})
				std::static_pointer_cast<CLayerTiles>(pMap->m_vpGroups[0]->m_vpLayers[2])->Resize(4096, 64);
			str_copy(m_pAfter->m_vpGroups[0]->m_vpLayers[0]->m_aName, "A renamed");
			Compare().*Access(TGroup{}) = true;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this, Find] { (Compare().*Access(TSelect{}))(Find("Navigation")); });
		Press(SDLK_n);
		Do([this, Find] {
			Check(SelectedDetail(Compare()) == std::array{Find("A renamed"), 0, 0}, "group navigation selects a child property");
			Check(distance(Compare().*Access(TCenter{}), vec2(1008, 1024)) < 0.1f, "child property fit excludes other changed layers in the group");
			CloseComparison();
		});
		Delay();
	}
	CUIRect EnvelopeGraph(int Side)
	{
		CUIRect Graph = (Compare().*Access(TPanes{}))[Side];
		Graph.Margin(6, &Graph);
		Graph.HSplitTop(38, nullptr, &Graph);
		Graph.HSplitBottom(20, &Graph, nullptr);
		Graph.VSplitLeft(45, nullptr, &Graph);
		Graph.Margin(4, &Graph);
		return Graph;
	}
	void CheckGraphPixel(int Side, float TimeFraction, float ValueFraction, ColorRGBA Expected, const char *pMessage)
	{
		const CUIRect Graph = EnvelopeGraph(Side);
		const vec2 Pos(Graph.x + Graph.w * TimeFraction, Graph.y + Graph.h * (1 - ValueFraction));
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		bool Found = false;
		if(Image.m_pData)
		{
			const int X = Pos.x / Ui()->Screen()->w * Image.m_Width, Y = Pos.y / Ui()->Screen()->h * Image.m_Height;
			for(int y = Y - 2; y <= Y + 2; ++y)
				for(int x = X - 2; x <= X + 2; ++x)
				{
					const auto C = Image.PixelColor(x, y);
					Found |= std::abs(C.r - Expected.r) < 0.18f && std::abs(C.g - Expected.g) < 0.18f && std::abs(C.b - Expected.b) < 0.18f;
				}
			Image.Free();
		}
		Check(Found, std::string(pMessage) + " side " + std::to_string(Side));
	}
	void CheckSettingsText()
	{
		// Independent expected rows: insertion of an empty After command keeps
		// the following command aligned while advancing only its After gutter.
		const char *aaCommands[4][2] = {{"(absent)", "(absent)"}, {"sv_pauseable 1", "sv_pauseable 0"}, {"[no line]", ""}, {"tune setting 2", "tune setting 2"}};
		const char *aaNumbers[4][2] = {{"1", "1"}, {"2", "2"}, {"-", "3"}, {"3", "4"}};
		const auto &Scroll = Compare().*Access(TSettings{});
		const auto Area = Scroll.*Access(TScrollRect{});
		CImageInfo Actual;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Actual);
		Check(Actual.m_pData != nullptr, "settings text framebuffer available");
		if(!Actual.m_pData)
			return;
		Ui()->MapScreen();
		for(int Row = 0; Row < 4; ++Row)
			for(int Side = 0; Side < 2; ++Side)
			{
				const float Width = (Area.w - 8) / 2;
				CUIRect Rect{Area.x + Side * (Width + 8), Area.y + Row * 18, Width, 18};
				const auto Background = Actual.PixelColor((int)((Rect.x + Rect.w - 2) / Ui()->Screen()->w * Actual.m_Width), (int)((Rect.y + 2) / Ui()->Screen()->h * Actual.m_Height));
				Rect.Draw(Background, IGraphics::CORNER_NONE, 0);
				CUIRect Number{Rect.x + 4, Rect.y + 4, 35, 12};
				const auto Color = Row == 1 || Row == 2 ? Side ? ColorRGBA(0.25f, 1, 0.45f, 1) : ColorRGBA(1, 0.3f, 0.3f, 1) : ColorRGBA(0.76f, 0.78f, 0.8f, 1);
				SLabelProperties Props;
				Props.SetColor(Color);
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&Number, aaNumbers[Row][Side], 9, TEXTALIGN_ML, Props);
				TextRender()->TextColor(Row == 2 && Side == 0 ? ColorRGBA(0.5f, 0.5f, 0.5f, 1) : ColorRGBA(0.76f, 0.78f, 0.8f, 1));
				CTextCursor Cursor;
				Cursor.SetPosition(vec2(Rect.x + 39, Rect.y + 4));
				Cursor.m_FontSize = 10;
				Cursor.m_LineWidth = Width - 43;
				// Independent oracle: only the final argument changes at byte 13.
				if(Row == 1)
					Cursor.m_vColorSplits.emplace_back(13, 1, Color);
				TextRender()->TextEx(&Cursor, aaCommands[Row][Side]);
				TextRender()->TextColor(ColorRGBA(1, 1, 1, 1));
			}
		CImageInfo Expected;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Expected);
		for(int Row = 0; Row < 4; ++Row)
			for(int Side = 0; Side < 2; ++Side)
			{
				const float Width = (Area.w - 8) / 2;
				const float Left = Area.x + Side * (Width + 8), Top = Area.y + Row * 18;
				int Differences = 0;
				for(int y = (int)(Top / Ui()->Screen()->h * Actual.m_Height); y < (Top + 18) / Ui()->Screen()->h * Actual.m_Height; ++y)
					for(int x = (int)(Left / Ui()->Screen()->w * Actual.m_Width); x < (Left + Width) / Ui()->Screen()->w * Actual.m_Width; ++x)
					{
						const auto A = Actual.PixelColor(x, y), B = Expected.PixelColor(x, y);
						Differences += std::abs(A.r - B.r) > 0.02f || std::abs(A.g - B.g) > 0.02f || std::abs(A.b - B.b) > 0.02f;
					}
				Check(Differences == 0, "rendered settings command and gutter row " + std::to_string(Row) + " side " + std::to_string(Side) + " mismatched pixels=" + std::to_string(Differences));
			}
		Actual.Free();
		Expected.Free();
	}
	void ObjectViewSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				str_copy(pMap->m_aDisplayName, Side ? "Object After" : "Object Before");
				pMap->m_aFilename[0] = 0;
				pMap->m_MapViewState.m_WorldOffset = vec2(0, 0);
				for(int i = 0; i < 200; ++i)
					pMap->m_vSettings.emplace_back(("tune setting " + std::to_string(i)).c_str());
				pMap->m_vSettings[0] = CEditorMapSetting("(absent)");
				pMap->m_vSettings[1] = CEditorMapSetting("sv_pauseable 1");
				if(Side)
				{
					pMap->m_vSettings[1] = CEditorMapSetting("sv_pauseable 0");
					pMap->m_vSettings[150] = CEditorMapSetting("tune changed 150");
					pMap->m_vSettings.insert(pMap->m_vSettings.begin() + 2, CEditorMapSetting(""));
				}
				for(const auto &[pName, Channels] : {std::pair{"Color", 4}, std::pair{"Position", 3}, std::pair{"Sound", 1}, std::pair{"Empty", 4}, std::pair{"Type", Side ? 1 : 3}})
				{
					auto pEnv = std::make_shared<CEnvelope>(Channels);
					str_copy(pEnv->m_aName, pName);
					if(str_comp(pName, "Empty") != 0)
					{
						pEnv->AddPoint(CFixedTime(0), {0, 1024, 512, 1024});
						pEnv->AddPoint(CFixedTime(Side ? 1000 : 500), {Side ? 3072 : 2048, 512, 2048, 1024});
						if(str_comp(pName, "Position") == 0)
						{
							pEnv->m_vPoints[0].m_Curvetype = CURVETYPE_BEZIER;
							pEnv->m_vPoints[0].m_Bezier.m_aOutTangentDeltaX[0] = CFixedTime(100);
							pEnv->m_vPoints[0].m_Bezier.m_aOutTangentDeltaY[0] = Side ? 4096 : 8192;
							pEnv->m_vPoints[1].m_Bezier.m_aInTangentDeltaX[0] = CFixedTime(-100);
						}
					}
					else if(Side)
						pEnv->AddPoint(CFixedTime(0), {1024, 0, 0, 1024});
					pMap->m_vpEnvelopes.push_back(pEnv);
				}
				for(const char *pName : {"Overlap", "Large", "Dense"})
				{
					auto pEnv = std::make_shared<CEnvelope>(1);
					str_copy(pEnv->m_aName, pName);
					if(str_comp(pName, "Overlap") == 0)
					{
						pEnv->AddPoint(CFixedTime(0), {0, 0, 0, 0});
						pEnv->AddPoint(CFixedTime(500), {Side ? 2048 : 1024, 0, 0, 0});
						pEnv->AddPoint(CFixedTime(510), {Side ? 2048 : 1024, 0, 0, 0});
						pEnv->AddPoint(CFixedTime(1000), {3072, 0, 0, 0});
					}
					else if(str_comp(pName, "Large") == 0)
					{
						pEnv->AddPoint(CFixedTime(0), {std::numeric_limits<int>::max() - Side, 0, 0, 0});
						pEnv->AddPoint(CFixedTime(1000), {std::numeric_limits<int>::max() - Side, 0, 0, 0});
					}
					else
					{
						pEnv->m_vPoints.resize(1025);
						for(int i = 0; i < 1025; ++i)
						{
							auto &Point = pEnv->m_vPoints[i];
							Point.m_Time = CFixedTime(i);
							Point.m_Curvetype = CURVETYPE_LINEAR;
							Point.m_aValues[0] = (Side + (i == 777 ? 1 : 0)) * 1024;
						}
					}
					pMap->m_vpEnvelopes.push_back(pEnv);
				}
				if(!Side)
				{
					auto pEnv = std::make_shared<CEnvelope>(1);
					str_copy(pEnv->m_aName, "Removed");
					pMap->m_vpEnvelopes.push_back(pEnv);
				}
				auto pGroup = pMap->NewGroup();
				auto pQuads = std::make_shared<CLayerQuads>(pMap.get());
				pQuads->NewQuad(64, 64, 32, 1024);
				pQuads->m_vQuads[0].m_ColorEnv = 0;
				pGroup->AddLayer(pQuads);
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this] { SelectKind(MapDiff::EEntryKind::SETTINGS); m_SavedCenter = Compare().*Access(TCenter{}); Graphics()->Resize(800, 600, 0); OnWindowResize(); });
		Delay();
		Screenshot("comparison-settings-800");
		Do([this] { CheckSettingsText(); });
		Delay();
		auto Press = [this](SDL_Keycode K) {
			Do([this, K] { Key(K, true); });
			Delay(2);
			Do([this, K] { Key(K, false); });
			Delay(2);
		};
		Press(SDLK_n);
		Press(SDLK_n);
		Delay(20);
		Do([this] {
			Check(SelectedDetail(Compare()) == std::array{FindEntry("Ordered server settings"), 0, 2}, "settings N selects aligned changed row");
			Check(((Compare().*Access(TSettings{})).*Access(TScrollPos{})) > 1000, "settings N reveals distant command");
			Check((Compare().*Access(TCenter{})) == m_SavedCenter, "settings navigation preserves map camera");
		});
		Do([this] { m_SavedScroll = (Compare().*Access(TSettings{})).*Access(TScrollPos{}); });
		Click([this] { return SectionPosition(2); });
		Click([this] { return SectionPosition(3); });
		Delay(20);
		Do([this] { Check(std::abs(((Compare().*Access(TSettings{})).*Access(TScrollPos{})) - m_SavedScroll) < 0.1f && SelectedDetail(Compare()) == std::array{FindEntry("Ordered server settings"), 0, 2}, "settings section restores detail and text scroll after an envelope visit"); });
		Screenshot("comparison-settings-distant");
		Press(SDLK_p);
		Delay(20);
		Do([this] { Check(((Compare().*Access(TSettings{})).*Access(TScrollPos{})) < 100, "settings P reveals inserted empty command"); });
		Do([this] {
			const auto P = (Compare().*Access(TPanes{}))[0].Center();
			Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
		});
		Delay();
		for(int i = 0; i < 3; ++i)
		{
			Do([] { SDL_Event E{}; E.type = SDL_MOUSEWHEEL; E.wheel.y = 1; SDL_PushEvent(&E); });
			Delay(2);
		}
		Delay(30);
		Do([this] { Check(((Compare().*Access(TSettings{})).*Access(TScrollPos{})) == 0, "changed settings click target is visible after scrolling up"); });
		Click([this] { const auto &Scroll = Compare().*Access(TSettings{}); const auto Rect = Scroll.*Access(TScrollRect{}); return vec2(Rect.x + 100, Rect.y + (Scroll.*Access(TScrollOffset{})) + 27); });
		Do([this] { Check((Compare().*Access(TCenter{})) == m_SavedCenter, "settings click preserves map camera"); Check(SelectedDetail(Compare()) == std::array{FindEntry("Ordered server settings"), 0, 0}, "settings click selects a different changed command; actual property=" + std::to_string(SelectedDetail(Compare())[2])); });
		Press(SDLK_n);
		Do([this] { Check(SelectedDetail(Compare()) == std::array{FindEntry("Ordered server settings"), 0, 1}, "N after settings click selects the inserted empty command"); });
		Press(SDLK_p);
		Do([this] { Check(SelectedDetail(Compare()) == std::array{FindEntry("Ordered server settings"), 0, 0}, "P after settings click returns to the replaced command"); });
		Click([this] { return SectionPosition(0); });
		Click([this] {
			const auto &Navigator = Compare().*Access(TNavigator{});
			for(int Row = 0; Row < (int)Navigator.size(); ++Row)
				if(Navigator[Row] >= 0 && Result().m_vEntries[Navigator[Row]].m_Kind == MapDiff::EEntryKind::LAYER)
					return vec2(100, NavigatorTop() + Row * 36 + 18);
			Check(false, "spatial entry available after settings");
			return vec2(100, 141);
		});
		Do([this] {
			Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Kind == MapDiff::EEntryKind::LAYER, "settings click switches back to the map view");
			Check((Compare().*Access(TZoom{})) * (Compare().*Access(TPanes{}))[0].h >= 1087, "leaving full-height settings fits the actual map pane height");
			m_SavedCenter = Compare().*Access(TCenter{});
		});
		for(const char *pName : {"Color", "Position", "Sound", "Empty", "Type", "Removed"})
		{
			Do([this, pName] { SelectKind(MapDiff::EEntryKind::ENVELOPE, pName); });
			Delay();
			Screenshot(std::string("comparison-envelope-") + pName);
			Do([this] { Check((Compare().*Access(TCenter{})) == m_SavedCenter && std::isfinite(Compare().*Access(TZoom{})), "envelope view preserves finite map camera"); });
		}
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Color"); Compare().*Access(TEnvelopeChannels{}) = 1; });
		Delay();
		Do([this] {
			// Shared axes: value range 0..3 with 8% padding. The shorter
			// Before envelope must end at 0.5s, never repeat into the empty half.
			CImageInfo Image;
			static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
			Check(Image.m_pData != nullptr, "envelope framebuffer available");
			if(Image.m_pData)
			{
				for(int Side = 0; Side < 2; ++Side)
				{
					CUIRect Graph = (Compare().*Access(TPanes{}))[Side];
					Graph.Margin(6, &Graph);
					Graph.HSplitTop(38, nullptr, &Graph);
					Graph.HSplitBottom(20, &Graph, nullptr);
					Graph.VSplitLeft(45, nullptr, &Graph);
					Graph.Margin(4, &Graph);
					bool FoundTerminal = false, FoundRepeated = false;
					const vec2 Terminal(Graph.x + Graph.w * (Side ? 1 : 0.5f), Graph.y + Graph.h - ((Side ? 3 : 2) + 0.24f) / 3.48f * Graph.h);
					for(int y = (int)Graph.y; y < Graph.y + Graph.h; ++y)
						for(int x = (int)Graph.x; x < Graph.x + Graph.w; ++x)
						{
							const auto C = Image.PixelColor((int)(x / Ui()->Screen()->w * Image.m_Width), (int)(y / Ui()->Screen()->h * Image.m_Height));
							const bool Strong = C.r > 0.75f && C.g < 0.5f && C.b < 0.5f;
							FoundRepeated |= !Side && x > Graph.x + Graph.w * 0.6f && Strong;
							FoundTerminal |= distance(vec2(x, y), Terminal) < 7 && (Strong || C.g > 0.75f);
						}
					Check(FoundTerminal, "envelope graph shows exact non-looping terminal value side " + std::to_string(Side));
					Check(!FoundRepeated, "shorter envelope has no repetition or extrapolation");
				}
				Image.Free();
			}
			for(int Side = 0; Side < 2; ++Side)
			{
				// Linear samples are away from all point markers, including a
				// near-terminal sample still 11+ pixels from the endpoint.
				CheckGraphPixel(Side, Side ? 0.37f : 0.185f, ((Side ? 3 : 2) * 0.37f + 0.24f) / 3.48f, ColorRGBA(1, 0.2f, 0.2f, 1), "linear curve interior independently rendered");
				CheckGraphPixel(Side, Side ? 0.9f : 0.45f, ((Side ? 3 : 2) * 0.9f + 0.24f) / 3.48f, ColorRGBA(1, 0.2f, 0.2f, 1), "linear curve near-terminal interior independently rendered");
			}
			Check((Compare().*Access(TValueRows{})).size() >= 3, "envelope inspector includes exact fields and affected layers");
		});
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Position"); });
		Delay();
		Do([this] {
			// Before Bezier controls are 0,8,2,2; symmetric-time midpoint
			// is 0.25s and the independently computed cubic value is 4.
			CheckGraphPixel(0, 0.25f, 4.64f / 9.28f, ColorRGBA(1, 0.2f, 0.2f, 1), "Bezier cubic midpoint independently rendered");
			CheckGraphPixel(1, 0.5f, 3.64f / 9.28f, ColorRGBA(1, 0.2f, 0.2f, 1), "Bezier cubic midpoint independently rendered");
			// At cubic parameter 0.85, evaluate the time and value control
			// polygons independently, staying clear of the endpoint markers.
			CheckGraphPixel(0, 0.44285f, (2.3375f + 0.64f) / 9.28f, ColorRGBA(1, 0.2f, 0.2f, 1), "Bezier near-terminal interior independently rendered");
			CheckGraphPixel(1, 0.912475f, (3.04725f + 0.64f) / 9.28f, ColorRGBA(1, 0.2f, 0.2f, 1), "Bezier near-terminal interior independently rendered");
		});
		for(int Side = 0; Side < 2; ++Side)
		{
			Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Overlap"); });
			Delay();
			Click([this] {
				const auto &Rows = Compare().*Access(TNavigator{});
				const auto It = std::find(Rows.begin(), Rows.end(), FindEntry("Overlap"));
				return vec2(100, NavigatorTop() + (It - Rows.begin()) * 36 + 18);
			});
			for(int Point : {2, 1})
			{
				Click([this, Side, Point] {
					const auto Graph = EnvelopeGraph(Side);
					return vec2(Graph.x + Graph.w * (Point == 2 ? 0.51f : 0.5f), Graph.y + Graph.h * (1 - ((Side ? 2 : 1) + 0.24f) / 3.48f));
				});
				Do([this, Point] {
					Check(SelectedDetail(Compare()) == std::array{FindEntry("Overlap"), 0, Point - 1}, "nearest overlapping marker selects exactly one point");
					const auto &Values = Compare().*Access(TValueRows{});
					Check(!Values.empty() && Values[0].m_Name == "Point " + std::to_string(Point) + ": Channel 0 value (1/1024)" && Values[0].m_Before == "1024" && Values[0].m_After == "2048", "marker click selects exact inspector values");
				});
			}
			Press(SDLK_DOWN);
			Do([this] { Check(SelectedDetail(Compare()) == std::array{FindEntry("Overlap"), 0, 1}, "arrow after marker click navigates inspector, not navigator"); });
			Press(SDLK_UP);
			Do([this] { Check(SelectedDetail(Compare()) == std::array{FindEntry("Overlap"), 0, 0}, "reverse arrow after marker click stays in inspector"); });
		}
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Large"); });
		Delay();
		Screenshot("comparison-envelope-large");
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
				CheckGraphPixel(Side, 0.37f, 0.5f, ColorRGBA(1, 0.2f, 0.2f, 1), "constant large-value envelope has a finite visible curve");
			const auto &Values = Compare().*Access(TValueRows{});
			Check(!Values.empty() && Values[0].m_Before == "2147483647" && Values[0].m_After == "2147483646", "large envelope retains exact fixed-point inspector values");
		});
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Dense"); });
		Delay();
		Do([this] {
			// Point 777 is beyond the first 256 changed points and between
			// overview samples 775 and 779 in this 1,025-point envelope.
			(Compare().*Access(TSelectDetail{}))(777);
		});
		Delay();
		Click([this] { return SectionPosition(3); });
		Click([this] { return SectionPosition(2); });
		Delay(20);
		Do([this] {
			const auto &Scroll = (Compare().*Access(TDetails{})).*Access(TListScroll{});
			const auto Rect = Scroll.*Access(TScrollRect{});
			const float Y = Rect.y + (Scroll.*Access(TScrollOffset{})) + 777 * 19 + 9;
			Check((Compare().*Access(TSelectedDetail{})) == 777 && Rect.Inside(vec2(Rect.x + 10, Y)), "returning to envelopes reveals the saved distant inspector detail");
		});
		Screenshot("comparison-envelope-dense");
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
				CheckGraphPixel(Side, 777.0f / 1024, ((Side ? 2 : 1) + 0.16f) / 2.32f, ColorRGBA(1, 1, 1, 1), "dense selected point outside overview has a white marker");
			const auto &Values = Compare().*Access(TValueRows{});
			Check(!Values.empty() && Values[0].m_Name == "Point 777: Channel 0 value (1/1024)" && Values[0].m_Before == "1024" && Values[0].m_After == "2048", "dense selected point has exact inspector fields");
		});
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Color"); });
		Delay();
		Do([this] {
			Compare().*Access(TZoom{}) = 4;
			const auto P = (Compare().*Access(TPanes{}))[0].Center();
			Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
			SDL_Event E{};
			E.type = SDL_MOUSEWHEEL;
			E.wheel.y = -1;
			SDL_PushEvent(&E);
			Mouse(SDL_BUTTON_LEFT, true);
		});
		Delay();
		Do([this] { Ui()->OnCursorMove(30, 30); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); Check((Compare().*Access(TZoom{})) == 4 && (Compare().*Access(TCenter{})) == m_SavedCenter, "resource wheel and drag leave map camera unchanged"); });
		Delay();
		Click([this] { const auto P = (Compare().*Access(TPanes{}))[0]; return vec2(P.x + 25, P.y + 34); });
		Do([this] { Check((Compare().*Access(TEnvelopeChannels{})) == 0, "native envelope channel toggle affects paired graphs"); });
		Screenshot("comparison-envelope-no-channels");
		Click([] { return vec2(106, 62); });
		WaitReady();
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Removed"); Check((Compare().*Access(TEnvelopeChannels{})) == 15, "Swap resets envelope channel state"); });
		Delay();
		Screenshot("comparison-envelope-added");
		Do([this] { CheckSources("resource views, channel toggles and Swap"); Compare().*Access(TTreeActive{}) = false; CloseComparison(); });
		Delay();
		Do([this] { OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		Do([this] { Check(Compare().*Access(TTreeActive{}), "reopen restores navigator keyboard focus"); });
		Click([this] { return SectionPosition(2); });
		Press(SDLK_DOWN);
		Do([this] { Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Position", "reopened envelope section accepts navigator keyboard input"); });
		Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Color"); });
		Delay();
		Do([this] { CheckSources("resource view reopening"); Graphics()->Resize(1280, 800, 0); OnWindowResize(); CloseComparison(); });
		Delay();
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto *pMap = Side ? m_pAfter : m_pBefore;
				pMap->m_vSettings.clear();
				for(int i = 0; i < 6; ++i)
				{
					const std::string Command = "tune " + std::to_string(i) + " " + (Side ? "after " : "before ") + std::string(210, 'W');
					pMap->m_vSettings.emplace_back(Command.c_str());
				}
			}
			CaptureSources();
			Graphics()->Resize(800, 600, 0);
			OnWindowResize();
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this] {
			SelectKind(MapDiff::EEntryKind::SETTINGS);
			const auto P = (Compare().*Access(TPanes{}))[0].Center();
			Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
		});
		Delay();
		for(int i = 0; i < 5; ++i)
		{
			Do([] { SDL_Event E{}; E.type = SDL_MOUSEWHEEL; E.wheel.y = -1; SDL_PushEvent(&E); });
			Delay(2);
		}
		Delay(30);
		Do([this] {
			const auto &Scroll = Compare().*Access(TSettings{});
			m_Log << "SETTINGS narrow width=" << (Scroll.*Access(TScrollRect{})).w << " content=" << (Scroll.*Access(TContentSize{})) << " viewport=" << (Scroll.*Access(TScrollRect{})).h << " scroll=" << (Scroll.*Access(TScrollPos{})) << std::endl;
			Check(Scroll.ContentOverflows() && (Scroll.*Access(TScrollPos{})) > 50, "wrapped settings overflow and scroll at narrow width");
			m_SavedEntry = Compare().*Access(TSelectedDetail{});
			Graphics()->Resize(1600, 600, 0);
			OnWindowResize();
		});
		Delay(30);
		Do([this] {
			const auto &Scroll = Compare().*Access(TSettings{});
			m_Log << "SETTINGS wide width=" << (Scroll.*Access(TScrollRect{})).w << " content=" << (Scroll.*Access(TContentSize{})) << " viewport=" << (Scroll.*Access(TScrollRect{})).h << " scroll=" << (Scroll.*Access(TScrollPos{})) << std::endl;
			Check(!Scroll.ContentOverflows() && (Scroll.*Access(TScrollPos{})) == 0, "settings resize-to-fit clears stored scrolling");
			Check((Compare().*Access(TSelectedDetail{})) == m_SavedEntry, "settings resize-to-fit preserves selection");
			Graphics()->Resize(800, 600, 0);
			OnWindowResize();
		});
		Delay(30);
		Do([this] {
			const auto &Scroll = Compare().*Access(TSettings{});
			Check(Scroll.ContentOverflows() && (Scroll.*Access(TScrollPos{})) == 0, "settings resize-back cannot resurrect old scrolling");
			Check((Compare().*Access(TSelectedDetail{})) == m_SavedEntry, "settings resize-back preserves selection");
			CheckSources("settings width transitions");
			Graphics()->Resize(1280, 800, 0);
			OnWindowResize();
			CloseComparison();
		});
		Delay();
	}
	void SectionScrollSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_copy(pMap->m_aDisplayName, "Scroll fixture");
				pMap->m_vpGroups.clear();
				pMap->m_pGameGroup = nullptr;
				WhiteImage(*pMap, "scroll", true);
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Scroll");
				for(int n = 0; n < 60; ++n)
				{
					auto pTiles = std::make_shared<CLayerTiles>(pMap.get(), 8, 8);
					str_format(pTiles->m_aName, sizeof(pTiles->m_aName), "Layer %d", n);
					pTiles->m_Image = 0;
					pTiles->m_pTiles[n % 64].m_Index = Side && n < 24 ? 2 : 1;
					pGroup->AddLayer(pTiles);
				}
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Click([] { return vec2(240, 62); });
		Do([this] {
			Check((Compare().*Access(TChangedNavigator{})).size() == 24, "scroll fixture overflows with changed layers");
			Ui()->OnCursorMove((100 - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (300 - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
		});
		Delay();
		for(int i = 0; i < 8; ++i)
		{
			Do([] { SDL_Event E{}; E.type = SDL_MOUSEWHEEL; E.wheel.y = -1; SDL_PushEvent(&E); });
			Delay(2);
		}
		Delay(30);
		Do([this] {
			auto &Scroll = ((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{});
			m_SavedScroll = Scroll.*Access(TScrollPos{});
			Check(m_SavedScroll > 100, "mouse wheel scrolls the navigator to the unchanged header");
			m_SavedEntry = Compare().*Access(TSelectedEntry{});
			m_SavedCenter = Compare().*Access(TCenter{});
		});
		auto Header = [this] {
			const auto &Scroll = ((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{});
			return vec2(100, NavigatorTop() + 24 * 36 + 18 + (Scroll.*Access(TScrollOffset{})));
		};
		Click(Header);
		Delay(15);
		Do([this] {
			const auto &Scroll = ((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{});
			Check((Compare().*Access(TUnchangedExpanded{})) && std::abs((Scroll.*Access(TScrollPos{})) - m_SavedScroll) < 0.1f, "expanding unchanged retains scrolled viewport");
			Check((Compare().*Access(TSelectedEntry{})) == m_SavedEntry && (Compare().*Access(TCenter{})) == m_SavedCenter, "expanding unchanged retains inspection");
			(Compare().*Access(TSelect{}))(FindEntry("Layer 40"));
			m_SavedCenter = Compare().*Access(TCenter{});
		});
		Click(Header);
		Delay(15);
		Do([this] {
			const auto &Scroll = ((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{});
			Check(!(Compare().*Access(TUnchangedExpanded{})) && std::abs((Scroll.*Access(TScrollPos{})) - m_SavedScroll) < 0.1f, "collapsing unchanged retains scrolled viewport");
			Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Layer 40") && (Compare().*Access(TCenter{})) == m_SavedCenter, "collapsing unchanged preserves a hidden selection and camera");
		});
		Click([this] { return SectionPosition(1); });
		Click([this] { return SectionPosition(0); });
		Do([this] { Check(std::abs(((((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{})).*Access(TScrollPos{})) - m_SavedScroll) < 0.1f, "sidebar views retain independent scroll positions"); });
		Click([this] { return SectionPosition(1); });
		DoubleClick([this] { return TreePosition("Scroll"); });
		Click([this] { return TreePosition("Scroll"); });
		Do([this] { Key(SDLK_n, true); });
		Delay(2);
		Do([this] { Key(SDLK_n, false); Check(std::get<1>(LayerState(Compare(), FindEntry("Scroll"))), "Next reveals the focused child's collapsed parent"); });
		Click([this] { return TreePosition("Scroll", 1); });
		Do([this] { CheckSources("scroll and tree navigation"); });
		Click([] { return vec2(106, 62); });
		WaitReady();
		Do([this] { Check(std::get<0>(LayerState(Compare(), FindEntry("Scroll"))) && std::get<2>(LayerState(Compare(), FindEntry("Scroll"))) == 24, "Swap resets visibility without accumulating group counts"); });
		Click([] { return vec2(106, 62); });
		WaitReady();
		Do([this] { Check(std::get<2>(LayerState(Compare(), FindEntry("Scroll"))) == 24, "repeated Swap retains exact aggregate counts"); CloseComparison(); });
		Delay();
		Do([this] { OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		Do([this] { Check(std::get<0>(LayerState(Compare(), FindEntry("Scroll"))) && (Compare().*Access(TSection{})) == 0, "reopening resets viewer visibility and sidebar view"); CloseComparison(); });
		Delay();
		Do([this] {
			for(int n = 3; n < 60; ++n)
				std::static_pointer_cast<CLayerTiles>(m_pAfter->m_vpGroups[0]->m_vpLayers[n])->m_pTiles[n % 64].m_Index = 1;
			for(auto *pMap : {m_pBefore, m_pAfter})
			{
				auto pLarge = pMap->m_vpGroups[0];
				auto pSmall = pMap->NewGroup();
				str_copy(pSmall->m_aName, "Small");
				pSmall->m_vpLayers.assign(pLarge->m_vpLayers.begin(), pLarge->m_vpLayers.begin() + 3);
				pLarge->m_vpLayers.erase(pLarge->m_vpLayers.begin(), pLarge->m_vpLayers.begin() + 3);
				std::swap(pMap->m_vpGroups[0], pMap->m_vpGroups[1]);
			}
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Click([] { return vec2(240, 62); });
		Click([this] { return vec2(100, NavigatorTop() + 3 * 36 + 18); });
		auto ScrollOnce = [this] {
			Do([this] { Ui()->OnCursorMove((100 - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (300 - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h); });
			Delay();
			Do([] { SDL_Event E{}; E.type = SDL_MOUSEWHEEL; E.wheel.y = -1; SDL_PushEvent(&E); });
			Delay(30);
		};
		ScrollOnce();
		Do([this] { Check(((((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{})).*Access(TScrollPos{})) > 50, "expanded section scrolls before collapse-to-fit"); });
		Click([this] { return vec2(100, NavigatorTop() + 3 * 36 + 18 + ((((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{})).*Access(TScrollOffset{}))); });
		Do([this] { Check(((((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{})).*Access(TScrollPos{})) == 0, "section collapse clamps stored scrolling when content fits"); });
		Click([this] { return vec2(100, NavigatorTop() + 3 * 36 + 18); });
		Do([this] { Check(((((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})]).*Access(TListScroll{})).*Access(TScrollPos{})) == 0, "section expansion cannot resurrect old scroll position"); });
		Click([this] { return SectionPosition(1); });
		ScrollOnce();
		Do([this] { Check(((Compare().*Access(TLayerTree{})).*Access(TScrollPos{})) > 20, "layer tree scrolls before collapse-to-fit"); });
		DoubleClick([this] { return TreePosition("Scroll"); });
		Do([this] { Check(((Compare().*Access(TLayerTree{})).*Access(TScrollPos{})) == 0, "tree collapse clamps stored scrolling when content fits"); });
		DoubleClick([this] { return TreePosition("Scroll"); });
		Do([this] { Check(((Compare().*Access(TLayerTree{})).*Access(TScrollPos{})) == 0, "tree expansion cannot resurrect old scroll position"); });
		Click([this] { return TreePosition("Small"); });
		auto Press = [this](SDL_Keycode K) {
			Do([this, K] { Key(K, true); });
			Delay(2);
			Do([this, K] { Key(K, false); });
			Delay(2);
		};
		Press(SDLK_DOWN);
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Layer 0"), "Down moves the layer tree selection rather than inspector details"); });
		Press(SDLK_END);
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Layer 59"), "End selects and reveals the final tree layer"); });
		Press(SDLK_LEFT);
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Scroll"), "Left selects the layer's parent group"); });
		Press(SDLK_LEFT);
		Do([this] { Check(!std::get<1>(LayerState(Compare(), FindEntry("Scroll"))), "Left folds the selected group"); });
		Press(SDLK_RIGHT);
		Do([this] { Check(std::get<1>(LayerState(Compare(), FindEntry("Scroll"))), "Right expands the selected group"); });
		ToggleMapContext();
		Click([this] { return TreePosition("Layer 3"); });
		Do([this] { Check((Compare().*Access(TContext{})) == 0, "unchanged tree inspection retains explicit context mode"); CheckPaneNotice(); });
		Click([this] { return TreePosition("Layer 0", 1); });
		Click([this] { return TreePosition("Small"); });
		Do([this] { Check(SelectedDetail(Compare())[0] == FindEntry("Layer 0") && (Compare().*Access(TFocusEntry{})) == FindEntry("Small"), "group inspection initially selects a hidden child detail"); CheckPaneNotice(); });
		Do([this] { Graphics()->Resize(800, 600, 0); OnWindowResize(); });
		Delay();
		Screenshot("comparison-layer-tree-compact");
		Do([this] { Graphics()->Resize(1280, 800, 0); OnWindowResize(); CloseComparison(); });
		Delay();
	}
	void MeasureMotion(const std::string &Name)
	{
		Do([this] { m_MeasureRender = true; m_SavedCenter = Compare().*Access(TCenter{}); });
		Delay(1);
		m_vSteps.push_back([this, Name, Count = 0, Total = 0.0, Maximum = 0.0]() mutable {
			Total += m_LastRenderMs;
			Maximum = std::max(Maximum, m_LastRenderMs);
			if(++Count < 32)
			{
				Compare().*Access(TCenter{}) = m_SavedCenter + vec2((Count % 8) * 1500, (Count % 3) * 350);
				Compare().*Access(TZoom{}) = Count % 2 ? 20.0f : 10.0f;
				return false;
			}
			m_Log << "TIMING motion " << Name << " mean_ms=" << Total / Count << " max_ms=" << Maximum << std::endl;
			m_MeasureRender = false;
			Compare().*Access(TCenter{}) = m_SavedCenter;
			Compare().*Access(TZoom{}) = 20.0f;
			return true;
		});
	}
	void MeasureFrames(const std::string &Name)
	{
		Do([this] { m_MeasureRender = true; });
		Delay(2);
		m_vSteps.push_back([this, Name, Count = 0, Total = 0.0, Maximum = 0.0]() mutable {
			Total += m_LastRenderMs;
			Maximum = std::max(Maximum, m_LastRenderMs);
			if(++Count < 5)
				return false;
			m_Log << "TIMING render " << Name << " zoom=" << (Compare().*Access(TZoom{})) << " mean_ms=" << Total / Count << " max_ms=" << Maximum << std::endl;
			m_MeasureRender = false;
			return true;
		});
	}
	void PerformanceSteps(bool LocalMaps)
	{
		Do([this, LocalMaps] {
			for(int Side = 0; Side < 2; ++Side)
			{
				CEditorMap *pMap;
				if(LocalMaps)
				{
					Check(Load(Side ? "maps/perf-after.map" : "maps/perf-before.map", IStorage::TYPE_SAVE), "load local performance fixture");
					pMap = Map();
				}
				else
				{
					auto pNew = std::make_unique<CEditorMap>(this);
					pNew->CreateDefault();
					str_copy(pNew->m_aDisplayName, "Fixture");
					WhiteImage(*pNew, "dense", true);
					auto pTiles = std::make_shared<CLayerTiles>(pNew.get(), 2048, 1024);
					pTiles->m_Image = 0;
					for(int n = 0; n < pTiles->m_Width * pTiles->m_Height; ++n)
						pTiles->m_pTiles[n].m_Index = Side && n < 64 * pTiles->m_Width ? 2 : 1;
					pNew->NewGroup()->AddLayer(pTiles);
					pMap = pNew.get();
					Maps().push_back(std::move(pNew));
				}
				(Side ? m_pAfter : m_pBefore) = pMap;
			}
			this->*Access(TSelectedMap{}) = Maps().size() - 2;
			// A layer property edit exercises whole-layer drawing even with context off.
			std::shared_ptr<CLayerTiles> pLargest;
			std::shared_ptr<CLayerGroup> pLargestGroup;
			size_t MostTiles = 0;
			for(const auto &pGroup : m_pAfter->m_vpGroups)
				for(const auto &pLayer : pGroup->m_vpLayers)
					if(pLayer->m_Type == LAYERTYPE_TILES)
					{
						auto pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
						size_t Nonempty = 0;
						for(int n = 0; n < pTiles->m_Width * pTiles->m_Height; ++n)
							Nonempty += pTiles->m_pTiles[n].m_Index != 0;
						if(!pLargest || Nonempty > MostTiles)
						{
							pLargest = pTiles;
							pLargestGroup = pGroup;
							MostTiles = Nonempty;
						}
					}
			Check(pLargest != nullptr, "performance fixture has tile layer");
			if(pLargest)
			{
				str_copy(pLargest->m_aName, "Perf layer");
				str_copy(pLargestGroup->m_aName, "Perf group");
				m_Log << "TIMING fixture dimensions=" << pLargest->m_Width << "x" << pLargest->m_Height << " nonempty=" << MostTiles << std::endl;
			}
			if(LocalMaps)
			{
				MapDiff::CSnapshot Before, After;
				const auto Start = std::chrono::steady_clock::now();
				MapDiff::Capture(*m_pBefore, Before);
				MapDiff::Capture(*m_pAfter, After);
				const auto Captured = std::chrono::steady_clock::now();
				const auto Result = MapDiff::Compare(Before, After);
				const auto Finished = std::chrono::steady_clock::now();
				m_Log << "TIMING local capture_pair_ms=" << std::chrono::duration<double, std::milli>(Captured - Start).count() << " pure_compare_ms=" << std::chrono::duration<double, std::milli>(Finished - Captured).count() << " changes=" << Result.ChangeCount() << std::endl;
			}
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this] {
			SelectKind(MapDiff::EEntryKind::LAYER, "Perf layer");
			Check((Compare().*Access(TZoom{})) <= 20.0f, "large layer automatic fit stays at inspection scale");
		});
		if(LocalMaps)
		{
			auto pCount = std::make_shared<uint64_t>();
			Do([this, pCount] { *pCount = m_CaptureCount; Tabs().SelectMap(*this, m_pBefore); });
			Delay();
			Do([this] { const auto Start = std::chrono::steady_clock::now(); Tabs().Open(*this, m_pBefore, m_pAfter); m_Log << "TIMING unchanged_tab_request_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count() << std::endl; });
			WaitReady();
			Do([this, pCount] { Check(m_CaptureCount == *pCount, "large unchanged tab switch performs zero captures"); Tabs().SelectMap(*this, m_pAfter); });
			Delay();
			Do([this, pCount] { for(int i = 0; i < 20; ++i) { m_pAfter->m_MapInfo.m_aVersion[0] = 'a' + i; m_pAfter->OnModify(); } Check(m_CaptureCount == *pCount, "large hidden repeated edits perform zero captures"); Tabs().Open(*this, m_pBefore, m_pAfter); });
			WaitReady();
			Do([this, pCount] { Check(m_CaptureCount == *pCount + 1, "large pending tab coalesces edits into one capture"); m_Log << "TIMING refresh_captures=" << m_CaptureCount - *pCount << std::endl; });
		}
		MeasureFrames(LocalMaps ? "local context off" : "dense context off");
		if(LocalMaps)
			Screenshot("performance-layer");
		Click([] { return vec2(240, 62); });
		MeasureFrames(LocalMaps ? "local selection context" : "dense selection context");
		Click([] { return vec2(240, 62); });
		MeasureFrames(LocalMaps ? "local context on" : "dense context on");
		Click([this] { return SectionPosition(1); });
		MeasureFrames(LocalMaps ? "local full context with layer tree" : "dense full context with layer tree");
		if(LocalMaps)
		{
			Screenshot("performance-layer-tree");
			Do([this] { Graphics()->Resize(800,600,0); OnWindowResize(); });
			Delay();
			Screenshot("performance-layer-tree-800x600");
			Do([this] { Graphics()->Resize(1280,800,0); OnWindowResize(); });
			Delay();
		}
		Click([this] { return SectionPosition(0); });
		if(LocalMaps)
			Screenshot("performance-full-context");
		MeasureMotion(LocalMaps ? "local full context pan/zoom" : "dense full context pan/zoom");
		Do([this] {
			const auto P = (Compare().*Access(TPanes{}))[0].Center();
			Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
		});
		Delay();
		Do([this] {
			IInput::CEvent Event{};
			Event.m_Key = KEY_MOUSE_WHEEL_DOWN;
			Event.m_Flags = IInput::FLAG_PRESS;
			for(int i = 0; i < 80; ++i)
				Compare().OnInput(*this, Event);
			Check((Compare().*Access(TZoom{})) <= 20.0f, "wheel cannot zoom out to an unbounded tile workload");
		});
		MeasureFrames(LocalMaps ? "local maximum zoom out" : "dense maximum zoom out");
		Click([] { return vec2(167, 62); });
		Do([this] { Check((Compare().*Access(TZoom{})) <= 20.0f, "large group fit stays at inspection scale"); });
		MeasureFrames(LocalMaps ? "local group context on" : "dense group context on");
		MeasureMotion(LocalMaps ? "local group full context pan/zoom" : "dense group full context pan/zoom");
		ToggleMapContext();
		MeasureFrames(LocalMaps ? "local group context off" : "dense group context off");
		Do([this] { CloseComparison(); });
		Delay();
		if(!LocalMaps)
		{
			Do([this] {
				auto pGroup = m_pAfter->m_vpGroups.back();
				str_copy(pGroup->m_aName, m_pBefore->m_vpGroups.back()->m_aName);
				str_copy(pGroup->m_vpLayers.back()->m_aName, m_pBefore->m_vpGroups.back()->m_vpLayers.back()->m_aName);
				Compare().*Access(TGroup{}) = false;
				OpenComparison(*this, m_pBefore, m_pAfter);
			});
			WaitReady();
			Do([this] {
				SelectKind(MapDiff::EEntryKind::LAYER);
				const auto &Entry = Result().m_vEntries[Compare().*Access(TSelectedEntry{})];
				Check(Entry.m_vProperties.empty() && Entry.m_vTiles.size() == 64u * 2048, "large tile-only changes exercise sparse rendering");
				Compare().*Access(TCenter{}) = vec2(2040 * 32 + 16, 32 * 32 + 16);
				Compare().*Access(TZoom{}) = 1.0f;
			});
			Delay();
			Do([this] { CheckPixels(vec2(2040 * 32 + 16, 32 * 32 + 16)); CheckBatchedMarkers(); Compare().*Access(TZoom{}) = 20.0f; });
			MeasureFrames("dense tile-only context off at far edge");
			ToggleMapContext();
			MeasureFrames("dense tile-only context on at far edge");
			Do([this] { CloseComparison(); });
			Delay();
		}
	}
	void CheckSkyPixels(bool Inspection, bool Visible = true, bool GroupSelected = false)
	{
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "textured sky framebuffer available");
		if(!Image.m_pData)
			return;
		for(int Side = 0; Side < 2; ++Side)
			for(int Probe = 0; Probe < 2; ++Probe)
			{
				const auto Pane = (Compare().*Access(TPanes{}))[Side];
				const vec2 Projection = vec2(-20, 10) + (Inspection ? Compare().*Access(TCenter{}) : vec2(0, 0));
				// Position envelope at time zero translates the textured quad by (32,16).
				const vec2 Point = Pane.Center() + (vec2(32 + (Probe ? 24 : -24), 16) - Projection) / (Compare().*Access(TZoom{}));
				Check(Pane.Inside(Point), "zero-parallax textured quad reachable");
				if(!Pane.Inside(Point))
					continue;
				const auto C = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
				const float Alpha = !Visible ? 0 : Inspection ? 0.65f :
										0.25f;
				const ColorRGBA Expected(mix(0.08f, Probe ? 0.0f : 0.75f, Alpha), 0.09f * (1 - Alpha), mix(0.11f, Probe ? 1.0f : 0.0f, Alpha), 1);
				Check(std::abs(C.r - Expected.r) < 0.015f && std::abs(C.g - Expected.g) < 0.015f && std::abs(C.b - Expected.b) < 0.015f, "textured sky keeps source texture and envelopes under camera projection");
			}
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto Pane = (Compare().*Access(TPanes{}))[Side];
			const vec2 Projection = vec2(-20, 10) + (Inspection ? Compare().*Access(TCenter{}) : vec2(0, 0));
			const vec2 Point = Pane.Center() + (vec2(-120, 0) - Projection) / (Compare().*Access(TZoom{}));
			Check(Pane.Inside(Point), "overlapping textured quads reachable");
			if(Pane.Inside(Point))
			{
				const auto C = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
				const float Alpha = !Visible ? 0 : Inspection ? 0.65f :
										0.25f;
				// Opaque red then blue source quads each receive context alpha.
				const float Background = (1 - Alpha) * (1 - Alpha);
				const ColorRGBA Expected(0.08f * Background + Alpha * (1 - Alpha), 0.09f * Background, 0.11f * Background + Alpha, 1);
				Check(std::abs(C.r - Expected.r) < 0.015f && std::abs(C.g - Expected.g) < 0.015f && std::abs(C.b - Expected.b) < 0.015f, "visible quad batching preserves source order and alpha");
			}
		}
		if(Inspection && Visible)
			for(int Side = 0; Side < 2; ++Side)
			{
				const auto Pane = (Compare().*Access(TPanes{}))[Side];
				const vec2 Point = Pane.Center() + (vec2(160, 0) - vec2(-20, 10) - (Compare().*Access(TCenter{}))) / (Compare().*Access(TZoom{}));
				Check(Pane.Inside(Point), "selected zero-parallax sibling reachable");
				if(Pane.Inside(Point))
				{
					const auto C = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
					const float Alpha = GroupSelected ? 0.65f : 0.25f;
					Check(std::abs(C.g - mix(0.09f, 1.0f, Alpha)) < 0.015f, "unchanged sibling shares selected owning-group inspection projection");
				}
			}
		Image.Free();
	}
	void SectionAndSkySteps()
	{
		auto pChangesState = std::make_shared<std::tuple<vec2, float, int, bool>>();
		Do([this] {
			Graphics()->Resize(1280, 800, 0);
			OnWindowResize();
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				str_copy(pMap->m_aDisplayName, Side ? "Sections After" : "Sections Before");
				pMap->m_aFilename[0] = 0;
				pMap->m_MapViewState.m_WorldOffset = vec2(0, 0);
				auto pImage = WhiteImage(*pMap, "sky", false);
				for(int Y = 0; Y < 256; ++Y)
					for(int X = 0; X < 256; ++X)
						pImage->SetPixelColor(X, Y, X < 128 ? ColorRGBA(1, 0, 0, 1) : ColorRGBA(0, 0, 1, 1));
				pImage->m_Texture = Graphics()->LoadTextureRaw(*pImage, Graphics()->TextureLoadFlags(), "sky");
				for(int Env = 0; Env < 3; ++Env)
				{
					auto pEnv = std::make_shared<CEnvelope>(Env == 1 ? 3 : 4);
					str_copy(pEnv->m_aName, Env == 0 ? "Tint" : Env == 1 ? "Position" :
											       "Changed");
					pEnv->AddPoint(CFixedTime(0), Env == 1 ? std::array{32 * 1024, 16 * 1024, 0, 0} : std::array{Env == 2 && Side ? 512 : 768, 1024, 1024, 1024});
					pMap->m_vpEnvelopes.push_back(pEnv);
				}
				for(int Index = 0; Index < 80; ++Index)
				{
					auto pEnv = std::make_shared<CEnvelope>(1);
					str_format(pEnv->m_aName, sizeof(pEnv->m_aName), "Equal %02d", Index);
					pEnv->AddPoint(CFixedTime(0), {Index, 0, 0, 0});
					pMap->m_vpEnvelopes.push_back(pEnv);
				}
				pMap->m_vSettings.emplace_back(Side ? "tune gravity 0.5" : "tune gravity 1");
				auto pSky = pMap->NewGroup();
				str_copy(pSky->m_aName, "Sky");
				pSky->m_ParallaxX = pSky->m_ParallaxY = 0;
				pSky->m_OffsetX = -20;
				pSky->m_OffsetY = 10;
				auto pQuads = std::make_shared<CLayerQuads>(pMap.get());
				str_copy(pQuads->m_aName, "Sky tex");
				pQuads->m_Image = 0;
				pQuads->NewQuad(0, 0, 128, 96);
				pQuads->m_vQuads[0].m_ColorEnv = 0;
				pQuads->m_vQuads[0].m_PosEnv = 1;
				for(int Color = 0; Color < 2; ++Color)
				{
					pQuads->NewQuad(-120, 0, 32, 32);
					for(auto &Texcoord : pQuads->m_vQuads.back().m_aTexcoords)
						Texcoord = {Color ? 768 : 256, 512};
				}
				pSky->AddLayer(pQuads);
				auto pSibling = std::make_shared<CLayerQuads>(pMap.get());
				str_copy(pSibling->m_aName, "Sky sib");
				pSibling->NewQuad(160, 0, 32, 32);
				for(auto &Color : pSibling->m_vQuads[0].m_aColors)
					Color = {0, 255, 0, 255};
				pSky->AddLayer(pSibling);
				auto pTerrain = pMap->NewGroup();
				str_copy(pTerrain->m_aName, "Terrain");
				auto pTiles = std::make_shared<CLayerTiles>(pMap.get(), 2048, 8);
				str_copy(pTiles->m_aName, "Distant");
				pTiles->m_pTiles[1800 + 5 * 2048].m_Index = Side ? 2 : 1;
				pTerrain->AddLayer(pTiles);
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this] {
			SelectKind(MapDiff::EEntryKind::LAYER, "Distant");
			for(int Index : Compare().*Access(TNavigator{}))
				Check(Index < 0 || (Result().m_vEntries[Index].m_Kind != MapDiff::EEntryKind::ENVELOPE && Result().m_vEntries[Index].m_Kind != MapDiff::EEntryKind::SETTINGS), "Changes excludes dedicated resource sections");
		});
		Click([] { return vec2(240, 62); });
		Click([] { return vec2(240, 62); });
		Do([this] { Check((Compare().*Access(TCenter{})).x > 50000, "distant tile fit exercises far camera"); CheckSkyPixels(false); });
		Screenshot("sections-distant-textured-sky");
		auto Move = [this](vec2 P) { Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h); };
		Do([this, Move] { Move((Compare().*Access(TPanes{}))[0].Center()); Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this, Move] { Move((Compare().*Access(TPanes{}))[0].Center() + vec2(16, 8)); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); });
		Delay();
		Do([this] { CheckSkyPixels(false); SDL_Event E{}; E.type = SDL_MOUSEWHEEL; E.wheel.y = -1; SDL_PushEvent(&E); });
		Delay();
		Do([this] { CheckSkyPixels(false); });
		Click([] { return vec2(240, 62); });
		Do([this] { CheckSkyPixels(false, false); });
		Click([] { return vec2(240, 62); });
		Do([this] { CheckSkyPixels(false, false); });
		Click([] { return vec2(240, 62); });
		Do([this, pChangesState] { *pChangesState = {Compare().*Access(TCenter{}), Compare().*Access(TZoom{}), Compare().*Access(TSelectedDetail{}), Compare().*Access(TGroup{})}; });
		Click([this] { return SectionPosition(1); });
		Click([this] { return TreePosition("Sky tex"); });
		Do([this] { Compare().*Access(TZoom{}) = std::max(1.5f, Compare().*Access(TZoom{})); });
		Delay();
		Do([this] { CheckSkyPixels(true); });
		Do([this, Move] { Move((Compare().*Access(TPanes{}))[0].Center()); Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this, Move] { Move((Compare().*Access(TPanes{}))[0].Center() + vec2(8, 8)); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); });
		Delay();
		Do([this] { CheckSkyPixels(true); });
		Click([this] { return TreePosition("Sky"); });
		Do([this] { CheckSkyPixels(true, true, true); });
		Screenshot("sections-selected-zero-parallax");
		Click([this] { return SectionPosition(2); });
		Do([this] { Check((Compare().*Access(TSection{})) == 2 && (Compare().*Access(TNavigator{})).size() == 83, "Envelopes exposes changed and equal entries"); });
		Click([this] { return vec2(100, NavigatorTop() + 36 + 18); });
		Do([this] { Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Tint", "equal envelope is directly selectable"); Key(SDLK_n, true); });
		Delay();
		Do([this] { Key(SDLK_n, false); Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Changed", "envelope N skips equal entries and map changes"); });
		Do([this] { Key(SDLK_END, true); });
		Delay();
		Do([this] { Key(SDLK_END, false); });
		Delay(20);
		auto pEnvelopeState = std::make_shared<std::pair<int, float>>();
		Do([this, pEnvelopeState] { const auto &Scroll = (Compare().*Access(TNavLists{}))[2].*Access(TListScroll{}); *pEnvelopeState = {Compare().*Access(TSelectedEntry{}), Scroll.*Access(TScrollPos{})}; Check(pEnvelopeState->second > 1000, "envelope navigator actually overflows and scrolls"); });
		Click([this] { return SectionPosition(3); });
		Click([this] { return SectionPosition(2); });
		Delay(20);
		Do([this, pEnvelopeState] { const auto &Scroll = (Compare().*Access(TNavLists{}))[2].*Access(TListScroll{}); Check((Compare().*Access(TSelectedEntry{})) == pEnvelopeState->first && std::abs((Scroll.*Access(TScrollPos{})) - pEnvelopeState->second) < 0.1f, "envelope navigator restores its distant selection and scroll"); });
		Click([this] { return SectionPosition(3); });
		Do([this] { Check((Compare().*Access(TSection{})) == 3 && (Compare().*Access(TNavigator{})).size() == 1, "Server settings has its own navigator"); Key(SDLK_n, true); });
		Delay();
		Do([this] { Key(SDLK_n, false); Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Kind == MapDiff::EEntryKind::SETTINGS, "settings N wraps inside its section"); });
		Click([this] { return SectionPosition(0); });
		Do([this, pChangesState] { Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Distant", "Changes restores its spatial selection"); Check(*pChangesState == std::tuple{Compare().*Access(TCenter{}), Compare().*Access(TZoom{}), Compare().*Access(TSelectedDetail{}), Compare().*Access(TGroup{})}, "different-entry round trip restores the pre-departure camera, detail and grouping"); m_SavedCenter = Compare().*Access(TCenter{}); m_SavedEntry = Compare().*Access(TSelectedEntry{}); });
		auto Drag = [this, Move](float Target) {
			Do([this, Move] { Move((Compare().*Access(TDivider{})).Center()); });
			Delay();
			Do([this] { Check(CursorType(*this) == 2, "native divider hover has resize cursor"); Mouse(SDL_BUTTON_LEFT, true); });
			Delay();
			Do([this, Move, Target] { Move(vec2(Target, (Compare().*Access(TDivider{})).Center().y)); });
			Delay();
			Do([this, Move] { Move((Compare().*Access(TPanes{}))[1].Center()); });
			Delay();
			Do([this, Move, Target] { const float Zoom = Compare().*Access(TZoom{}); IInput::CEvent Event{}; Event.m_Key = KEY_MOUSE_WHEEL_UP; Event.m_Flags = IInput::FLAG_PRESS; Compare().OnInput(*this, Event); Check(Ui()->ActiveItem() == &(Compare().*Access(TDivider{})) && (Compare().*Access(TPanes{}))[1].Inside(vec2(Ui()->MouseX(), Ui()->MouseY())) && (Compare().*Access(TZoom{})) == Zoom, "held divider blocks canvas wheel zoom"); Move(vec2(Target, (Compare().*Access(TDivider{})).Center().y)); });
			Delay();
			Do([this] { Check(!(Compare().*Access(TPanning{})) && (Compare().*Access(TCenter{})) == m_SavedCenter && (Compare().*Access(TSelectedEntry{})) == m_SavedEntry, "divider drag preserves selection and never pans canvas"); Mouse(SDL_BUTTON_LEFT, false); });
			Delay();
		};
		Drag(950);
		Do([this] { Check((Compare().*Access(TSidebarWidth{})) == 400 && (Compare().*Access(TPanes{}))[0].w >= 200, "wide divider clamps to usable paired canvases"); Graphics()->Resize(800, 600, 0); OnWindowResize(); });
		Delay();
		Do([this] { Check((Compare().*Access(TSidebarWidth{})) <= 355 && (Compare().*Access(TPanes{}))[0].w >= 200, "window shrink reclamps shared sidebar width"); });
		Screenshot("sections-resized-800");
		Drag(10);
		Do([this] { Check((Compare().*Access(TSidebarWidth{})) == 100, "narrow divider keeps section buttons usable"); });
		Screenshot("sections-narrow-800");
		auto ReadNarrowState = [this] { return std::tuple{Compare().*Access(TCenter{}), Compare().*Access(TZoom{}), Compare().*Access(TSelectedEntry{}), Compare().*Access(TSelectedDetail{}), Compare().*Access(TGroup{})}; };
		auto pNarrowChanges = std::make_shared<decltype(ReadNarrowState())>();
		auto pNarrowLayers = std::make_shared<decltype(ReadNarrowState())>();
		Do([pNarrowChanges, ReadNarrowState] { *pNarrowChanges = ReadNarrowState(); });
		Click([this] { return SectionPosition(2); });
		Do([this] { Check((Compare().*Access(TSection{})) == 2 && (Compare().*Access(TSidebarWidth{})) == 100, "Envelopes remains usable at minimum sidebar width"); });
		Click([this] { return SectionPosition(3); });
		Do([this] { Check((Compare().*Access(TSection{})) == 3 && (Compare().*Access(TSidebarWidth{})) == 100, "Server settings remains usable at minimum sidebar width"); });
		Click([this] { return SectionPosition(1); });
		Do([this] { Check((Compare().*Access(TSection{})) == 1 && (Compare().*Access(TSidebarWidth{})) == 100, "Layers remains usable at minimum sidebar width"); });
		Click([this] { return TreePosition("Sky"); });
		Do([this, pNarrowLayers, ReadNarrowState] { Check((Compare().*Access(TSelectedEntry{})) == FindEntry("Sky"), "minimum-width tree label selects its group"); *pNarrowLayers = ReadNarrowState(); });
		DoubleClick([this] { return TreePosition("Sky"); });
		Do([this, pNarrowLayers, ReadNarrowState] { Check(!std::get<1>(LayerState(Compare(), FindEntry("Sky"))) && ReadNarrowState() == *pNarrowLayers, "minimum-width group fold preserves selection and camera"); });
		DoubleClick([this] { return TreePosition("Sky"); });
		Do([this, pNarrowLayers, ReadNarrowState] { Check(std::get<1>(LayerState(Compare(), FindEntry("Sky"))) && ReadNarrowState() == *pNarrowLayers, "minimum-width group expand preserves selection and camera"); });
		Click([this] { return TreePosition("Sky tex", 1); });
		Do([this, pNarrowLayers, ReadNarrowState] { Check(!std::get<0>(LayerState(Compare(), FindEntry("Sky tex"))) && ReadNarrowState() == *pNarrowLayers, "minimum-width layer eye preserves selection and camera"); });
		Click([this] { return vec2(6 + (Compare().*Access(TSidebarWidth{})) / 2, Ui()->Screen()->h - 34); });
		Do([this, pNarrowLayers, ReadNarrowState] {
			bool AllVisible = true;
			for(int Index = 0; Index < (int)Result().m_vEntries.size(); ++Index)
				if(Result().m_vEntries[Index].m_Kind == MapDiff::EEntryKind::GROUP || Result().m_vEntries[Index].m_Kind == MapDiff::EEntryKind::LAYER)
					AllVisible &= std::get<0>(LayerState(Compare(), Index));
			Check(AllVisible && ReadNarrowState() == *pNarrowLayers && (Compare().*Access(TSidebarWidth{})) == 100, "minimum-width Show all restores comparison visibility and preserves selection and camera");
			CheckSources("minimum-width layer controls");
		});
		Screenshot("sections-narrow-tree-800");
		Click([this] { return SectionPosition(0); });
		Do([this, pNarrowChanges, ReadNarrowState] { Check((Compare().*Access(TSection{})) == 0 && (Compare().*Access(TSidebarWidth{})) == 100 && ReadNarrowState() == *pNarrowChanges, "minimum-width Changes return restores selection and camera"); m_SavedCenter = Compare().*Access(TCenter{}); m_SavedEntry = Compare().*Access(TSelectedEntry{}); });
		Drag(230);
		Do([this, Move] { Move((Compare().*Access(TDivider{})).Center()); m_SavedScroll = this->*Access(TToolboxWidth{}); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this, Move] { Move((Compare().*Access(TDivider{})).Center() + vec2(40, 0)); });
		Delay();
		Do([this] { Key(SDLK_ESCAPE, true); });
		Delay();
		Do([this] { Check(Tabs().Transitioning(), "leaving during divider drag waits for release"); Mouse(SDL_BUTTON_LEFT, false); Key(SDLK_ESCAPE, false); });
		Delay();
		auto pExtraState = std::make_shared<std::pair<EExtraEditor, float>>();
		Do([this, Move, pExtraState] {
			Check(!Compare().Active() && m_ToolBoxWidth == m_SavedScroll, "held comparison divider never resizes normal editor toolbox");
			CheckSources("sections resize and projection");
			*pExtraState = {m_ActiveExtraEditor, m_aExtraEditorSplits[EXTRAEDITOR_HISTORY]};
			m_ActiveExtraEditor = EXTRAEDITOR_HISTORY;
			Move(vec2(m_ToolBoxWidth - 0.5f, 180));
		});
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this, Move] { Move(vec2(m_SavedScroll + 34.5f, 180)); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); });
		Delay();
		Do([this, Move, pExtraState] {
			Check(m_ToolBoxWidth > m_SavedScroll + 20 && m_aExtraEditorSplits[EXTRAEDITOR_HISTORY] == pExtraState->second, "normal toolbox drag survives another divider rendered in the same frame; width=" + std::to_string(m_ToolBoxWidth) + " before=" + std::to_string(m_SavedScroll));
			Move(vec2(Ui()->Screen()->w - 40, Ui()->Screen()->h - 16 - m_aExtraEditorSplits[EXTRAEDITOR_HISTORY]));
		});
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this, Move, pExtraState] { Move(vec2(Ui()->Screen()->w - 40, Ui()->Screen()->h - 16 - pExtraState->second - 30)); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); });
		Delay();
		Do([this, pExtraState] {
			Check(m_aExtraEditorSplits[EXTRAEDITOR_HISTORY] > pExtraState->second + 20, "normal bottom editor divider also remains draggable");
			m_ToolBoxWidth = m_SavedScroll;
			m_ActiveExtraEditor = pExtraState->first;
			m_aExtraEditorSplits[EXTRAEDITOR_HISTORY] = pExtraState->second;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this, Move] { Move((Compare().*Access(TPanes{}))[0].Center()); });
		Delay();
		Do([this] { Check(CursorType(*this) == 0 && !(Compare().*Access(TPanning{})), "reopening resets divider drag and cursor"); CloseComparison(); });
		Delay();
		Do([this] {
			m_pAfter->m_vSettings = m_pBefore->m_vSettings;
			m_pAfter->m_vpEnvelopes[2]->m_vPoints[0].m_aValues[0] = 768;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Click([this] { return SectionPosition(2); });
		Do([this] { Check((Compare().*Access(TContext{})) == 0 && (Compare().*Access(TNavigator{})).size() == 83 && (Compare().*Access(TChangedNavigator{})).empty(), "equal envelopes remain accessible with context off"); });
		Click([this] { return SectionPosition(3); });
		Do([this] { Check((Compare().*Access(TContext{})) == 0 && (Compare().*Access(TNavigator{})).size() == 1 && (Compare().*Access(TChangedNavigator{})).empty(), "equal settings remain accessible with context off"); CloseComparison(); });
		Delay();
		Do([this] { m_pBefore->m_vpEnvelopes.clear(); m_pAfter->m_vpEnvelopes.clear(); m_pBefore->m_vSettings.clear(); m_pAfter->m_vSettings.clear(); OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		Click([this] { return SectionPosition(2); });
		Do([this] { Check((Compare().*Access(TNavigator{})).empty() && (Compare().*Access(TSelectedEntry{})) == -1, "absent envelopes clear stale selection"); });
		Screenshot("sections-empty-envelopes");
		Click([this] { return SectionPosition(3); });
		Screenshot("sections-empty-settings");
		Do([this] { CloseComparison(); });
		Delay();
		Do([this] { m_pBefore->m_vpGroups.clear(); m_pAfter->m_vpGroups.clear(); OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		Click([this] { return SectionPosition(1); });
		Do([this] { Check((Compare().*Access(TNavigator{})).empty() && (Compare().*Access(TSelectedEntry{})) == -1, "empty layer section clears selection"); });
		Screenshot("sections-empty-layers");
		Do([this] { CloseComparison(); Compare().*Access(TSidebarWidth{}) = 225; Graphics()->Resize(1280, 800, 0); OnWindowResize(); });
		Delay();
	}
	void SectionReviewSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				str_copy(pMap->m_aDisplayName, Side ? "Review After" : "Review Before");
				pMap->m_aFilename[0] = 0;
				pMap->m_MapViewState.m_WorldOffset = vec2(0, 0);
				for(int Index = 0; Index < 1000; ++Index)
					pMap->m_vSettings.emplace_back(("tune command " + std::to_string(Index) + " repeated wrapped words repeated wrapped words repeated wrapped words repeated wrapped words repeated wrapped words").c_str());
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Review");
				auto pTall = std::make_shared<CLayerQuads>(pMap.get());
				str_copy(pTall->m_aName, "Tall");
				pTall->NewQuad(0, 0, 32, Side ? 2080 : 2048);
				pGroup->AddLayer(pTall);
				for(int Index = 0; Index < 3; ++Index)
				{
					auto pTiles = std::make_shared<CLayerTiles>(pMap.get(), 1, 1);
					pTiles->m_aName[0] = 0;
					pTiles->m_pTiles[0].m_Index = Index == 0 ? Side + 1 : 3;
					pGroup->AddLayer(pTiles);
				}
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			Compare().*Access(TGroup{}) = false;
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Click([this] { return SectionPosition(3); });
		Delay();
		Click([this] { return SectionPosition(1); });
		Do([this] {
			Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Tall", "first Layers visit from Settings chooses the tall layer");
			const float Expected = (2080.0f + 64) / (Compare().*Access(TPanes{}))[0].h;
			Check(std::abs((Compare().*Access(TZoom{})) - Expected) < 0.001f, "first Layers visit fits the new inspector-height panes");
		});
		// Save distinct same-entry inspections in the two spatial sections.
		Click([this] { return SectionPosition(0); });
		Click([this] { return DetailPosition(1); });
		auto pOriginal = std::make_shared<std::tuple<vec2, float, int, bool>>();
		auto pOther = std::make_shared<std::tuple<vec2, float, int, bool>>();
		auto ReadState = [this] { return std::tuple{Compare().*Access(TCenter{}), Compare().*Access(TZoom{}), Compare().*Access(TSelectedDetail{}), Compare().*Access(TGroup{})}; };
		Do([this, pOriginal, ReadState] { Compare().*Access(TCenter{}) = vec2(123, 456); Compare().*Access(TZoom{}) = 3.5f; *pOriginal = ReadState(); });
		Click([this] { return SectionPosition(1); });
		Click([this] { return DetailPosition(0); });
		Do([this, pOther, ReadState] { Compare().*Access(TCenter{}) = vec2(-300, 900); Compare().*Access(TZoom{}) = 2.25f; Compare().*Access(TGroup{}) = true; *pOther = ReadState(); });
		Click([this] { return SectionPosition(0); });
		Do([this, pOriginal, ReadState] { Check(ReadState() == *pOriginal && Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Tall", "same-entry Changes return restores pre-departure center, zoom, detail and grouping"); });
		Click([this] { return SectionPosition(1); });
		Do([this, pOther, ReadState] { Check(ReadState() == *pOther, "same-entry Layers return restores its own center, zoom, detail and grouping"); Compare().*Access(TGroup{}) = false; });
		// Use the real inspector to verify the displayed matching explanations.
		auto CheckMatching = [this](const char *pReason) {
			const auto &Values = Compare().*Access(TValueRows{});
			Check(std::any_of(Values.begin(), Values.end(), [pReason](const auto &Value) { return Value.m_Name == "Matching" && Value.m_Before == pReason && Value.m_After == "Historical identity is unknown"; }), std::string("inspector explains ") + pReason);
		};
		Click([this] { return TreePosition("Tall"); });
		Click([this] { return DetailPosition(0); });
		Do([this, CheckMatching] { Check(SelectedDetail(Compare())[1] == 2, "positional object inspector row selected"); CheckMatching("Positional fallback in compatible ordered span"); });
		Do([this] {
			for(int Index = 0; Index < (int)Result().m_vEntries.size(); ++Index)
				if(Result().m_vEntries[Index].m_Kind == MapDiff::EEntryKind::LAYER && Result().m_vEntries[Index].m_Positional)
				{
					(Compare().*Access(TSelect{}))(Index);
					break;
				}
		});
		Click([this] { return DetailPosition(0); });
		Do([this, CheckMatching] { Check(SelectedDetail(Compare())[1] == 4 && Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Positional, "positional layer matching row selected"); CheckMatching("Positional fallback in compatible ordered span"); m_SavedEntry = Compare().*Access(TSelectedEntry{}); });
		Click([this] { return SectionPosition(0); });
		Do([this] { (Compare().*Access(TSelect{}))(m_SavedEntry); });
		Delay();
		Do([this] {
			const auto &Indices = Compare().*Access(TNavigator{});
			const int Row = std::find(Indices.begin(), Indices.end(), m_SavedEntry) - Indices.begin();
			const auto &Scroll = (Compare().*Access(TNavLists{}))[0].*Access(TListScroll{});
			const auto &Rect = Scroll.*Access(TScrollRect{});
			const vec2 Point(Rect.x + 50, Rect.y + (Scroll.*Access(TScrollOffset{})) + Row * 36 + 8);
			Ui()->OnCursorMove((Point.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (Point.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h);
		});
		Delay();
		Do([this] { Check(str_find(m_aTooltip, "#1 (unnamed)") && str_find(m_aTooltip, "Before #1 / After #1"), "non-tree name tooltip exposes full unnamed label and source indices"); });
		Click([this] { return SectionPosition(1); });
		Do([this] {
			for(int Index = 0; Index < (int)Result().m_vEntries.size(); ++Index)
				if(Result().m_vEntries[Index].m_Kind == MapDiff::EEntryKind::LAYER && Result().m_vEntries[Index].m_Ambiguous && !Result().m_vEntries[Index].m_Positional)
				{
					(Compare().*Access(TSelect{}))(Index);
					break;
				}
		});
		Click([this] { return DetailPosition(0); });
		Do([this, CheckMatching] { Check(SelectedDetail(Compare())[1] == 4, "duplicate layer matching row selected"); CheckMatching("Ambiguous duplicate content match"); });
		Click([this] { return SectionPosition(3); });
		auto Move = [this](vec2 P) { Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h); };
		Do([this, Move] { Compare().*Access(TSidebarWidth{}) = 100; Graphics()->Resize(800, 600, 0); OnWindowResize(); });
		Delay();
		Do([this, Move] { m_SavedScroll = (Compare().*Access(TSettings{})).*Access(TContentSize{}); Check(m_SavedScroll > 30000, "large settings fixture has wrapped command rows"); Move((Compare().*Access(TDivider{})).Center()); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		for(int Pixel = 0; Pixel < 20; ++Pixel)
		{
			Do([this, Move, Pixel] { Move(vec2(240 + Pixel * 5, (Compare().*Access(TDivider{})).Center().y)); });
			Delay(2);
			Do([this] { Check(Ui()->ActiveItem() == &(Compare().*Access(TDivider{})) && ((Compare().*Access(TSettings{})).*Access(TContentSize{})) == m_SavedScroll, "continuous settings resize retains measured row heights"); });
		}
		Do([this] {
			CImageInfo Image;
			static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
			const auto &Area = (Compare().*Access(TSettings{})).*Access(TScrollRect{});
			bool Clean = Image.m_pData != nullptr;
			if(Clean)
				for(int Y = (int)Area.y + 5; Y < Area.y + 150; ++Y)
				{
					const auto Color = Image.PixelColor((int)((Area.x + Area.w / 2) / Ui()->Screen()->w * Image.m_Width), (int)(Y / Ui()->Screen()->h * Image.m_Height));
					Clean &= std::abs(Color.r - 0.08f) < 0.015f && std::abs(Color.g - 0.09f) < 0.015f && std::abs(Color.b - 0.11f) < 0.015f;
				}
			Check(Clean, "cached settings wrapping stays clipped to each column during resize");
			Image.Free();
			Mouse(SDL_BUTTON_LEFT, false);
		});
		Delay();
		Do([this] { Check(((Compare().*Access(TSettings{})).*Access(TContentSize{})) > m_SavedScroll, "settings remeasure to the narrower columns on divider release"); m_SavedScroll = (Compare().*Access(TSettings{})).*Access(TContentSize{}); });
		Delay();
		Do([this] { Check(((Compare().*Access(TSettings{})).*Access(TContentSize{})) == m_SavedScroll, "released settings geometry remains cached"); CheckSources("section review regression"); CloseComparison(); Compare().*Access(TSidebarWidth{}) = 225; Graphics()->Resize(1280, 800, 0); OnWindowResize(); });
		Delay();
	}
	void Shortcut(SDL_Keycode K, bool Shift = false)
	{
		Do([this, K, Shift] { Key(SDLK_LCTRL, true); if(Shift) Key(SDLK_LSHIFT, true); Key(K, true); });
		Delay(2);
		Do([this, K, Shift] { Key(K, false); if(Shift) Key(SDLK_LSHIFT, false); Key(SDLK_LCTRL, false); });
		Delay();
	}

	vec2 ComparisonTabPoint(CMapCompare *pTarget, bool Close = false)
	{
		float X = m_ToolBoxWidth + 2 + ((this->*Access(TTabScroll{})).*Access(TScrollOffset{}));
		for(const auto &pMap : Maps())
			X += std::clamp(TextRender()->TextWidth(10, pMap->m_aDisplayName) + 10, 60.0f, 120.0f) + 20;
		for(const auto &pDocument : Tabs().*Access(TCompareDocuments{}))
		{
			const auto &Sources = pDocument.get()->*Access(TSources{});
			const bool Pending = (pDocument.get()->*Access(TCapturedRevisions{}))[0] != Sources[0]->m_ContentRevision || (pDocument.get()->*Access(TCapturedRevisions{}))[1] != Sources[1]->m_ContentRevision;
			const std::string Title = std::string("Before: ") + Sources[0]->m_aDisplayName + " / After: " + Sources[1]->m_aDisplayName + (Pending ? " *" : "");
			const float Width = std::clamp(TextRender()->TextWidth(10, Title.c_str()) + 12, 150.0f, 270.0f);
			if(pDocument.get() == pTarget)
				return vec2(X + (Close ? Width + 9 : Width / 2), 31);
			X += Width + 20;
		}
		return vec2(-1, -1);
	}

	void TabLifecycleSteps()
	{
		struct CFixture
		{
			CEditorMap *m_A = nullptr, *m_B = nullptr, *m_C = nullptr;
			CMapCompare *m_AB = nullptr, *m_AC = nullptr;
			const void *m_Snapshot = nullptr;
			uint64_t m_Revision = 0;
			size_t m_Count = 0;
			size_t m_HistorySize = 0;
			int m_Original = 0;
		};
		auto F = std::make_shared<CFixture>();
		Do([this, F] {
			for(int i = 0; i < 3; ++i)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_copy(pMap->m_aFilename, "maps/same.map");
				str_format(pMap->m_aDisplayName, sizeof(pMap->m_aDisplayName), "same (%d)", i + 1);
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Persistent");
				auto pLayer = std::make_shared<CLayerTiles>(pMap.get(), 8, 8);
				str_copy(pLayer->m_aName, "Bookmark");
				pLayer->m_pTiles[9].m_Index = i == 1 ? TILE_FREEZE : TILE_SOLID;
				pGroup->AddLayer(pLayer);
				if(i == 0)
					F->m_A = pMap.get();
				else if(i == 1)
					F->m_B = pMap.get();
				else
					F->m_C = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			Tabs().Open(*this, F->m_A, F->m_B);
		});
		WaitReady();
		Do([this, F] {
			F->m_AB = Tabs().Selected();
			F->m_Snapshot = SnapshotIdentity(Compare());
			(Compare().*Access(TSetSection{}))(1);
			SelectKind(MapDiff::EEntryKind::LAYER, "Bookmark");
			Compare().*Access(TCenter{}) = vec2(123, 456);
			Compare().*Access(TZoom{}) = 3.25f;
			SetLayerVisible(Compare(), FindEntry("Bookmark"), false);
			F->m_Count = (Tabs().*Access(TCompareDocuments{})).size();
			Tabs().Open(*this, F->m_A, F->m_B);
			Check(Tabs().Selected() == F->m_AB && (Tabs().*Access(TCompareDocuments{})).size() == F->m_Count, "opening existing ordered pair selects its stable tab");
			Tabs().SelectMap(*this, F->m_B);
		});
		Delay();
		Do([this, F] {
			Check(!Tabs().Active() && Map() == F->m_B, "comparison returns to a valid map tab");
			F->m_B->m_vSettings.emplace_back("tune gravity 0.5");
			F->m_B->OnModify();
			F->m_B->m_vSettings.emplace_back("sv_test_cmds 1");
			F->m_B->OnModify();
			Check(SnapshotIdentity(*F->m_AB) == F->m_Snapshot && CompareState(*F->m_AB) == 4, "hidden edits retain pending snapshot without capture");
			Tabs().Open(*this, F->m_A, F->m_C);
		});
		WaitReady();
		Screenshot("tabs-multiple-documents");
		Do([this, F] {
			F->m_AC = Tabs().Selected();
			Check(F->m_AC != F->m_AB && Result().ChangeCount() == 0, "duplicate filenames keep distinct comparison documents");
			Compare().*Access(TCenter{}) = vec2(-100, 20);
			Tabs().Open(*this, F->m_A, F->m_B);
		});
		WaitReady();
		Do([this, F] {
			Check(Tabs().Selected() == F->m_AB && SnapshotIdentity(Compare()) != F->m_Snapshot, "activation coalesces hidden changes into one new snapshot");
			Check((Compare().*Access(TCapturedRevisions{}))[1] == F->m_B->m_ContentRevision, "published result matches latest source revision");
			Check((Compare().*Access(TCenter{})) == vec2(123, 456) && (Compare().*Access(TZoom{})) == 3.25f && !std::get<0>(LayerState(Compare(), FindEntry("Bookmark"))), "refresh preserves camera and object visibility");
			Check(Result().m_vEntries[Compare().*Access(TSelectedEntry{})].m_Name == "Bookmark", "refresh restores object selection by identity");
			F->m_Snapshot = SnapshotIdentity(Compare());
			Tabs().SelectMap(*this, F->m_B);
		});
		Delay();
		Do([this, F] { F->m_Revision = F->m_B->m_ContentRevision; Check(F->m_B->Save("maps/tabs-save.map", [this](const char *pError) { Check(false, pError); }), "save current source"); Check(F->m_B->m_ContentRevision == F->m_Revision, "saving does not reset or advance content revision"); Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Do([this, F] { Check(SnapshotIdentity(Compare()) == F->m_Snapshot, "unchanged save and tab switching reuse comparison"); });
		Do([this, F] {
			auto pUnrelated = std::make_unique<CEditorMap>(this);
			pUnrelated->CreateDefault();
			pUnrelated->ResetModifiedState();
			str_copy(pUnrelated->m_aDisplayName, "Close background");
			auto *pMap = pUnrelated.get();
			Maps().push_back(std::move(pUnrelated));
			Tabs().CloseMap(*this, pMap);
		});
		Delay();
		Do([this, F] { Check(Tabs().Selected() == F->m_AB && SnapshotIdentity(Compare()) == F->m_Snapshot, "closing clean unrelated source tab preserves active comparison"); });
		// Native tab click and middle-click close, followed by the discoverable shortcut.
		Do([this, F] { Tabs().Open(*this, F->m_A, F->m_C); });
		WaitReady();
		Do([this, F] { Check(Tabs().Selected() == F->m_AC, "native click starts on another comparison"); });
		Click([this, F] { return ComparisonTabPoint(F->m_AB); });
		Do([this, F] { Check(Tabs().Selected() == F->m_AB && SnapshotIdentity(Compare()) == F->m_Snapshot && (Compare().*Access(TCenter{})) == vec2(123, 456), "native inactive comparison tab click selects AB and preserves snapshot/camera"); });
		Click([this, F] { return ComparisonTabPoint(F->m_AB); }, SDL_BUTTON_MIDDLE);
		Do([this, F] { Check(!Tabs().Active() && Tabs().CanReopen() && (Tabs().*Access(TCompareDocuments{})).size() == F->m_Count, "middle click closes only its comparison and remembers sources"); });
		Shortcut(SDLK_F4, true);
		WaitReady();
		Do([this, F] { F->m_AB = Tabs().Selected(); Check((Compare().*Access(TCenter{})) == vec2(123,456) && !std::get<0>(LayerState(Compare(), FindEntry("Bookmark"))), "reopen retains inspection preferences and live source identities"); });
		// Exercise the same trackers used by held editor gestures, then switch via
		// native Ctrl+Tab before releasing the mouse. Undo must restore the source.
		for(int Mode = 0; Mode < 4; ++Mode)
		{
			Do([this, F] { Tabs().SelectMap(*this, F->m_B); });
			Delay();
			Do([this, F, Mode] {
				auto *pMap = F->m_B;
				if(Mode == 0)
				{
					pMap->m_SelectedGroup = pMap->m_vpGroups.size() - 1;
					pMap->m_vSelectedLayers = {0};
					auto pLayer = std::static_pointer_cast<CLayerTiles>(pMap->SelectedLayer(0));
					F->m_Original = pLayer->GetTile(2, 2).m_Index;
					pLayer->SetTile(2, 2, CTile{TILE_FREEZE});
					pMap->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::BRUSH_DRAW;
				}
				else if(Mode == 1)
				{
					pMap->m_SelectedGroup = 0;
					pMap->m_vSelectedLayers = {0};
					pMap->m_vSelectedQuads = {0};
					auto pLayer = std::static_pointer_cast<CLayerQuads>(pMap->SelectedLayer(0));
					F->m_Original = pLayer->m_vQuads[0].m_aPoints[4].x;
					pMap->m_QuadTracker.BeginQuadTrack(pLayer, {0});
					pLayer->m_vQuads[0].m_aPoints[4].x += 1024;
				}
				else if(Mode == 2)
				{
					auto pEnv = std::make_shared<CEnvelope>(3);
					pEnv->AddPoint(CFixedTime(0), {0, 0, 0, 0});
					pMap->InsertEnvelope(pMap->m_vpEnvelopes.size(), pEnv);
					pMap->m_SelectedEnvelope = pMap->m_vpEnvelopes.size() - 1;
					pMap->m_vSelectedEnvelopePoints = {{0, 0}};
					pMap->m_EnvOpTracker.Begin(EEnvelopeEditorOp::DRAG_POINT);
					pEnv->m_vPoints[0].m_aValues[0] = 1024;
					F->m_Original = 0;
				}
				else
				{
					auto pLayer = std::make_shared<CLayerSounds>(pMap);
					pLayer->NewSource(0, 0);
					pMap->m_vpGroups.back()->AddLayer(pLayer);
					pMap->m_SelectedGroup = pMap->m_vpGroups.size() - 1;
					pMap->m_vSelectedLayers = {(int)pMap->m_vpGroups.back()->m_vpLayers.size() - 1};
					pMap->m_SelectedSoundSource = 0;
					pMap->m_SoundSourceOperationTracker.Begin(&pLayer->m_vSources[0], ESoundSourceOp::MOVE, pMap->m_vSelectedLayers[0]);
					pLayer->m_vSources[0].m_Position.x = 1024;
					F->m_Original = 0;
				}
				F->m_HistorySize = (Mode == 2 ? pMap->m_EnvelopeEditorHistory : pMap->m_EditorHistory).m_vpUndoActions.size();
				Key(SDLK_LCTRL, true);
				Key(SDLK_TAB, true);
				Mouse(SDL_BUTTON_LEFT, true);
			});
			Delay(2);
			Do([this, F, Mode] {
				const auto &H = Mode == 2 ? F->m_B->m_EnvelopeEditorHistory : F->m_B->m_EditorHistory;
				Check(H.m_vpUndoActions.size() == F->m_HistorySize + 1, "switch finalizes held editor operation " + std::to_string(Mode));
				Key(SDLK_TAB, false);
				Key(SDLK_LCTRL, false);
			});
			Delay(2);
			Do([this] { Check(Tabs().Transitioning(), "held mouse prevents destination input after shortcut release"); Mouse(SDL_BUTTON_LEFT, false); });
			Delay();
			Do([this, F, Mode] {
				Check(!Tabs().Transitioning() && Map() == F->m_C, "release completes source map transition");
				auto &H = Mode == 2 ? F->m_B->m_EnvelopeEditorHistory : F->m_B->m_EditorHistory;
				Check(H.Undo(), "finalized gesture can undo on its source");
				int Value = -1;
				if(Mode == 0)
					Value = std::static_pointer_cast<CLayerTiles>(F->m_B->m_vpGroups.back()->m_vpLayers[0])->GetTile(2, 2).m_Index;
				else if(Mode == 1)
					Value = std::static_pointer_cast<CLayerQuads>(F->m_B->m_vpGroups[0]->m_vpLayers[0])->m_vQuads[0].m_aPoints[4].x;
				else if(Mode == 2)
					Value = F->m_B->m_vpEnvelopes.back()->m_vPoints[0].m_aValues[0];
				else
					Value = std::static_pointer_cast<CLayerSounds>(F->m_B->m_vpGroups.back()->m_vpLayers.back())->m_vSources[0].m_Position.x;
				Check(Value == F->m_Original, "undo restores held gesture value " + std::to_string(Mode));
			});
		}
		Do([this, F] { Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		// All three histories must refresh even when individual actions omit OnModify.
		for(int History = 0; History < 3; ++History)
		{
			Do([this, F, History] {
				auto &H = History == 0 ? F->m_B->m_EditorHistory : History == 1 ? F->m_B->m_ServerSettingsHistory :
												  F->m_B->m_EnvelopeEditorHistory;
				const auto Revision = F->m_B->m_ContentRevision;
				H.RecordAction(std::make_shared<CSmokeAction>(F->m_B));
				Check(F->m_B->m_ContentRevision > Revision, "history RecordAction advances revision " + std::to_string(History));
				H.Undo();
			});
			WaitReady();
			Do([this, F, History] {
				Check((Compare().*Access(TCapturedRevisions{}))[1] == F->m_B->m_ContentRevision, "automatic refresh after undo history " + std::to_string(History));
				Check(std::any_of(Result().m_vEntries.begin(), Result().m_vEntries.end(), [](const auto &E) { return std::any_of(E.m_vProperties.begin(), E.m_vProperties.end(), [](const auto &P) { return P.m_After == "UNEXPECTED UNDO"; }); }), "undo result contains current author value");
				auto &H = History == 0 ? F->m_B->m_EditorHistory : History == 1 ? F->m_B->m_ServerSettingsHistory :
												  F->m_B->m_EnvelopeEditorHistory;
				H.Redo();
			});
			WaitReady();
			Do([this, F, History] { Check((Compare().*Access(TCapturedRevisions{}))[1] == F->m_B->m_ContentRevision, "automatic refresh after redo history " + std::to_string(History)); Check(std::any_of(Result().m_vEntries.begin(), Result().m_vEntries.end(), [](const auto &E) { return std::any_of(E.m_vProperties.begin(), E.m_vProperties.end(), [](const auto &P) { return P.m_After == "UNEXPECTED REDO"; }); }), "redo result contains current author value"); });
		}
		Do([this, F] {
			F->m_B->OnModify();
			Tabs().OnUpdate(*this); // Marks pending.
			Tabs().OnUpdate(*this); // Captures and starts the one worker.
			Check(CompareState(Compare()) == 3, "stale completion fixture starts comparison worker");
			str_copy(F->m_B->m_MapInfo.m_aAuthor, "Newest generation");
			F->m_B->OnModify();
			Tabs().OnUpdate(*this);
			Check(CompareState(Compare()) != 4, "obsolete worker result cannot publish");
			Tabs().Open(*this, F->m_A, F->m_C);
			Tabs().Open(*this, F->m_C, F->m_B);
			Tabs().Open(*this, F->m_A, F->m_B);
			int Workers = (Tabs().*Access(TRetiredWorker{})).valid();
			for(auto &pDocument : Tabs().*Access(TCompareDocuments{}))
				Workers += (pDocument.get()->*Access(TWorker{})).valid();
			Check(Workers <= 1, "rapid different-pair switching retains at most one global worker");
		});
		WaitReady();
		Do([this, F] { Check((Compare().*Access(TCapturedRevisions{}))[1] == F->m_B->m_ContentRevision, "newest generation wins after obsolete work is reaped"); });
		Do([this, F] {
			// A current selection cannot accidentally follow a reused numeric slot.
			for(auto *pMap : {F->m_A, F->m_B})
			{
				pMap->m_vpGroups.back()->m_vpLayers.clear();
				auto pReplacement = std::make_shared<CLayerTiles>(pMap, 8, 8);
				str_copy(pReplacement->m_aName, "Replacement");
				pMap->m_vpGroups.back()->AddLayer(pReplacement);
				pMap->OnModify();
			}
			IInput::CEvent Event{};
			Event.m_Key = KEY_HOME;
			Event.m_Flags = IInput::FLAG_PRESS;
			OnInput(Event);
			Check((Compare().*Access(TCenter{})) == vec2(123, 456), "Home rejects stale snapshot indices before refresh");
		});
		WaitReady();
		Do([this, F] { Check((Compare().*Access(TSelectedEntry{})) == -1 && (Compare().*Access(TSelectedDetail{})) == -1, "deleted selection clears instead of following replacement index"); Tabs().SelectMap(*this, F->m_B); });
		Delay();
		Do([this, F] {
			const auto Revision = F->m_B->m_ContentRevision;
			Check(AddImage("editor/checker.png", IStorage::TYPE_ALL, this), "direct image add succeeds");
			Check(F->m_B->m_ContentRevision > Revision, "direct image add notifies comparison");
			F->m_Revision = F->m_B->m_ContentRevision;
			Check(ReplaceImage("editor/checker.png", IStorage::TYPE_ALL, false), "direct image replacement succeeds");
			Check(F->m_B->m_ContentRevision > F->m_Revision, "same-index image replacement notifies comparison");
			Tabs().Open(*this, F->m_A, F->m_B);
		});
		WaitReady();
		// Multiple Ctrl+Tab presses while Ctrl is held advance the pending tab.
		Do([this] { Key(SDLK_LCTRL, true); Key(SDLK_TAB, true); });
		Delay(2);
		Do([this] { Key(SDLK_TAB, false); });
		Delay(2);
		Do([this] { Key(SDLK_TAB, true); });
		Delay(2);
		Do([this] { Check(Tabs().Transitioning(), "held Ctrl keeps destination input behind transition barrier"); Key(SDLK_TAB, false); Key(SDLK_LCTRL, false); });
		Delay();
		Do([this, F] { Check(!Tabs().Active() && Map() == Maps()[0].get(), "repeated Ctrl+Tab advances pending target while modifier held"); Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Do([this, F] { Tabs().SelectMap(*this, F->m_B); });
		Delay();
		Do([this, F] {
			F->m_B->ResetModifiedState();
			F->m_B->m_SelectedGroup = 0;
			F->m_B->m_vSelectedLayers = {0};
			F->m_B->m_vSelectedQuads = {0};
			auto pLayer = std::static_pointer_cast<CLayerQuads>(F->m_B->SelectedLayer(0));
			F->m_Original = pLayer->m_vQuads[0].m_aPoints[4].x;
			F->m_B->m_QuadTracker.BeginQuadTrack(pLayer, {0});
			pLayer->m_vQuads[0].m_aPoints[4].x += 1024;
			F->m_HistorySize = F->m_B->m_EditorHistory.m_vpUndoActions.size();
			Key(SDLK_LCTRL, true);
			Key(SDLK_F4, true);
			Mouse(SDL_BUTTON_LEFT, true);
		});
		Delay(2);
		Do([this, F] { Check(F->m_B->m_Modified && F->m_B->m_EditorHistory.m_vpUndoActions.size() == F->m_HistorySize + 1, "same-map close finalizes unsaved held quad before deciding confirmation"); Key(SDLK_F4, false); Key(SDLK_TAB, true); });
		Delay(2);
		Do([this] { Key(SDLK_TAB, false); Key(SDLK_LCTRL, false); Mouse(SDL_BUTTON_LEFT, false); });
		Delay();
		Do([this, F] { Check(Ui()->IsPopupOpen() && !Tabs().Active() && Map() == F->m_B, "held close keeps confirmation on source map despite Tab press"); Key(SDLK_ESCAPE, true); });
		Delay(2);
		Do([this] { Key(SDLK_ESCAPE, false); });
		Delay();
		Do([this, F] { Check(F->m_B->m_EditorHistory.Undo() && std::static_pointer_cast<CLayerQuads>(F->m_B->m_vpGroups[0]->m_vpLayers[0])->m_vQuads[0].m_aPoints[4].x == F->m_Original, "cancelled held close retains undoable source gesture"); Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Do([this, F] { Tabs().CloseMap(*this, F->m_B); });
		Delay();
		Do([this, F] { Check(Ui()->IsPopupOpen() && m_PopupEventType == POPEVENT_CLOSE_MAP, "dirty source close keeps visible confirmation associated with source map"); Check((Tabs().*Access(TCompareDocuments{})).size() >= 2, "unconfirmed source close retains all dependents"); Key(SDLK_ESCAPE, true); });
		Delay(2);
		Do([this] { Key(SDLK_ESCAPE, false); });
		Delay();
		Do([this, F] { Check(!Ui()->IsPopupOpen(), "cancel source close through native Escape"); Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Do([this, F] {
			Check(Tabs().Selected() == F->m_AB, "cancelled close preserves comparison identity");
			const auto Count = (Tabs().*Access(TCompareDocuments{})).size();
			Check(HandleMapDrop("maps/coverage.map", IStorage::TYPE_ALL), "unrelated drop loads a map");
			Check((Tabs().*Access(TCompareDocuments{})).size() == Count, "unrelated load preserves all comparisons");
		});
		Delay();
		Do([this, F] { Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Shortcut(SDLK_F4);
		Do([this] { Check(Tabs().CanReopen(), "Ctrl+F4 remembers closed comparison"); });
		Do([this, F] {
			for(size_t i = 0; i < Maps().size(); ++i)
				if(Maps()[i].get() == F->m_A)
				{
					CloseMap(i, false);
					break;
				}
			Check((Tabs().*Access(TCompareDocuments{})).size() == 1 && !Tabs().CanReopen(), "source disposal closes multiple dependents and prunes their history while retaining unrelated comparison");
		});
		Delay();
		Do([this, F] { Tabs().Open(*this, F->m_C, F->m_B); });
		WaitReady();
		Click([this] { return ComparisonTabPoint(Tabs().Selected(), true); });
		Do([this] { Check(!Tabs().Active() && Tabs().CanReopen(), "X closes comparison without disposing sources"); Tabs().Reopen(*this); });
		WaitReady();
		Shortcut(SDLK_TAB, true);
		Do([this] { Check(!Tabs().Active(), "Ctrl+Shift+Tab returns from first comparison to last map"); });
		Do([this, F] {
			const auto Captures = m_CaptureCount;
			Tabs().Open(*this, F->m_B, F->m_C);
			Check(CompareState(Compare()) == 2, "new comparison can close before first capture");
			CloseComparison();
			Check(m_CaptureCount == Captures, "closing pending comparison avoids snapshot work");
		});
		Delay();
		Shortcut(SDLK_F4, true);
		WaitReady();
		Do([this] { Check(!Result().m_vEntries.empty() && (Compare().*Access(TSelectedEntry{})) >= 0, "reopening before first capture uses initial result selection"); });
		Do([this] { CloseComparison(); });
		Delay();
		for(int i = 0; i < 12; ++i)
		{
			Do([this, F] { Tabs().Open(*this, F->m_B, F->m_C); });
			Delay(2);
			Do([this] { CloseComparison(); });
			Delay(2);
		}
		Do([this] { Check(ClosedComparisonCount(Tabs()) == 10, "recent comparison history is bounded to ten lightweight descriptors"); });
		Do([this, F] {
			for(size_t i = 0; i < Maps().size(); ++i)
				if(Maps()[i].get() == F->m_B)
				{
					CloseMap(i, false);
					break;
				}
			Check((Tabs().*Access(TCompareDocuments{})).empty() && !Tabs().CanReopen(), "last dependent source disposal releases remaining comparison state");
		});

		Delay();
	}

	void WriteReviewSounds()
	{
		// A 20 ms mono Opus silence fixture.
		static const char aAdd[] =
			"\x4f\x67\x67\x53\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\xe0\x7b\x3e\x6a\x00\x00\x00\x00\xc5\x02"
			"\x48\xbf\x01\x13\x4f\x70\x75\x73\x48\x65\x61\x64\x01\x01\x38\x01\x80\xbb\x00\x00\x00\x00\x00\x4f"
			"\x67\x67\x53\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xe0\x7b\x3e\x6a\x01\x00\x00\x00\x8f\xa4\xdd"
			"\x2a\x01\x3d\x4f\x70\x75\x73\x54\x61\x67\x73\x0c\x00\x00\x00\x4c\x61\x76\x66\x36\x31\x2e\x37\x2e"
			"\x31\x30\x30\x01\x00\x00\x00\x1d\x00\x00\x00\x65\x6e\x63\x6f\x64\x65\x72\x3d\x4c\x61\x76\x63\x36"
			"\x31\x2e\x31\x39\x2e\x31\x30\x30\x20\x6c\x69\x62\x6f\x70\x75\x73\x4f\x67\x67\x53\x00\x04\xf8\x04"
			"\x00\x00\x00\x00\x00\x00\xe0\x7b\x3e\x6a\x02\x00\x00\x00\x58\xab\xa0\xe0\x02\x03\x03\xf8\xff\xfe"
			"\xf8\xff\xfe";
		IOHANDLE Add = Storage()->OpenFile("review-add.opus", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		io_write(Add, aAdd, sizeof(aAdd) - 1);
		io_close(Add);
		// A 60 ms mono Opus silence fixture.
		static const char aReplace[] =
			"\x4f\x67\x67\x53\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x77\xe6\xca\x8f\x00\x00\x00\x00\x5b\x00"
			"\x50\x09\x01\x13\x4f\x70\x75\x73\x48\x65\x61\x64\x01\x01\x38\x01\x80\xbb\x00\x00\x00\x00\x00\x4f"
			"\x67\x67\x53\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x77\xe6\xca\x8f\x01\x00\x00\x00\xa0\xc7\xcb"
			"\x3c\x01\x3d\x4f\x70\x75\x73\x54\x61\x67\x73\x0c\x00\x00\x00\x4c\x61\x76\x66\x36\x31\x2e\x37\x2e"
			"\x31\x30\x30\x01\x00\x00\x00\x1d\x00\x00\x00\x65\x6e\x63\x6f\x64\x65\x72\x3d\x4c\x61\x76\x63\x36"
			"\x31\x2e\x31\x39\x2e\x31\x30\x30\x20\x6c\x69\x62\x6f\x70\x75\x73\x4f\x67\x67\x53\x00\x04\x78\x0c"
			"\x00\x00\x00\x00\x00\x00\x77\xe6\xca\x8f\x02\x00\x00\x00\x27\x1a\x49\x37\x04\x03\x03\x03\x03\xf8"
			"\xff\xfe\xf8\xff\xfe\xf8\xff\xfe\xf8\xff\xfe";
		IOHANDLE Replace = Storage()->OpenFile("review-replace.opus", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		io_write(Replace, aReplace, sizeof(aReplace) - 1);
		io_close(Replace);
	}

	void ReviewSteps()
	{
		struct CFixture
		{
			CEditorMap *m_A{}, *m_B{}, *m_C{};
			CMapCompare *m_AB{};
			std::shared_ptr<CLayerTiles> m_pTiles;
			std::shared_ptr<CEnvelope> m_pEnvelope;
			SPopupMenuId m_Popup;
			uint64_t m_Revision{};
			size_t m_History{};
			int m_Entry{}, m_Detail{}, m_Value{};
			vec2 m_Center;
		};
		auto F = std::make_shared<CFixture>();
		auto Press = [this](int KeyCode) { Do([this, KeyCode] { Key(KeyCode, true); }); Delay(); Do([this, KeyCode] { Key(KeyCode, false); }); Delay(); };
		auto Move = [this](vec2 P) { Ui()->OnCursorMove((P.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (P.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h); };
		auto CheckCaptured = [this, F](const char *pName) {
			MapDiff::CSnapshot Current;
			MapDiff::Capture(*F->m_B, Current);
			Check(Tabs().Selected() == F->m_AB && (Compare().*Access(TCapturedRevisions{}))[1] == F->m_B->m_ContentRevision && MapDiff::Compare(CapturedSnapshot(Compare(), 1), Current).ChangeCount() == 0, pName);
		};
		Do([this, F] {
			WriteReviewSounds();
			for(int Side = 0; Side < 3; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_format(pMap->m_aDisplayName, sizeof(pMap->m_aDisplayName), "Review %d", Side);
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Review group");
				auto pTiles = std::make_shared<CLayerTiles>(pMap.get(), 8, 8);
				str_copy(pTiles->m_aName, "ReviewTile");
				pGroup->AddLayer(pTiles);
				auto pEnvelope = std::make_shared<CEnvelope>(3);
				str_copy(pEnvelope->m_aName, "Review envelope");
				pEnvelope->AddPoint(CFixedTime(0), {0, 0, 0, 0});
				pEnvelope->AddPoint(CFixedTime(1000), {128 * 1024, 0, 0, 0});
				pEnvelope->m_vPoints[0].m_Curvetype = CURVETYPE_BEZIER;
				pEnvelope->m_vPoints[0].m_Bezier.m_aOutTangentDeltaX[0] = CFixedTime(250);
				pEnvelope->m_vPoints[0].m_Bezier.m_aOutTangentDeltaY[0] = 32 * 1024;
				pEnvelope->m_vPoints[1].m_Bezier.m_aInTangentDeltaX[0] = CFixedTime(-250);
				pEnvelope->m_vPoints[1].m_Bezier.m_aInTangentDeltaY[0] = -32 * 1024;
				pMap->m_vpEnvelopes.push_back(pEnvelope);
				if(Side == 0)
					F->m_A = pMap.get();
				else if(Side == 1)
					F->m_C = pMap.get();
				else
				{
					F->m_B = pMap.get();
					F->m_pTiles = pTiles;
					F->m_pEnvelope = pEnvelope;
					pTiles->m_pTiles[45].m_Index = TILE_FREEZE;
					pMap->m_vSettings = {"tune gravity 0.5", "sv_test_cmds 1"};
					str_copy(pMap->m_MapInfo.m_aAuthor, "Review author");
				}
				Maps().push_back(std::move(pMap));
			}
			Tabs().OpenSelector(*this);
		});
		Delay();
		auto SourceRow = [this](int Side, const CEditorMap *pSource) {
			const auto &Scroll = (Compare().*Access(TPairLists{}))[Side].*Access(TListScroll{});
			const auto &Rect = Scroll.*Access(TScrollRect{});
			int Index = 0;
			while(Maps()[Index].get() != pSource)
				++Index;
			return vec2(Rect.x + 40, Rect.y + (Scroll.*Access(TScrollOffset{})) + Index * 42 + 12);
		};
		Click([F, SourceRow] { return SourceRow(0, F->m_A); });
		Click([F, SourceRow] { return SourceRow(1, F->m_C); });
		Do([this, F] { Check((Compare().*Access(TSources{}))[0] == F->m_A && (Compare().*Access(TSources{}))[1] == F->m_C, "native selector rows choose both sources while their strip tabs are visible"); });
		Click([this] { return vec2(Ui()->Screen()->w - 80, Ui()->Screen()->h - 38); });
		WaitReady();
		Do([this, F] { Check((Compare().*Access(TSources{}))[0] == F->m_A && (Compare().*Access(TSources{}))[1] == F->m_C, "selector starts the chosen ordered pair"); CloseComparison(); Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Do([this, F] { F->m_AB = Tabs().Selected(); SelectKind(MapDiff::EEntryKind::LAYER, "ReviewTile"); F->m_Detail = Compare().*Access(TSelectedDetail{}); F->m_pTiles->m_pTiles[9].m_Index = TILE_FREEZE; F->m_B->OnModify(); });
		Delay();
		WaitReady();
		auto CheckTile = [this](const char *pName) { const auto Detail = SelectedDetail(Compare()); Check(Detail[1] == 1 && Result().m_vEntries[Detail[0]].m_vTiles[Detail[2]].m_X == 5 && Result().m_vEntries[Detail[0]].m_vTiles[Detail[2]].m_Y == 5, pName); };
		Do([this, F, CheckTile] { CheckTile("active tile detail survives changed detail index"); Check((Compare().*Access(TSelectedDetail{})) > F->m_Detail, "inserted earlier tile moves detail index"); (Compare().*Access(TSetSection{}))(2); F->m_pTiles->m_pTiles[0].m_Index = TILE_FREEZE; F->m_B->OnModify(); });
		Delay();
		WaitReady();
		Do([this, CheckTile] { (Compare().*Access(TSetSection{}))(0); CheckTile("inactive section restores tile detail by coordinate after refresh"); });
		Click([this] { return DetailPosition(Compare().*Access(TSelectedDetail{})); });
		Do([this] { Check(!(Compare().*Access(TTreeActive{})), "native inspector click owns arrow focus"); CloseComparison(); });
		Delay();
		Shortcut(SDLK_F4, true);
		WaitReady();
		Do([this, F] { F->m_AB = Tabs().Selected(); F->m_Detail = Compare().*Access(TSelectedDetail{}); Check(!(Compare().*Access(TTreeActive{})), "reopen retains inspector arrow focus"); });
		Press(SDLK_UP);
		Do([this, F] { Check((Compare().*Access(TSelectedDetail{})) < F->m_Detail, "reopened inspector receives arrow input"); });
		auto OpenTabPopup = [this, F] {
			F->m_Entry = Compare().*Access(TSelectedEntry{});
			F->m_Detail = Compare().*Access(TSelectedDetail{});
			Compare().*Access(TCenter{}) = vec2(123, 456);
			F->m_Center = Compare().*Access(TCenter{});
			m_PopupMapTab.m_pEditor = this;
			m_PopupMapTab.m_SelectedMap = Maps().size() - 1;
			Ui()->DoPopupMenu(&m_PopupMapTab, 100, 100, 170, 96, &m_PopupMapTab, CPopupMapTab::Render);
		};
		for(int Focus = 0; Focus < 3; ++Focus)
		{
			Do([this, Focus, OpenTabPopup] { if(Focus == 2) { (Compare().*Access(TSetSection{}))(3); SelectKind(MapDiff::EEntryKind::SETTINGS); } Compare().*Access(TTreeActive{}) = Focus == 1; OpenTabPopup(); });
			Delay();
			Press(SDLK_n);
			Press(SDLK_p);
			Press(SDLK_HOME);
			Press(SDLK_DOWN);
			Do([this, F] { Check(Ui()->IsPopupOpen() && Tabs().Selected() == F->m_AB && (Compare().*Access(TSelectedEntry{})) == F->m_Entry && (Compare().*Access(TSelectedDetail{})) == F->m_Detail && (Compare().*Access(TCenter{})) == F->m_Center, "open tab popup owns viewer navigation and Home/arrow input"); });
			Press(SDLK_ESCAPE);
			Do([this, F] { Check(!Ui()->IsPopupOpen() && Tabs().Selected() == F->m_AB, "Escape dismisses tab popup without leaving comparison"); });
		}
		Do([this, F] { Tabs().SelectMap(*this, F->m_B); });
		Delay();
		Do([this, F] { Map()->m_SelectedGroup = Map()->m_vpGroups.size() - 1; F->m_Revision = Map()->m_ContentRevision; F->m_History = Map()->m_EditorHistory.m_vpUndoActions.size(); Ui()->DoPopupMenu(&F->m_Popup,100,100,145,256,this,PopupGroup); });
		Delay();
		Do([Move] { Move(vec2(207, 123)); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this] { Ui()->OnCursorMove(60, 0); });
		Delay();
		Do([this] { Key(SDLK_LCTRL,true); Key(SDLK_TAB,true); });
		Delay();
		Do([this, F] { Check(!Tabs().Active() && !Tabs().Transitioning() && Map() == F->m_B && Ui()->IsPopupOpen() && Map()->SelectedGroup()->m_OffsetX != 0, "property popup drag retains document input ownership during Ctrl+Tab"); Key(SDLK_TAB,false); Key(SDLK_LCTRL,false); Mouse(SDL_BUTTON_LEFT,false); });
		Delay();
		Do([this, F] { Check(Map()->m_ContentRevision > F->m_Revision && Map()->m_EditorHistory.m_vpUndoActions.size() == F->m_History + 1, "property popup release records its END revision and undo action"); });
		Press(SDLK_ESCAPE);
		Do([this, F] { Tabs().Open(*this, F->m_A, F->m_B); });
		WaitReady();
		Do([CheckCaptured] { CheckCaptured("comparison refresh includes completed property popup drag"); });
		for(int Side = 0; Side < 2; ++Side)
		{
			Do([this, F] { Tabs().SelectMap(*this, F->m_B); });
			Delay();
			Do([this, F] { m_ActiveExtraEditor = EXTRAEDITOR_ENVELOPES; Map()->m_SelectedEnvelope = 0; Map()->m_EnvelopeEditorState.m_ActiveChannels = 1; Map()->m_EnvelopeEditorState.m_ResetZoom = true; F->m_Revision = Map()->m_ContentRevision; });
			Delay(10);
			Do([this, F, Move, Side] {
				CUIRect Graph{m_ToolBoxWidth, Ui()->Screen()->h - 16 - m_aExtraEditorSplits[EXTRAEDITOR_ENVELOPES] + 32, Ui()->Screen()->w - m_ToolBoxWidth - 2, m_aExtraEditorSplits[EXTRAEDITOR_ENVELOPES] - 34};
				const auto &Point = F->m_pEnvelope->m_vPoints[Side];
				const auto X = Side ? Point.m_Bezier.m_aInTangentDeltaX[0] : Point.m_Bezier.m_aOutTangentDeltaX[0];
				const int Y = Side ? Point.m_Bezier.m_aInTangentDeltaY[0] : Point.m_Bezier.m_aOutTangentDeltaY[0];
				F->m_Value = Y;
				Move(vec2(((this->*Access(TEnvelopeEditor{})).*Access(TEnvelopeX{}))(Graph, (Point.m_Time + X).AsSeconds()), ((this->*Access(TEnvelopeEditor{})).*Access(TEnvelopeY{}))(Graph, fx2f(Point.m_aValues[0] + Y))));
			});
			Delay();
			Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
			Delay();
			Do([this] { Ui()->OnCursorMove(40, -20); });
			Delay();
			Do([this, F, Side] { const auto &Point = F->m_pEnvelope->m_vPoints[Side]; Check(Map()->m_ContentRevision > F->m_Revision && (Side ? Point.m_Bezier.m_aInTangentDeltaY[0] : Point.m_Bezier.m_aOutTangentDeltaY[0]) != F->m_Value && Input()->KeyIsPressed(KEY_MOUSE_1), "native tangent mutation advances revision before release"); Key(SDLK_LCTRL,true); Key(SDLK_TAB,true); });
			Delay();
			Do([this] { Key(SDLK_TAB,false); Key(SDLK_LCTRL,false); });
			Delay();
			Do([this] { Check(Tabs().Transitioning(), "interrupted tangent waits for held mouse release"); Mouse(SDL_BUTTON_LEFT,false); });
			WaitReady();
			Do([CheckCaptured] { CheckCaptured("interrupted tangent edit refreshes existing comparison"); });
		}
		for(int Operation = 1; Operation <= 2; ++Operation)
		{
			Do([this, F] { Tabs().SelectMap(*this, F->m_B); });
			Delay();
			Do([this] { m_ActiveExtraEditor = EXTRAEDITOR_NONE; Mouse(SDL_BUTTON_LEFT,true); });
			Delay();
			Do([this, F, Operation] {
				auto &Point = F->m_pEnvelope->m_vPoints[0];
				const auto Revision = Map()->m_ContentRevision;
				CQuad Quad{};
				Map()->m_CurrentQuadIndex = 0;
				Ui()->SetActiveItem(&Point);
				m_QuadEnvelopePointOperation = static_cast<EQuadEnvelopePointOperation>(Operation);
				Ui()->OnCursorMove(10, 0);
				Ui()->Update();
				Map()->m_MapViewState.m_MouseDeltaWorld = vec2(1, 1);
				Map()->m_MapViewState.m_MouseWorldPos = vec2(64, 96);
				Graphics()->TextureClear();
				Graphics()->QuadsBegin();
				DoQuadEnvPoint(&Quad, F->m_pEnvelope.get(), 0, 0);
				Graphics()->QuadsEnd();
				Check(Map()->m_ContentRevision > Revision, "quad envelope preview mutation advances revision");
				if(Operation == 1)
				{
					const auto Same = Map()->m_ContentRevision;
					Graphics()->QuadsBegin();
					DoQuadEnvPoint(&Quad, F->m_pEnvelope.get(), 0, 0);
					Graphics()->QuadsEnd();
					Check(Map()->m_ContentRevision == Same, "unchanged quad preview values do not advance revision");
				}
				Ui()->SetActiveItem(nullptr);
				m_QuadEnvelopePointOperation = EQuadEnvelopePointOperation::NONE;
				Mouse(SDL_BUTTON_LEFT, false);
				Tabs().Open(*this, F->m_A, F->m_B);
			});
			WaitReady();
			Do([CheckCaptured] { CheckCaptured("quad preview edit refreshes existing comparison"); });
		}
		for(int Replace = 0; Replace < 2; ++Replace)
		{
			Do([this, F] { Tabs().SelectMap(*this, F->m_B); });
			Delay();
			Do([this, F, Replace] { const auto Revision = Map()->m_ContentRevision; Check(Replace ? ReplaceSoundCallback("review-replace.opus",IStorage::TYPE_SAVE,this) : AddSound("review-add.opus",IStorage::TYPE_SAVE,this), "valid Opus sound callback succeeds"); Check(Map()->m_ContentRevision > Revision,"sound callback advances source revision"); Tabs().Open(*this,F->m_A,F->m_B); });
			WaitReady();
			Do([this, F, CheckCaptured] { CheckCaptured("sound callback refresh contains current sound bytes"); const auto &Sounds = CapturedSnapshot(Compare(),1).m_vSounds; Check(Sounds.size() == 1 && Sounds[0].m_vData.size() == (size_t)F->m_B->m_vpSounds[0]->m_DataSize && !std::memcmp(Sounds[0].m_vData.data(),F->m_B->m_vpSounds[0]->m_pData,Sounds[0].m_vData.size()),"captured sound bytes equal actual decoded callback resource"); });
		}
		Do([this, F] { m_ActiveExtraEditor = EXTRAEDITOR_NONE; for(size_t i = Maps().size(); i-- > 0;) if(Maps()[i].get() == F->m_A || Maps()[i].get() == F->m_B || Maps()[i].get() == F->m_C) CloseMap(i,false); });
		Delay();
	}

	void CheckWrappedInlineText()
	{
		const auto Area = (Compare().*Access(TSettings{})).*Access(TScrollRect{});
		const float Width = (Area.w - 8) / 2;
		float Height = 15;
		for(int Side = 0; Side < 2; ++Side)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = 10;
			Cursor.m_LineWidth = Width - 43;
			Cursor.m_Flags = 0;
			TextRender()->TextEx(&Cursor, (Side ? m_pAfter : m_pBefore)->m_vSettings[0].m_aCommand);
			Height = std::max(Height, Cursor.Height() + 8);
			if(Graphics()->WindowWidth() == 800)
				Check(Cursor.m_LineCount > 1, "multibyte inline fixture actually wraps at 800x600");
		}
		CImageInfo Actual;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Actual);
		if(!Actual.m_pData)
		{
			Check(false, "wrapped text framebuffer available");
			return;
		}
		for(int Side = 0; Side < 2; ++Side)
		{
			const std::string Text = (Side ? m_pAfter : m_pBefore)->m_vSettings[0].m_aCommand;
			const auto Color = Side ? ColorRGBA(0.25f, 1, 0.45f, 1) : ColorRGBA(1, 0.3f, 0.3f, 1);
			CUIRect Rect{Area.x + Side * (Width + 8), Area.y, Width, Height};
			const auto Background = Actual.PixelColor((Rect.x + Rect.w - 2) / Ui()->Screen()->w * Actual.m_Width, (Rect.y + 2) / Ui()->Screen()->h * Actual.m_Height);
			Rect.Draw(Background, IGraphics::CORNER_NONE, 0);
			CUIRect Number{Rect.x + 4, Rect.y + 4, 35, 12};
			SLabelProperties Props;
			Props.SetColor(Color);
			Props.m_EllipsisAtEnd = true;
			Props.m_MaxWidth = Number.w;
			Props.m_EnableWidthCheck = false;
			Ui()->DoLabel(&Number, "1", 9, TEXTALIGN_ML, Props);
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(Rect.x + 39, Rect.y + 4));
			Cursor.m_FontSize = 10;
			Cursor.m_LineWidth = Width - 43;
			// Literal expected edits; never read the production result ranges.
			for(const std::string Token : {Side ? "è" : "é", Side ? "間🙃" : "界🙂"})
				Cursor.m_vColorSplits.emplace_back(Text.find(Token), Token.size(), Color);
			TextRender()->TextColor(ColorRGBA(0.76f, 0.78f, 0.8f, 1));
			TextRender()->TextEx(&Cursor, Text.c_str());
			TextRender()->TextColor(ColorRGBA(1, 1, 1, 1));
		}
		CImageInfo Expected;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Expected);
		int Different = 0;
		for(int y = Area.y / Ui()->Screen()->h * Actual.m_Height; y < (Area.y + Height) / Ui()->Screen()->h * Actual.m_Height; ++y)
			for(int x = Area.x / Ui()->Screen()->w * Actual.m_Width; x < (Area.x + Area.w) / Ui()->Screen()->w * Actual.m_Width; ++x)
			{
				const auto A = Actual.PixelColor(x, y), B = Expected.PixelColor(x, y);
				Different += std::abs(A.r - B.r) > 0.02f || std::abs(A.g - B.g) > 0.02f || std::abs(A.b - B.b) > 0.02f;
			}
		Check(Different == 0, "wrapped multibyte glyph colors and neutral context match independent oracle; mismatched pixels=" + std::to_string(Different));
		if(Different)
		{
			CImageLoader::SavePng(Storage()->OpenFile("screenshots/polish-inline-actual.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "polish-inline-actual.png", Actual);
			CImageLoader::SavePng(Storage()->OpenFile("screenshots/polish-inline-expected.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "polish-inline-expected.png", Expected);
		}
		Actual.Free();
		Expected.Free();
	}

	void ChromePolishSteps()
	{
		auto Press = [this](int KeyCode) { Do([this, KeyCode] { Key(KeyCode, true); }); Delay(); Do([this, KeyCode] { Key(KeyCode, false); }); Delay(); };
		auto CheckChrome = [this](bool Hidden = false) {
			CImageInfo Image;
			static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
			Check(Image.m_pData != nullptr, "chrome framebuffer available");
			if(!Image.m_pData)
				return;
			for(float X : {Ui()->Screen()->w / 2, Ui()->Screen()->w - 30})
			{
				const auto Color = Image.PixelColor(X / Ui()->Screen()->w * Image.m_Width, 21 / Ui()->Screen()->h * Image.m_Height);
				Check(Hidden ? Color.r > 0.9f && Color.g < 0.1f && Color.b > 0.9f : std::abs(Color.r - Color.g) < 0.05f && std::abs(Color.g - Color.b) < 0.05f && Color.r < 0.6f,
					Hidden ? "GUI-hidden map uses the full viewport" : "opaque toolbar strip stays in front of dense map content");
			}
			if(!Hidden)
				for(float Y : {24.0f, 32.0f, 40.0f, 52.0f, 62.0f, 72.0f, 95.0f})
					for(float X : {10.0f, m_ToolBoxWidth + 5, Ui()->Screen()->w / 2, Ui()->Screen()->w - 30})
					{
						const auto Color = Image.PixelColor(X / Ui()->Screen()->w * Image.m_Width, Y / Ui()->Screen()->h * Image.m_Height);
						Check(!(Color.r > 0.85f && Color.g < 0.15f && Color.b > 0.85f), "tab and control rows occlude magenta world geometry at y=" + std::to_string(Y));
					}
			Image.Free();
		};
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
				str_copy(pMap->m_aDisplayName, Side ? "Chrome After" : "Chrome Before");
				// CreateDefault's derived zoom is only synchronized on its first
				// normal render. Establish that invariant for both owned fixtures.
				pMap->m_MapViewState.m_WorldZoom = pMap->m_MapViewState.m_Zoom.GetValue() / 100;
				pMap->m_Modified = true;
				pMap->m_vSettings = {Side ? "sv_name shared words shared words shared words shared words shared words cafè 世間🙃 shared end" : "sv_name shared words shared words shared words shared words shared words café 世界🙂 shared end"};
				for(int Group = 0; Group < 2; ++Group)
				{
					auto pGroup = pMap->NewGroup();
					pGroup->m_UseClipping = Group == 0;
					pGroup->m_ClipW = pGroup->m_ClipH = 100;
					auto pLayer = std::make_shared<CLayerQuads>(pMap.get());
					for(int i = 0; i < 32; ++i)
					{
						pLayer->NewQuad(i * 8 + Side, i * 8, 200000, 200000);
						for(auto &Color : pLayer->m_vQuads.back().m_aColors)
							Color = {255, 0, 255, 255};
					}
					pGroup->AddLayer(pLayer);
				}
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			Tabs().SelectMap(*this, m_pBefore);
		});
		Delay();
		for(int Width : {1280, 800})
		{
			Do([this, Width] { Graphics()->Resize(Width, Width == 800 ? 600 : 800, 0); OnWindowResize(); MapView()->Zoom()->SetValueInstant(Width == 800 ? 150 : 100); Map()->m_MapViewState.m_WorldOffset += vec2(100, 100); });
			Delay();
			Do([CheckChrome] { CheckChrome(); });
			Screenshot("polish-map-chrome-" + std::to_string(Width));
		}
		Do([this] { m_GuiActive = false; });
		Delay();
		Do([CheckChrome] { CheckChrome(true); });
		Do([this] { m_GuiActive = true; CaptureSources(); OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		for(int Width : {1280, 800})
		{
			Do([this, Width] { Graphics()->Resize(Width, Width == 800 ? 600 : 800, 0); OnWindowResize(); SelectKind(MapDiff::EEntryKind::LAYER); Compare().*Access(TContext{}) = 2; Compare().*Access(TCenter{}) += vec2(100, 100); Compare().*Access(TZoom{}) *= 1.5f; });
			Delay();
			Do([CheckChrome] { CheckChrome(); });
			Screenshot("polish-comparison-chrome-" + std::to_string(Width));
		}
		Do([this, CheckChrome] {
			CheckChrome();
			for(auto *pAction : {&m_QuickActionSave, &m_QuickActionSaveAs, &m_QuickActionSaveCopy, &m_QuickActionAppendMap, &m_QuickActionMapDetails, &m_QuickActionTestMapLocally, &m_QuickActionAddGroup, &m_QuickActionToggleGrid, &m_QuickActionEnvelopes})
			{
				Check(pAction->Disabled(), std::string("comparison disables ") + pAction->Label());
				pAction->Call();
			}
			Check(m_Dialog == DIALOG_NONE && !Ui()->IsPopupOpen(), "disabled callbacks open no editing dialogs");
			CheckSources("disabled comparison callbacks");
		});
		Click([this] { return SectionPosition(3); });
		Do([this] { CheckWrappedInlineText(); });
		Screenshot("polish-inline-wrapped-800");
		Do([this] { Graphics()->Resize(1280, 800, 0); OnWindowResize(); });
		Delay();
		Do([this] { CheckWrappedInlineText(); });
		Screenshot("polish-inline-wrapped-1280");
		Do([this] { Graphics()->Resize(800, 600, 0); OnWindowResize(); });
		Delay();
		Click([] { return vec2(20, 10); });
		Do([this] {
			Check(Ui()->IsPopupOpen(), "comparison File menu opens through shared chrome");
			CImageInfo Image;
			static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
			for(int Row = 0; Row < 2; ++Row)
			{
				int Glyphs = 0;
				float Brightest = 0;
				for(int y = Row ? 103 : 23; y < (Row ? 115 : 35); ++y)
					for(int x = 12; x < 45; ++x)
					{
						const auto Color = Image.PixelColor(x / Ui()->Screen()->w * Image.m_Width, y / Ui()->Screen()->h * Image.m_Height);
						Glyphs += Color.r > 0.4f;
						Brightest = std::max(Brightest, Color.r);
					}
				Check(Glyphs > 3 && (Row ? Brightest <= 0.62f : Brightest > 0.8f), Row ? "unavailable Save text stays visible and grey" : "available New text stays bright");
			}
			Image.Free();
		});
		Screenshot("polish-comparison-file-800");
		Click([] { return vec2(30, 108); });
		Do([this] { Check(Ui()->IsPopupOpen() && m_Dialog == DIALOG_NONE, "disabled Save click is inert"); Ui()->ClosePopupMenus(); });
		Click([] { return vec2(85, 10); });
		Do([this] { Check(Ui()->IsPopupOpen(), "comparison Tools menu remains reachable"); });
		Screenshot("polish-comparison-tools-800");
		Click([] { return vec2(100, 55); });
		Do([this] { Check(!m_PopupEventActivated && Ui()->IsPopupOpen(), "disabled editing tool opens no confirmation"); Ui()->ClosePopupMenus(); });
		Click([] { return vec2(150, 10); });
		Do([this] { Check(Ui()->IsPopupOpen(), "comparison Settings menu remains reachable"); });
		Screenshot("polish-comparison-settings-800");
		auto ReadSettings = [this] { return std::tuple{m_BrushColorEnabled, m_AllowPlaceUnusedTiles, m_ShowEnvelopePreview, m_ActiveEnvelopePreview}; };
		auto pSettings = std::make_shared<decltype(ReadSettings())>();
		Do([pSettings, ReadSettings] { *pSettings = ReadSettings(); });
		for(float Y : {43.0f, 57.0f, 85.0f})
			for(float X : {300.0f, 365.0f})
			{
				Click([X, Y] { return vec2(X, Y); });
				Do([this, pSettings, ReadSettings] { Check(Ui()->IsPopupOpen() && ReadSettings() == *pSettings, "disabled brush coloring, unused-tile policy and envelope preview controls preserve their values"); });
			}
		Do([this] { Ui()->ClosePopupMenus(); Key(SDLK_LCTRL, true); });
		Press(SDLK_s);
		Do([this] { Key(SDLK_LCTRL, false); Check(m_Dialog == DIALOG_NONE, "comparison save shortcut is inert"); });
		Delay();
		Click([] { return vec2(20, 10); });
		Click([] { return vec2(30, 49); });
		Do([this] { Check(m_Dialog == DIALOG_FILE, "Load menu opens a usable comparison dialog"); });
		Press(SDLK_ESCAPE);
		Do([this] { Check(m_Dialog == DIALOG_NONE && Tabs().Active(), "dialog Escape preserves comparison"); CheckSources("shared menus and shortcuts"); });
		Click([this] { return vec2(Ui()->Screen()->w - 9, 10); });
		Do([this] { Check(HasUnsavedData() && Ui()->IsPopupOpen() && m_PopupEventType == POPEVENT_EXIT && g_Config.m_ClEditor, "shared X opens the existing unsaved exit confirmation from comparison"); });
		Click([this] { return vec2(Ui()->Screen()->w / 2 - 130, Ui()->Screen()->h / 2 + 50); });
		Do([this] { Check(!Ui()->IsPopupOpen() && g_Config.m_ClEditor && Tabs().Active(), "canceling shared X leaves the comparison open"); CheckSources("comparison exit cancellation"); });
		Shortcut(SDLK_p);
		Do([this] { Check(m_Dialog == DIALOG_QUICK_PROMPT, "native Ctrl+P opens the comparison action prompt"); });
		TypeText("Save");
		Do([this] { Check(((this->*Access(TPrompt{})).*Access(TPromptResults{})).empty(), "action prompt filters out disabled editing actions"); });
		Press(SDLK_RETURN);
		Do([this] { Check(m_Dialog == DIALOG_QUICK_PROMPT && Tabs().Active(), "Enter cannot execute a disabled action from the prompt"); CheckSources("disabled prompt action"); });
		Press(SDLK_ESCAPE);
		Do([this] { Check(m_Dialog == DIALOG_NONE && Tabs().Active(), "prompt Escape owns the key and preserves comparison"); });
		auto pLoadState = std::make_shared<std::pair<CMapCompare *, const void *>>();
		auto ReadLoadView = [this] { return std::tuple{Compare().*Access(TSelectedEntry{}), Compare().*Access(TSelectedDetail{}), Compare().*Access(TCenter{}), Compare().*Access(TZoom{}), Maps().size()}; };
		auto pLoadView = std::make_shared<decltype(ReadLoadView())>();
		Do([this, pLoadState] { *pLoadState = {Tabs().Selected(), SnapshotIdentity(Compare())}; });
		Do([pLoadView, ReadLoadView] { *pLoadView = ReadLoadView(); });
		Shortcut(SDLK_p);
		TypeText("Load");
		Do([this] { const auto &Actions = (this->*Access(TPrompt{})).*Access(TPromptResults{}); Check(!Actions.empty() && Actions.front() == &m_QuickActionLoadMap, "prompt text filters to the allowed Load action"); });
		Press(SDLK_RETURN);
		Do([this] { Check(m_Dialog == DIALOG_FILE && Tabs().Active(), "prompt Enter routes Load into a usable native dialog"); });
		TypeText("coverage");
		Do([this] { Check(str_comp(((this->*Access(TFileBrowser{})).*Access(TFileFilter{})).GetString(), "coverage") == 0, "Load dialog receives native search text"); });
		Press(SDLK_DOWN);
		Press(SDLK_UP);
		Press(SDLK_END);
		Shortcut(SDLK_n);
		Do([this, pLoadView, ReadLoadView] { Check(m_Dialog == DIALOG_FILE && Tabs().Active() && ReadLoadView() == *pLoadView, "Load dialog list keys and shortcuts preserve comparison selection, camera and documents"); });
		Click([this] { const auto &Scroll = ((this->*Access(TFileBrowser{})).*Access(TFileList{})).*Access(TListScroll{}); const auto &Rect = Scroll.*Access(TScrollRect{}); return vec2(Rect.x + 80, Rect.y + (Scroll.*Access(TScrollOffset{})) + ((this->*Access(TFileBrowser{})).*Access(TFileSelection{})) * 15 + 7); });
		Press(SDLK_RETURN);
		Delay();
		Do([this] { Check(m_Dialog == DIALOG_NONE && !Tabs().Active() && Map() != m_pBefore && Map() != m_pAfter, "successful Load selects the new document after releasing dialog input"); CloseMap(Maps().size() - 1, false); OpenComparison(*this, m_pBefore, m_pAfter); });
		WaitReady();
		Do([this, pLoadState] { Check(Tabs().Selected() == pLoadState->first && SnapshotIdentity(Compare()) == pLoadState->second, "successful Load preserves the prior comparison and cached snapshot"); CheckSources("successful comparison Load"); });
		Click([] { return vec2(20, 10); });
		Click([] { return vec2(40, 194); });
		Do([this] { Check(!Tabs().Active(), "File Close targets the comparison document"); CheckSources("comparison File close"); m_QuickActionReopenComparison.Call(); });
		WaitReady();
		Do([this] { Check(Tabs().Active(), "Reopen restores a menu-closed comparison"); m_QuickActionNewMap.Call(); });
		Delay();
		Do([this] { Check(!Tabs().Active() && Map() != m_pBefore && Map() != m_pAfter, "New safely selects its new document from comparison"); CloseMap(Maps().size() - 1, false); m_QuickActionCompareMaps.Call(); });
		Delay();
		Click([] { return vec2(20, 10); });
		Do([this] { Check(State() == 1 && Ui()->IsPopupOpen() && m_QuickActionSave.Disabled(), "selector retains shared menus and command policy"); Ui()->ClosePopupMenus(); CloseComparison(); });
		Delay();
		Do([this] { Graphics()->Resize(1280, 800, 0); OnWindowResize(); for(size_t i = Maps().size(); i-- > 0;) if(Maps()[i].get() == m_pBefore || Maps()[i].get() == m_pAfter) CloseMap(i, false); });
		Delay();
	}

	void ModalOwnershipSteps()
	{
		auto Press = [this](int KeyCode) { Do([this, KeyCode] { Key(KeyCode, true); }); Delay(); Do([this, KeyCode] { Key(KeyCode, false); }); Delay(); };
		auto Move = [this](vec2 Point) { Ui()->OnCursorMove((Point.x - Ui()->MouseX()) * Graphics()->WindowWidth() / Ui()->Screen()->w, (Point.y - Ui()->MouseY()) * Graphics()->WindowHeight() / Ui()->Screen()->h); };
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				str_copy(pMap->m_aDisplayName, Side ? "Modal After" : "Modal Before");
				auto pGroup = pMap->NewGroup();
				str_copy(pGroup->m_aName, "Modal layers");
				for(int Index = 0; Index < 100; ++Index)
				{
					pMap->m_vSettings.emplace_back(("command " + std::to_string(Index) + (Side ? " after" : " before")).c_str());
					auto pEnv = std::make_shared<CEnvelope>(1);
					str_format(pEnv->m_aName, sizeof(pEnv->m_aName), "Modal %d", Index);
					for(int Point = 0; Point < (Index == 0 ? 100 : 2); ++Point)
						pEnv->AddPoint(CFixedTime(Point * 100), {Side * 1024, 0, 0, 0});
					pMap->m_vpEnvelopes.push_back(pEnv);
					auto pLayer = std::make_shared<CLayerTiles>(pMap.get(), 1, 1);
					str_format(pLayer->m_aName, sizeof(pLayer->m_aName), "Layer %d", Index);
					pLayer->m_pTiles[0].m_Index = Side + 1;
					pGroup->AddLayer(pLayer);
				}
				(Side ? m_pAfter : m_pBefore) = pMap.get();
				Maps().push_back(std::move(pMap));
			}
			CaptureSources();
			OpenComparison(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		auto ReadState = [this] {
			return std::tuple{Compare().*Access(TSection{}), Compare().*Access(TSelectedEntry{}), Compare().*Access(TSelectedDetail{}), Compare().*Access(TFocusEntry{}), Compare().*Access(TTreeActive{}), Compare().*Access(TEnvelopeChannels{}), Compare().*Access(TCenter{}), Compare().*Access(TZoom{}), Compare().*Access(TSidebarWidth{}),
				(Compare().*Access(TSettings{})).*Access(TScrollPos{}), (Compare().*Access(TLayerTree{})).*Access(TScrollPos{}), ((Compare().*Access(TNavLists{}))[Compare().*Access(TSection{})].*Access(TListScroll{})).*Access(TScrollPos{}), ((Compare().*Access(TDetails{})).*Access(TListScroll{})).*Access(TScrollPos{}), (Compare().*Access(TValues{})).*Access(TScrollPos{}), Maps().size()};
		};
		auto pState = std::make_shared<decltype(ReadState())>();
		for(int Section : {1, 2, 3})
		{
			Click([this, Section] { return SectionPosition(Section); });
			if(Section == 2)
				Do([this] { SelectKind(MapDiff::EEntryKind::ENVELOPE, "Modal 0"); });
			Delay(25);
			for(int Modal = 0; Modal < 3; ++Modal)
			{
				Do([this, pState, ReadState] { Compare().*Access(TTreeActive{}) = true; *pState = ReadState(); });
				if(Modal < 2)
					Shortcut(Modal ? SDLK_p : SDLK_o);
				else
					Click([] { return vec2(150, 10); });
				Do([this, Modal] { Check(Modal == 2 ? Ui()->IsPopupOpen() : m_Dialog == (Modal ? DIALOG_QUICK_PROMPT : DIALOG_FILE), "native modal owns comparison input"); });
				for(int Area = 0; Area < (Section == 1 ? 1 : 2); ++Area)
				{
					Do([this, Section, Area, Move] {
						auto &Scroll = Section == 1 ? Compare().*Access(TLayerTree{}) : Area == 0 ? (Compare().*Access(TNavLists{}))[Section].*Access(TListScroll{}) :
													Section == 2      ? (Compare().*Access(TDetails{})).*Access(TListScroll{}) :
															    Compare().*Access(TSettings{});
						// Settings has one navigator item; its paired command surface overflows.
						if(Section != 3 || Area != 0)
							Check(Scroll.ContentOverflows(), "modal wheel target has real scrollable content");
						Move((Scroll.*Access(TScrollRect{})).Center());
					});
					Delay();
					Do([] { SDL_Event Event{}; Event.type = SDL_MOUSEWHEEL; Event.wheel.y = -1; SDL_PushEvent(&Event); });
					Delay(20);
					Do([this, pState, ReadState] { Check(ReadState() == *pState, "modal wheel leaves comparison scroll, focus, selection and camera unchanged"); });
				}
				if(Modal < 2)
				{
					Press(SDLK_DOWN);
					Press(SDLK_END);
					Shortcut(SDLK_n);
					Press(SDLK_p);
					Press(SDLK_n);
					Do([this, pState, ReadState] { Check(ReadState() == *pState, "modal keyboard owns arrows, navigation and global shortcuts"); });
				}
				Do([this, Section, Move] {
					if(Section == 2)
					{
						const auto Graph = EnvelopeGraph(0);
						Move(vec2(Graph.x + Graph.w * 50.0f / 99, Graph.y + Graph.h * (1 - 0.08f / 1.16f)));
					}
					else
					{
						const auto &Scroll = Section == 3 ? Compare().*Access(TSettings{}) : Compare().*Access(TLayerTree{});
						const auto &Rect = Scroll.*Access(TScrollRect{});
						Move(vec2(Rect.x + 50, Rect.y + 27));
					}
				});
				Delay();
				Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
				Delay();
				Do([this] { Check(Ui()->MouseButtonClicked(0) || Ui()->MouseButton(0), "modal probe receives the native mouse press"); Mouse(SDL_BUTTON_LEFT, false); });
				Delay();
				Do([this, pState, ReadState] { Check(ReadState() == *pState, "modal click leaves underlying envelope markers, settings focus and tree selection unchanged"); if(m_Dialog != DIALOG_NONE || Ui()->IsPopupOpen()) Key(SDLK_ESCAPE, true); });
				Delay();
				Do([this] { Key(SDLK_ESCAPE, false); });
				Delay();
				Do([this, pState, ReadState] { Check(Tabs().Active() && m_Dialog == DIALOG_NONE && !Ui()->IsPopupOpen() && ReadState() == *pState, "modal Escape or outside-popup click preserves the comparison state"); CheckSources("modal input ownership"); });
			}
			if(Section == 2)
			{
				Click([this] { const auto Graph = EnvelopeGraph(0); return vec2(Graph.x + Graph.w * 50.0f / 99, Graph.y + Graph.h * (1 - 0.08f / 1.16f)); });
				Do([this] { Check((Compare().*Access(TSelectedDetail{})) == 50 && !(Compare().*Access(TTreeActive{})), "the same uncovered envelope marker really changes selection and focus"); });
			}
		}
		Do([this] { CloseComparison(); });
		Delay();
		Do([this] { for(size_t i = Maps().size(); i-- > 0;) if(Maps()[i].get() == m_pBefore || Maps()[i].get() == m_pAfter) CloseMap(i, false); });
		Delay();
	}

	void Setup()
	{
		Delay(10);
		if(Storage()->FileExists("maps/perf-before.map", IStorage::TYPE_SAVE) && Storage()->FileExists("maps/perf-after.map", IStorage::TYPE_SAVE))
		{
			PerformanceSteps(true);
			Do([this] { m_Log << "RESULT failures=" << m_Failures << " steps=" << m_vSteps.size() << std::endl; Client()->Quit(); });
			return;
		}
		ChromePolishSteps();
		ModalOwnershipSteps();
		if(std::getenv("DDNET_MAP_COMPARE_POLISH"))
		{
			Do([this] { m_Log << "RESULT failures=" << m_Failures << " steps=" << m_vSteps.size() << std::endl; Client()->Quit(); });
			return;
		}
		ReviewSteps();
		TabLifecycleSteps();
		if(std::getenv("DDNET_MAP_COMPARE_REVIEW"))
		{
			Do([this] { m_Log << "RESULT failures=" << m_Failures << " steps=" << m_vSteps.size() << std::endl; Client()->Quit(); });
			return;
		}
		RegressionSteps();
		FitRegressionSteps();
		FullContextSteps();
		NavigationSteps();
		SectionScrollSteps();
		ObjectViewSteps();
		SectionAndSkySteps();
		SectionReviewSteps();
		PerformanceSteps(false);
		const char *apNames[] = {"coverage", "Tutorial", "Tsunami", "Sunny Side Up"};
		for(int Fixture = 0; Fixture < 4; ++Fixture)
		{
			const std::string Name = apNames[Fixture];
			Do([this, Name] { LoadPair(Name); CaptureSources(); OpenComparison(*this,m_pBefore,m_pAfter); });
			WaitReady();
			Do([this, Name] { Check(Result().ChangeCount()==0,"equal real map "+Name); CheckSources(Name+" identical viewer"); });
			if(Fixture == 0)
			{
				Screenshot("comparison-equal");
				Click([] { return vec2(240, 62); });
				Do([this] {
					Check((Compare().*Access(TUnchangedExpanded{})) && (Compare().*Access(TSelectedEntry{})) >= 0, "equal maps open unchanged section with a real selection");
					const int Selected = Compare().*Access(TSelectedEntry{});
					Check(Selected >= 0 && (Result().m_vEntries[Selected].m_Kind == MapDiff::EEntryKind::LAYER || Result().m_vEntries[Selected].m_Kind == MapDiff::EEntryKind::GROUP), "equal-map selection context starts on spatial content");
					m_SavedEntry = Compare().*Access(TSelectedEntry{});
					m_SavedCenter = Compare().*Access(TCenter{});
					Key(SDLK_n, true);
				});
				Delay(2);
				Do([this] { Key(SDLK_n, false); Key(SDLK_p, true); });
				Delay(2);
				Do([this] { Key(SDLK_p, false); Check((Compare().*Access(TSelectedEntry{})) == m_SavedEntry && (Compare().*Access(TCenter{})) == m_SavedCenter, "equal-map Prev/Next preserve selection and camera"); });
				Click([this] { return vec2(100, NavigatorTop() + 18); });
				Do([this] { Check(!(Compare().*Access(TUnchangedExpanded{})) && (Compare().*Access(TSelectedEntry{})) == m_SavedEntry, "equal maps collapse without replacing the inspected selection"); });
				Do([this] { Key(SDLK_END, true); });
				Delay(2);
				Do([this] { Key(SDLK_END, false); Check((Compare().*Access(TSelectedEntry{})) == m_SavedEntry, "End cannot select a header-only equal-map list"); });
			}
			Do([this] { CloseComparison(); });
			Delay();
			Do([this] { Mutate(); CaptureSources(); });
			if(Fixture == 0)
			{
				// Exercise the same actual popup callback used by a tab's context menu.
				Do([this] { m_PopupMapTab.m_pEditor=this; m_PopupMapTab.m_SelectedMap=Maps().size()-1; Ui()->DoPopupMenu(&m_PopupMapTab,100,100,170,96,&m_PopupMapTab,CPopupMapTab::Render); });
				Click([] { return vec2(140, 123); });
			}
			else
				Do([this] { OpenComparison(*this, m_pBefore, m_pAfter); });
			WaitReady();
			Do([this, Name] { Check(Result().ChangeCount()>5,"unsaved changes "+Name); Check((Compare().*Access(TSources{}))[0]==m_pBefore && (Compare().*Access(TSources{}))[1]==m_pAfter,"context direction "+Name); SelectKind(MapDiff::EEntryKind::LAYER,"Game"); });
			Delay();
			Screenshot("comparison-" + std::to_string(Fixture) + "-tiles");
			Do([this] { SelectKind(MapDiff::EEntryKind::LAYER, "Added quad"); });
			Delay();
			if(Fixture == 0)
				Screenshot("comparison-quads");
			Click([] { return vec2(167, 62); }); // By group toolbar button.
			Do([this] { Check(Compare().*Access(TGroup{}),"By group button"); SelectKind(MapDiff::EEntryKind::GROUP,"Smoke"); });
			Delay();
			Screenshot("comparison-" + std::to_string(Fixture) + "-group");
			ToggleMapContext();
			Do([this] { Check(Compare().*Access(TContext{}), "context toggle actual=" + std::to_string(Compare().*Access(TContext{})) + " target=" + std::to_string(m_TargetContext)); });
			Click([] { return vec2(106, 62); });
			WaitReady();
			Do([this] { Check((Compare().*Access(TSources{}))[0]==m_pAfter,"Swap button"); SelectKind(MapDiff::EEntryKind::GROUP,"Smoke"); });
			Delay();
			if(Fixture == 0)
				Screenshot("comparison-swapped");
			// New and Load are available global commands, exercised in ChromePolishSteps.
			for(SDL_Keycode K : {SDLK_z, SDLK_y, SDLK_s, SDLK_v, SDLK_a, SDLK_g})
			{
				Do([this, K] { Key(SDLK_LCTRL,true); Key(K,true); });
				Delay(2);
				Do([this, K] { Key(K,false); Key(SDLK_LCTRL,false); });
				Delay(2);
				Do([this] { Check(Compare().Active() && m_Dialog == DIALOG_NONE, "map-only shortcut keeps comparison active without opening a dialog"); });
			}
			Do([this] { IInput::CEvent E{}; E.m_Flags=IInput::FLAG_TEXT; str_copy(E.m_aText,"Should not edit"); OnInput(E); });
			Click([this] { return vec2(Ui()->Screen()->w * 0.6f, 180); });
			Do([this, Name] { CheckSources(Name+" attempted edits"); Key(SDLK_ESCAPE,true); Mouse(SDL_BUTTON_LEFT,true); });
			Delay(3);
			Do([this] { Check(Compare().Active(),"closing frame waits for held mouse"); Key(SDLK_ESCAPE,false); Mouse(SDL_BUTTON_LEFT,false); });
			Delay();
			Do([this, Name] { Check(!Compare().Active(),"Escape exits"); CheckSources(Name+" after exit"); });
			if(Fixture == 0)
			{
				Do([this] { static SPopupMenuId s_Tools; Ui()->DoPopupMenu(&s_Tools,100,100,150,130,this,PopupMenuTools); });
				Click([] { return vec2(130, 109); });
				Do([this] { Check(State() == 1, "Tools opens pair selector"); });
				Screenshot("comparison-selector");
				// Choose last document in After using its stable identity for this callback test.
				Do([this] { (Compare().*Access(TSources{}))[1] = m_pAfter; });
				Click([this] { return vec2(Ui()->Screen()->w - 90, Ui()->Screen()->h - 38); });
				WaitReady();
				Do([this] { Check(Result().ChangeCount()>5,"pair selector compare button"); Graphics()->Resize(1100,700,0); OnWindowResize(); });
				Delay();
				Screenshot("comparison-resized");
				Do([this] { CloseMap(Maps().size()-1,false); Check(!Tabs().Active(),"source disposal closes dependent comparison"); });
				Delay();
				Screenshot("comparison-source-closed");
				Do([this] { CloseComparison(); });
				Delay();
				Do([this] { OpenComparison(*this,Maps()[0].get(),m_pBefore); Check(HandleMapDrop("maps/coverage.map",IStorage::TYPE_ALL),"external map drop loaded"); Check(Tabs().Transitioning(),"external load safely selects its new map without discarding comparisons"); CloseComparison(); });
				Delay();
			}
		}
		Do([this] { m_Log<<"RESULT failures="<<m_Failures<<" steps="<<m_vSteps.size()<<std::endl; Client()->Quit(); });
	}

public:
	void OnUpdate() override
	{
		// Scripted positions use Ui()->OnCursorMove. Isolate them from unrelated
		// physical mouse movement without changing native button/key delivery.
		SDL_GetRelativeMouseState(nullptr, nullptr);
		auto *pDocument = Tabs().Selected();
		const void *pSnapshot = pDocument ? SnapshotIdentity(*pDocument) : nullptr;
		const auto Start = std::chrono::steady_clock::now();
		CEditor::OnUpdate();
		if(pDocument && pDocument == Tabs().Selected() && HasSnapshot(*pDocument) && SnapshotIdentity(*pDocument) != pSnapshot)
		{
			++m_CaptureCount;
			m_Log << "TIMING capture generation=" << m_CaptureCount << " capture_pair_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count() << std::endl;
		}
	}
	void OnRender() override
	{
		const auto RenderStart = std::chrono::steady_clock::now();
		CEditor::OnRender();
		if(m_MeasureRender)
		{
			CImageInfo Image;
			static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
			Image.Free();
			m_LastRenderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RenderStart).count();
		}
		if(m_vSteps.empty())
		{
			m_Log.open("map-compare-native-results.log");
			m_Log << "INPUT physical relative mouse motion isolated; scripted cursor and native button/key assertions enabled" << std::endl;
			g_Config.m_EdAutosaveInterval = 0;
			Setup();
		}
		++m_Frames;
		++m_StepFrames;
		if(m_Step < m_vSteps.size() && m_vSteps[m_Step]())
		{
			m_Log << "STEP " << m_Step << " state " << State() << std::endl;
			++m_Step;
			m_StepFrames = 0;
		}
		if(m_StepFrames > 600 || m_Frames > 10000)
		{
			Check(false, "timeout step " + std::to_string(m_Step) + " state " + std::to_string(State()));
			m_Log << "RESULT timeout" << std::endl;
			Client()->Quit();
		}
	}
};
IEditor *CreateEditor() { return new CSmokeEditor; }
