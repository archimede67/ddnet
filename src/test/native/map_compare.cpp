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
using TPaneArray = CUIRect[2];
ACCESSOR(TPanes, CMapCompare, m_aPanes, TPaneArray)
struct TFit
{
	using Type = void (CMapCompare::*)();
	friend Type Access(TFit);
};
template struct CAccess<TFit, &CMapCompare::FitSelection>;
bool HasSnapshot(CMapCompare &);
template<auto Member>
struct CDataAccess
{
	friend bool HasSnapshot(CMapCompare &Obj) { return static_cast<bool>(Obj.*Member); }
};
template struct CDataAccess<&CMapCompare::m_pData>;

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
	void CheckPixels()
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
			const vec2 Point = Pane.Center() + (vec2(180 * 32 + 16, 190 * 32 + 16) - Projection) / Zoom;
			Check(Pane.Inside(Point), "zero-parallax fit reveals distant tile side " + std::to_string(Side));
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
	void Setup()
	{
		Delay(10);
		RegressionSteps();
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
		CEditor::OnRender();
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
