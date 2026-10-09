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

#include <SDL.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>

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
ACCESSOR(TCompare, CEditor, m_MapCompare, CMapCompare)
ACCESSOR(TMaps, CEditor, m_vpMaps, std::vector<std::unique_ptr<CEditorMap>>)
ACCESSOR(TSelectedMap, CEditor, m_SelectedMap, size_t)
ACCESSOR(TResult, CMapCompare, m_Result, MapDiff::CResult)
ACCESSOR(TContext, CMapCompare, m_Context, bool)
ACCESSOR(TGroup, CMapCompare, m_ByGroup, bool)
ACCESSOR(TSelectedEntry, CMapCompare, m_SelectedEntry, int)
ACCESSOR(TSelectedDetail, CMapCompare, m_SelectedDetail, int)
ACCESSOR(TFocusEntry, CMapCompare, m_FocusEntry, int)
ACCESSOR(TNavigator, CMapCompare, m_vNavigator, std::vector<int>)
ACCESSOR(TCenter, CMapCompare, m_Center, vec2)
ACCESSOR(TOrigin, CMapCompare, m_ParallaxOrigin, vec2)
ACCESSOR(TZoom, CMapCompare, m_Zoom, float)
ACCESSOR(TValues, CMapCompare, m_Values, CScrollRegion)
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
template<auto Member>
struct CDataAccess
{
	friend bool HasSnapshot(CMapCompare &Obj) { return static_cast<bool>(Obj.*Member); }
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
int CompareState(CMapCompare &);
template<auto Member>
struct CStateAccess
{
	friend int CompareState(CMapCompare &Obj) { return static_cast<int>(Obj.*Member); }
};
template struct CStateAccess<&CMapCompare::m_State>;

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
	double m_LastRenderMs = 0;
	vec2 m_SavedCenter;
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
	} m_aContexts[2];
	std::shared_ptr<CLayerGroup> m_pSavedBrush;
	std::vector<int> m_SavedBrushCells;
	std::ofstream m_Log;
	CMapCompare &Compare() { return this->*Access(TCompare{}); }
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
		m_vSteps.push_back([this] { return State() == 4; });
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
	void Click(std::function<vec2()> Pos)
	{
		Do([this, Pos] { const auto P=Pos(); Ui()->OnCursorMove((P.x-Ui()->MouseX())*Graphics()->WindowWidth()/Ui()->Screen()->w, (P.y-Ui()->MouseY())*Graphics()->WindowHeight()/Ui()->Screen()->h); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, true); });
		Delay();
		Do([this] { Mouse(SDL_BUTTON_LEFT, false); });
		Delay();
	}
	void Screenshot(const std::string &Name)
	{
		Do([this, Name] { CImageInfo Image; static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image); const std::string Path="screenshots/"+Name+".png"; Check(Image.m_pData && CImageLoader::SavePng(Storage()->OpenFile(Path.c_str(),IOFLAG_WRITE,IStorage::TYPE_SAVE),Path.c_str(),Image),"render screenshot "+Name); Image.Free(); });
		Delay();
	}
	void CaptureSources()
	{
		for(int i = 0; i < 2; ++i)
		{
			auto *p = i ? m_pAfter : m_pBefore;
			Check(MapDiff::Capture(*p, m_aSaved[i]), "capture source");
			m_aContexts[i] = {p->m_Modified, p->m_ModifiedAuto, p->m_SelectedGroup, p->m_vSelectedLayers, p->m_vSelectedQuads, p->m_MapViewState.m_WorldOffset, p->m_MapViewState.m_EditorOffset, p->m_MapViewState.m_WorldZoom, p->m_EnvelopeEvaluator.m_AnimateTime, p->m_EditorHistory.m_vpUndoActions.size(), p->m_EditorHistory.m_vpRedoActions.size()};
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
			MapDiff::CSnapshot Now;
			Check(MapDiff::Capture(*p, Now) && MapDiff::Compare(m_aSaved[i], Now).ChangeCount() == 0, Label + " source contents " + std::to_string(i));
			const auto &C = m_aContexts[i];
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
		Compare().Open(*this, m_pBefore, m_pAfter);
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
		Click([] { return vec2(240, 16); });
		Do([this] {
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::IMAGE && Result().m_vEntries[i].m_Name == "unused identical white")
					(Compare().*Access(TSelect{}))(i);
		});
		Delay();
		Do([this] {
			Check((Compare().*Access(TValueRows{})).empty(), "unused identical resource has no false affected-layer references");
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::IMAGE && Result().m_vEntries[i].m_Name == "white")
					(Compare().*Access(TSelect{}))(i);
		});
		Delay();
		Do([this] { Check((Compare().*Access(TValueRows{})).size() == 2, "used resource lists its actual two source layer references"); });
		Click([] { return vec2(240, 16); });
		Do([this] { SelectKind(MapDiff::EEntryKind::LAYER, "Pixel tiles"); });
		Delay();
		Do([this] { CheckPixels(); Key(SDLK_HOME,true); });
		Delay(2);
		Do([this] { Key(SDLK_HOME,false); Check((Compare().*Access(TSelectedEntry{}))==m_SavedEntry,"Home fits without navigator selection change"); m_SavedCenter=Compare().*Access(TCenter{}); });
		Click([] { return vec2(167, 16); });
		Click([] { return vec2(167, 16); });
		Do([this] { Check((Compare().*Access(TSelectedEntry{})) == m_SavedEntry, "group to layer returns to focused child"); });
		Click([] { return vec2(240, 16); });
		Do([this] { for(int i=0;i<(int)Result().m_vEntries.size();++i) if(Result().m_vEntries[i].m_Name=="Unchanged") { (Compare().*Access(TSelect{}))(i); break; } });
		Click([] { return vec2(240, 16); });
		Do([this] { const auto &Nav=Compare().*Access(TNavigator{}); const int Selected=Compare().*Access(TSelectedEntry{}); Check(std::find(Nav.begin(),Nav.end(),Selected)!=Nav.end(),"hiding context selects visible navigator entry"); Key(SDLK_n,true); });
		Delay(2);
		Do([this] { Key(SDLK_n,false); const auto &Nav=Compare().*Access(TNavigator{}); Check(std::find(Nav.begin(),Nav.end(),Compare().*Access(TSelectedEntry{}))!=Nav.end(),"Next remains coherent after context change"); SelectKind(MapDiff::EEntryKind::SETTINGS); Graphics()->Resize(800,600,0); OnWindowResize(); });
		Delay();
		Do([this] { const float Height=(Compare().*Access(TValues{})).*Access(TContentSize{}); m_Log << "INSPECTOR content_size=" << Height << std::endl; Check(Height>76,"wrapped inspector value expands scroll extent"); Graphics()->Resize(1280,800,0); OnWindowResize(); Compare().Close(); });
		Delay();
		Do([this] { Check(!HasSnapshot(Compare()) && Result().m_vEntries.empty(),"closed viewer releases owned snapshots/results");
   m_vPixelLayers[1]->m_pTiles[180+190*256].m_Index=1; str_copy(m_pAfter->m_MapInfo.m_aAuthor,m_pBefore->m_MapInfo.m_aAuthor); m_pAfter->m_vSettings=m_pBefore->m_vSettings;
   Compare().Open(*this,m_pBefore,m_pAfter); });
		WaitReady();
		Do([this] { Check(Result().ChangeCount()==0 && (Compare().*Access(TSelectedEntry{}))==-1,"changed to equal reopening has no stale selection"); Compare().Close(); });
		Delay();
		Do([this] { Compare().Open(*this,m_pBefore,m_pAfter); Compare().OnUpdate(*this); const auto Start=std::chrono::steady_clock::now(); Compare().Close(); Compare().Open(*this,m_pBefore,m_pAfter); Check(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count()<100,"cancel and immediate reopen do not wait for unfinished future"); });
		WaitReady();
		Do([this] { Check(Result().ChangeCount()==0,"reopened comparison completes after cancelled work is reaped"); Compare().Close(); });
		Delay();
	}
	void FitRegressionSteps()
	{
		Do([this] {
			for(int Side = 0; Side < 2; ++Side)
			{
				auto pMap = std::make_unique<CEditorMap>(this);
				pMap->CreateDefault();
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
			Compare().Open(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		auto SelectGroup = [this] {
			for(int i = 0; i < (int)Result().m_vEntries.size(); ++i)
				if(Result().m_vEntries[i].m_Kind == MapDiff::EEntryKind::GROUP && Result().m_vEntries[i].m_Name == "Fit test")
					(Compare().*Access(TSelect{}))(i);
		};
		Do(SelectGroup);
		Delay();
		Do([this] { CheckPixels(vec2(2000 * 32 + 16, 900 * 32 + 16)); Compare().Close(); });
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
			Compare().Open(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do(SelectGroup);
		Delay();
		Do([this] { CheckPixels(vec2(2000 * 32 + 16, 900 * 32 + 16)); Compare().Close(); });
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
	void ContextPixels(bool Context, bool Group)
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
			Sample(8, Context ? Over(Over(Background, ColorRGBA(1, 0, 0, 1), 0.25f), ColorRGBA(0, 0, 1, 1), 0.25f) : Background, "cross-group context preserves source order");
			Sample(9, Context ? Over(Background, ColorRGBA(0, 0, 1, 1), 0.25f) : Background, "unselected changes remain neutral context");
			Sample(10, Context ? Over(Over(Background, ColorRGBA(0, 0, 1, 1), 0.25f), ColorRGBA(1, 1, 1, 1), 0.65f) : Background, "selected layer context stays prominent above scenery");
			Sample(11, Context ? Over(Background, ColorRGBA(1, 1, 1, 1), Group ? 0.65f : 0.25f) : Background, "sibling layer follows layer or group focus");
			Sample(12, Side ? ColorRGBA(0.25f, 1, 0.45f, 1) : ColorRGBA(1, 0.3f, 0.3f, 1), "selected change remains visible");
			Sample(14, Context ? Over(Background, ColorRGBA(0, 1, 0, 1), 0.25f) : Background, "background group offset and clipping");
			Sample(15, Background, "clipped background tile remains hidden");
			Sample(16, Context ? Over(Background, ColorRGBA(0, 1, 1, 1), 0.25f) : Background, "untextured context uses the matching vertex layout");
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
			for(int Probe = 0; Probe < 2; ++Probe)
			{
				// The source quad is at (12.5, 14.5) tiles. Before has 0%
				// parallax, After 50%; the reference position is (128, 64).
				const vec2 World(12 * 32 + 16 - Probe * 64, 14 * 32 + 16);
				const auto Pane = (Compare().*Access(TPanes{}))[Side];
				const vec2 Point = Pane.Center() + (World - (Compare().*Access(TCenter{}))) / (Compare().*Access(TZoom{}));
				Check(Pane.Inside(Point), "parallax probe inside pane");
				const auto Color = Image.PixelColor((int)(Point.x / Ui()->Screen()->w * Image.m_Width), (int)(Point.y / Ui()->Screen()->h * Image.m_Height));
				Check(std::abs(Color.r - (Side == Probe ? 0.31f : 0.08f)) < 0.012f && std::abs(Color.g - (Side == Probe ? 0.3175f : 0.09f)) < 0.012f, "background parallax follows source with shared pan/zoom");
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
				Add(pMap->NewGroup(), "Red", ColorRGBA(1, 0, 0, 1), {8});
				pTiles = Add(pMap->NewGroup(), "Blue", ColorRGBA(0, 0, 1, 1), {8, 9, 10, 12});
				pTiles->m_pTiles[9 + 10 * 64].m_Index = Side ? 2 : 1;
				auto pClipped = pMap->NewGroup();
				pClipped->m_OffsetX = 32;
				pClipped->m_ParallaxX = pClipped->m_ParallaxY = 0;
				pClipped->m_UseClipping = 1;
				pClipped->m_ClipX = 14 * 32;
				pClipped->m_ClipY = 10 * 32;
				pClipped->m_ClipW = pClipped->m_ClipH = 32;
				Add(pClipped, "Clipped", ColorRGBA(0, 1, 0, 1), {15, 16});
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
			Compare().Open(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		auto Camera = [this] { Compare().*Access(TCenter{}) = vec2(12 * 32, 10 * 32 + 16); Compare().*Access(TZoom{}) = 1.0f; };
		Do([this, Camera] { SelectKind(MapDiff::EEntryKind::LAYER, "Focus"); Camera(); });
		Delay();
		Do([this] { ContextPixels(false, false); });
		Click([] { return vec2(240, 16); });
		Do([this] { ContextPixels(true, false); CheckCachedPane(); CheckContextBufferPixels(); });
		Screenshot("full-map-context-layer");
		Click([] { return vec2(167, 16); });
		Do(Camera);
		Delay();
		Do([this] { ContextPixels(true, true); });
		Screenshot("full-map-context-group");
		for(const auto &[Center, Zoom] : {std::pair{vec2(50 * 32, 12 * 32), 20.0f}, std::pair{vec2(50 * 32, 12 * 32), 0.5f}, std::pair{vec2(10000, 10000), 1.0f}, std::pair{vec2(12 * 32, 10 * 32 + 16), 1.0f}})
		{
			Do([this, Center, Zoom] { Compare().*Access(TCenter{}) = Center; Compare().*Access(TZoom{}) = Zoom; });
			Delay();
			Do([this] { CheckContextBufferPixels(); });
		}
		// Exercise the sparse immediate fallback against the same pixel oracle.
		Do([this] { static_cast<CGraphics_Threaded *>(Graphics())->*Access(TTileBuffering{}) = false; });
		Delay();
		Do([this] { ContextPixels(true, true); static_cast<CGraphics_Threaded *>(Graphics())->*Access(TTileBuffering{}) = true; CheckSources("full map context"); Compare().Close(); Check(!HasRenderCache(Compare()), "close releases comparison GPU cache"); });
		Delay();
		Do([this] { Compare().Open(*this, m_pBefore, m_pAfter); });
		WaitReady();
		Click([] { return vec2(240, 16); });
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
		});
		Delay();
		Do([this] { CheckParallaxContext(); Compare().*Access(TCenter{}) += vec2(32, 64); Compare().*Access(TZoom{}) = 2; });
		Delay();
		Do([this] { CheckParallaxContext(); });
		Do([this] { Check(HasRenderCache(Compare()), "reopen creates comparison cache"); Compare().Invalidate("context cache test"); Check(!HasRenderCache(Compare()), "source invalidation releases comparison cache"); Compare().Close(); });
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
			Compare().*Access(TGroup{}) = false;
			Compare().Open(*this, m_pBefore, m_pAfter);
		});
		WaitReady();
		Do([this] {
			SelectKind(MapDiff::EEntryKind::LAYER, "Perf layer");
			Check((Compare().*Access(TZoom{})) <= 20.0f, "large layer automatic fit stays at inspection scale");
		});
		MeasureFrames(LocalMaps ? "local context off" : "dense context off");
		if(LocalMaps)
			Screenshot("performance-layer");
		Click([] { return vec2(240, 16); });
		MeasureFrames(LocalMaps ? "local context on" : "dense context on");
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
		Click([] { return vec2(167, 16); });
		Do([this] { Check((Compare().*Access(TZoom{})) <= 20.0f, "large group fit stays at inspection scale"); });
		MeasureFrames(LocalMaps ? "local group context on" : "dense group context on");
		MeasureMotion(LocalMaps ? "local group full context pan/zoom" : "dense group full context pan/zoom");
		Click([] { return vec2(240, 16); });
		MeasureFrames(LocalMaps ? "local group context off" : "dense group context off");
		Do([this] { Compare().Close(); });
		Delay();
		if(!LocalMaps)
		{
			Do([this] {
				auto pGroup = m_pAfter->m_vpGroups.back();
				str_copy(pGroup->m_aName, m_pBefore->m_vpGroups.back()->m_aName);
				str_copy(pGroup->m_vpLayers.back()->m_aName, m_pBefore->m_vpGroups.back()->m_vpLayers.back()->m_aName);
				Compare().*Access(TGroup{}) = false;
				Compare().Open(*this, m_pBefore, m_pAfter);
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
			Click([] { return vec2(240, 16); });
			MeasureFrames("dense tile-only context on at far edge");
			Do([this] { Compare().Close(); });
			Delay();
		}
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
		RegressionSteps();
		FitRegressionSteps();
		FullContextSteps();
		PerformanceSteps(false);
		const char *apNames[] = {"coverage", "Tutorial", "Tsunami", "Sunny Side Up"};
		for(int Fixture = 0; Fixture < 4; ++Fixture)
		{
			const std::string Name = apNames[Fixture];
			Do([this, Name] { LoadPair(Name); CaptureSources(); Compare().Open(*this,m_pBefore,m_pAfter); });
			WaitReady();
			Do([this, Name] { Check(Result().ChangeCount()==0,"equal real map "+Name); CheckSources(Name+" identical viewer"); });
			if(Fixture == 0)
				Screenshot("comparison-equal");
			Do([this] { Compare().Close(); });
			Delay();
			Do([this] { Mutate(); CaptureSources(); });
			if(Fixture == 0)
			{
				// Exercise the same actual popup callback used by a tab's context menu.
				Do([this] { m_PopupMapTab.m_pEditor=this; m_PopupMapTab.m_SelectedMap=Maps().size()-1; Ui()->DoPopupMenu(&m_PopupMapTab,100,100,170,96,&m_PopupMapTab,CPopupMapTab::Render); });
				Click([] { return vec2(140, 123); });
			}
			else
				Do([this] { Compare().Open(*this, m_pBefore, m_pAfter); });
			WaitReady();
			Do([this, Name] { Check(Result().ChangeCount()>5,"unsaved changes "+Name); Check((Compare().*Access(TSources{}))[0]==m_pBefore && (Compare().*Access(TSources{}))[1]==m_pAfter,"context direction "+Name); SelectKind(MapDiff::EEntryKind::LAYER,"Game"); });
			Delay();
			Screenshot("comparison-" + std::to_string(Fixture) + "-tiles");
			Do([this] { SelectKind(MapDiff::EEntryKind::LAYER, "Added quad"); });
			Delay();
			if(Fixture == 0)
				Screenshot("comparison-quads");
			Click([] { return vec2(167, 16); }); // By group toolbar button.
			Do([this] { Check(Compare().*Access(TGroup{}),"By group button"); SelectKind(MapDiff::EEntryKind::GROUP,"Smoke"); });
			Delay();
			Screenshot("comparison-" + std::to_string(Fixture) + "-group");
			Click([] { return vec2(240, 16); });
			Do([this] { Check(Compare().*Access(TContext{}), "context toggle"); });
			Click([] { return vec2(106, 16); });
			WaitReady();
			Do([this] { Check((Compare().*Access(TSources{}))[0]==m_pAfter,"Swap button"); SelectKind(MapDiff::EEntryKind::GROUP,"Smoke"); });
			Delay();
			if(Fixture == 0)
				Screenshot("comparison-swapped");
			for(SDL_Keycode K : {SDLK_z, SDLK_y, SDLK_s, SDLK_v, SDLK_n, SDLK_o})
			{
				Do([this, K] { Key(SDLK_LCTRL,true); Key(K,true); });
				Delay(2);
				Do([this, K] { Key(K,false); Key(SDLK_LCTRL,false); });
				Delay(2);
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
				Click([this] { return vec2(Ui()->Screen()->w - 90, Ui()->Screen()->h - 21); });
				WaitReady();
				Do([this] { Check(Result().ChangeCount()>5,"pair selector compare button"); Graphics()->Resize(1100,700,0); OnWindowResize(); });
				Delay();
				Screenshot("comparison-resized");
				Do([this] { CloseMap(Maps().size()-1,false); Check(State()==5,"source close invalidates"); });
				Delay();
				Screenshot("comparison-source-closed");
				Do([this] { Compare().Close(); });
				Delay();
				Do([this] { Compare().Open(*this,Maps()[0].get(),m_pBefore); Check(HandleMapDrop("maps/coverage.map",IStorage::TYPE_ALL),"external map drop loaded"); Check(State()==5,"external load invalidates"); Compare().Close(); });
				Delay();
			}
		}
		Do([this] { m_Log<<"RESULT failures="<<m_Failures<<" steps="<<m_vSteps.size()<<std::endl; Client()->Quit(); });
	}

public:
	void OnUpdate() override { CEditor::OnUpdate(); }
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
