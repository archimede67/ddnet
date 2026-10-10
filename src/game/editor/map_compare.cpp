#include "map_compare.h"

#include "editor.h"
#include "map_compare_render.h"
#include "map_diff_snapshot.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/keys.h>
#include <engine/textrender.h>

#include <game/client/lineinput.h>
#include <game/editor/mapitems/envelope.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <tuple>
#include <unordered_map>

namespace
{
	constexpr ColorRGBA REMOVE_COLOR(1.0f, 0.3f, 0.3f, 1.0f);
	constexpr ColorRGBA ADD_COLOR(0.25f, 1.0f, 0.45f, 1.0f);
	constexpr ColorRGBA CHANGE_COLOR(1.0f, 0.78f, 0.35f, 1.0f);
	constexpr float MIN_ZOOM = 0.05f;
	// Match the editor's 2000% limit. Drawing an entire huge map as subpixel
	// tiles costs millions of submissions without providing useful detail.
	constexpr float MAX_ZOOM = 20.0f;
	constexpr float MAP_CONTEXT_ALPHA = 0.25f;
	constexpr float SELECTED_CONTEXT_ALPHA = 0.65f;
	class CFixedEnvelopeEval : public IEnvelopeEval
	{
		const CEditorMap &m_Map;

	public:
		explicit CFixedEnvelopeEval(const CEditorMap &Map) :
			m_Map(Map) {}
		void EnvelopeEval(int Offset, int Index, ColorRGBA &Result, size_t Channels) const override
		{
			if(Index >= 0 && Index < (int)m_Map.m_vpEnvelopes.size())
			{
				const CEnvelope &Envelope = *m_Map.m_vpEnvelopes[Index];
				Envelope.Eval(Offset / 1000.0f, Result, std::min(Channels, (size_t)Envelope.GetChannels()));
			}
		}
	};
	void Label(CEditor &Editor, CUIRect Rect, const std::string &Text, float Size = 11.0f, ColorRGBA Color = ColorRGBA(1, 1, 1, 1))
	{
		SLabelProperties Props;
		Props.m_EllipsisAtEnd = true;
		Props.m_MaxWidth = Rect.w;
		Props.m_EnableWidthCheck = false;
		Props.SetColor(Color);
		Editor.Ui()->DoLabel(&Rect, Text.c_str(), Size, TEXTALIGN_ML, Props);
	}
	std::string SourceName(const CEditorMap *pMap)
	{
		if(!pMap)
			return "Unavailable";
		return std::string(pMap->m_aDisplayName) + (pMap->m_Modified ? " * (unsaved changes)" : pMap->m_aFilename[0] == '\0' ? " (unsaved map)" :
																       " (saved)");
	}
	const MapDiff::CLayer *Layer(const MapDiff::CSnapshot &Snapshot, const MapDiff::CEntry &Entry, int Side)
	{
		int Group = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
		int Index = Side == 0 ? Entry.m_Before : Entry.m_After;
		if(Entry.m_Kind != MapDiff::EEntryKind::LAYER || Group < 0 || Index < 0)
			return nullptr;
		return &Snapshot.m_vGroups[Group].m_vLayers[Index];
	}
	std::array<float, 4> ObjectBounds(const CEditorMap &Map, const ::CLayer &Layer, int Index, std::array<float, 4> Bounds)
	{
		CFixedEnvelopeEval Eval(Map);
		ColorRGBA Offset(0, 0, 0, 0);
		if(Layer.m_Type == LAYERTYPE_QUADS)
		{
			const auto &Quad = static_cast<const CLayerQuads &>(Layer).m_vQuads[Index];
			Eval.EnvelopeEval(Quad.m_PosEnvOffset, Quad.m_PosEnv, Offset, 3);
			vec2 Center(Quad.m_aPoints[4].x / 1024.0f, Quad.m_aPoints[4].y / 1024.0f);
			const float Angle = Offset.b * pi / 180.0f;
			Bounds = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
			for(int p = 0; p < 4; ++p)
			{
				vec2 Point(Quad.m_aPoints[p].x / 1024.0f, Quad.m_aPoints[p].y / 1024.0f);
				vec2 Relative = Point - Center;
				Point = Center + vec2(Relative.x * std::cos(Angle) - Relative.y * std::sin(Angle), Relative.x * std::sin(Angle) + Relative.y * std::cos(Angle));
				Bounds[0] = std::min(Bounds[0], Point.x);
				Bounds[1] = std::min(Bounds[1], Point.y);
				Bounds[2] = std::max(Bounds[2], Point.x);
				Bounds[3] = std::max(Bounds[3], Point.y);
			}
		}
		else if(Layer.m_Type == LAYERTYPE_SOUNDS)
		{
			const auto &Source = static_cast<const CLayerSounds &>(Layer).m_vSources[Index];
			Eval.EnvelopeEval(Source.m_PosEnvOffset, Source.m_PosEnv, Offset, 2);
		}
		Bounds[0] += Offset.r;
		Bounds[1] += Offset.g;
		Bounds[2] += Offset.r;
		Bounds[3] += Offset.g;
		return Bounds;
	}
	std::string SourceIndices(const MapDiff::CEntry &Entry)
	{
		return "Before #" + (Entry.m_Before < 0 ? std::string("absent") : std::to_string(Entry.m_Before)) + " / After #" +
		       (Entry.m_After < 0 ? std::string("absent") : std::to_string(Entry.m_After));
	}
	std::string EntryName(const MapDiff::CEntry &Entry)
	{
		if(!Entry.m_Name.empty() && Entry.m_Name != "(unnamed)")
			return Entry.m_Name;
		const auto Index = [](int Value) { return Value < 0 ? std::string("-") : std::to_string(Value); };
		return "#" + Index(Entry.m_Before < 0 ? Entry.m_After : Entry.m_Before) +
		       (Entry.m_Before >= 0 && Entry.m_After >= 0 && Entry.m_Before != Entry.m_After ? " -> #" + Index(Entry.m_After) : "") + " (unnamed)";
	}
	std::string EntryLabel(const MapDiff::CEntry &Entry)
	{
		const char *pKind = "";
		switch(Entry.m_Kind)
		{
		case MapDiff::EEntryKind::IMAGE: pKind = "Image: "; break;
		case MapDiff::EEntryKind::SOUND: pKind = "Sound: "; break;
		case MapDiff::EEntryKind::ENVELOPE: pKind = "Envelope: "; break;
		case MapDiff::EEntryKind::GROUP: pKind = "Group: "; break;
		default: break;
		}
		return std::string(Entry.Status()) + " " + pKind + EntryName(Entry) + (Entry.m_Ambiguous ? " [?]" : "");
	}
	template<typename TCells, typename F>
	void ForVisibleCells(const TCells &Cells, const CScreenRect &Visible, F Visit)
	{
		const int MinX = (int)std::floor(Visible.m_TopLeft.x / 32);
		const int MaxX = (int)std::floor(Visible.m_BottomRight.x / 32);
		const int MinY = (int)std::floor(Visible.m_TopLeft.y / 32);
		const int MaxY = (int)std::floor(Visible.m_BottomRight.y / 32);
		MapCompareRender::ForVisibleCells(Cells, MinX, MinY, MaxX, MaxY, Visit);
	}
	std::array<vec2, 4> TileUV(const MapDiff::CTile &Tile)
	{
		std::array<vec2, 4> UV = {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)};
		for(auto &Point : UV)
		{
			if(Tile.m_Flags & TILEFLAG_XFLIP)
				Point.x = 1 - Point.x;
			if(Tile.m_Flags & TILEFLAG_YFLIP)
				Point.y = 1 - Point.y;
		}
		if(Tile.m_Flags & TILEFLAG_ROTATE)
			std::rotate(UV.begin(), UV.end() - 1, UV.end());
		return UV;
	}
	// Tile rendering uses canonical sparse cells, so stale skip/opaque flags and
	// editor render toggles cannot hide current unsaved content.
	void DrawTiles(CEditor &Editor, const std::vector<MapDiff::CCell> &Cells, const CScreenRect &Visible, ColorRGBA Color)
	{
		auto *pGraphics = Editor.Graphics();
		bool Arrays = pGraphics->HasTextureArraysSupport();
		if(Arrays)
			pGraphics->QuadsTex3DBegin();
		else
			pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		ForVisibleCells(Cells, Visible, [&](const auto &Cell) {
			if(!Cell.m_Tile.m_Index)
				return;
			const auto &T = Cell.m_Tile;
			const float X = (T.m_Index % 16) / 16.0f, Y = (T.m_Index / 16) / 16.0f;
			auto aUV = TileUV(T);
			if(!Arrays)
				for(auto &UV : aUV)
					UV = vec2(X, Y) + vec2(0.0005f, 0.0005f) + UV * (1.0f / 16 - 0.001f);
			pGraphics->QuadsSetSubsetFree(aUV[0].x, aUV[0].y, aUV[1].x, aUV[1].y, aUV[2].x, aUV[2].y, aUV[3].x, aUV[3].y, Arrays ? T.m_Index : -1);
			IGraphics::CQuadItem Quad(Cell.m_X * 32.0f, Cell.m_Y * 32.0f, 32, 32);
			if(Arrays)
				pGraphics->QuadsTex3DDrawTL(&Quad, 1);
			else
				pGraphics->QuadsDrawTL(&Quad, 1);
		});
		if(Arrays)
			pGraphics->QuadsTex3DEnd();
		else
			pGraphics->QuadsEnd();
	}
	void MarkerLines(CEditor &Editor, float X, float Y, float W, float H, int Side, float Zoom)
	{
		W = std::max(W, 2.0f * Zoom);
		H = std::max(H, 2.0f * Zoom);
		const float Size = std::min(10.0f * Zoom, 10.0f);
		IGraphics::CLineItem aLines[] = {
			{X, Y, X + W, Y}, {X + W, Y, X + W, Y + H},
			{X + W, Y + H, X, Y + H}, {X, Y + H, X, Y},
			{X + 2, Y + 2 + Size / 2, X + 2 + Size, Y + 2 + Size / 2},
			{X + 2 + Size / 2, Y + 2, X + 2 + Size / 2, Y + 2 + Size}};
		Editor.Graphics()->LinesDraw(aLines, Side == 0 ? 5 : 6);
	}
	void Marker(CEditor &Editor, float X, float Y, float W, float H, int Side, float Zoom, ColorRGBA Color)
	{
		Editor.Graphics()->TextureClear();
		Editor.Graphics()->LinesBegin();
		Editor.Graphics()->SetColor(Color);
		MarkerLines(Editor, X, Y, W, H, Side, Zoom);
		Editor.Graphics()->LinesEnd();
	}
}

// Only viewport geometry is uploaded, never an entire large map. A small margin
// lets linked panning reuse the buffers; uniforms provide each layer's styling.
struct CMapCompare::CRenderCache
{
	struct CTileBuffer
	{
		int m_Container = -1;
		CScreenRect m_Bounds{vec2(0, 0), vec2(0, 0)};
		unsigned int m_Indices = 0;
		bool m_Valid = false;
	};
	IGraphics *m_pGraphics;
	std::unordered_map<const MapDiff::CLayer *, CTileBuffer> m_Tiles;
	struct CObjectBuffer
	{
		std::vector<std::array<float, 4>> m_vBounds;
		std::vector<std::array<float, 4>> m_vBlocks;
	};
	std::unordered_map<const MapDiff::CLayer *, CObjectBuffer> m_Objects;
	explicit CRenderCache(IGraphics *pGraphics) :
		m_pGraphics(pGraphics) {}
	~CRenderCache()
	{
		for(auto &[pLayer, Buffer] : m_Tiles)
			m_pGraphics->DeleteBufferContainer(Buffer.m_Container);
	}
	template<typename F>
	void VisibleObjects(const CEditorMap &Map, const CLayer &Source, const MapDiff::CLayer &Layer, const CScreenRect &Visible, F Visit)
	{
		constexpr size_t BlockSize = 64;
		auto [It, Inserted] = m_Objects.try_emplace(&Layer);
		auto &Buffer = It->second;
		if(Inserted)
		{
			for(size_t i = 0; i < Layer.m_vObjects.size(); ++i)
			{
				const auto Bounds = ObjectBounds(Map, Source, i, Layer.m_vObjects[i].m_aBounds);
				Buffer.m_vBounds.push_back(Bounds);
				if(i % BlockSize == 0)
					Buffer.m_vBlocks.push_back(Bounds);
				else
				{
					auto &Block = Buffer.m_vBlocks.back();
					Block = {std::min(Block[0], Bounds[0]), std::min(Block[1], Bounds[1]), std::max(Block[2], Bounds[2]), std::max(Block[3], Bounds[3])};
				}
			}
		}
		auto VisibleBounds = [&](const auto &B) { return B[2] >= Visible.m_TopLeft.x && B[3] >= Visible.m_TopLeft.y && B[0] <= Visible.m_BottomRight.x && B[1] <= Visible.m_BottomRight.y; };
		for(size_t Block = 0; Block < Buffer.m_vBlocks.size(); ++Block)
			if(VisibleBounds(Buffer.m_vBlocks[Block]))
				for(size_t i = Block * BlockSize; i < std::min((Block + 1) * BlockSize, Buffer.m_vBounds.size()); ++i)
					if(VisibleBounds(Buffer.m_vBounds[i]))
						Visit(i, Buffer.m_vBounds[i]);
	}
	void Draw(CEditor &Editor, const MapDiff::CLayer &Layer, const CScreenRect &Visible, ColorRGBA Color, bool Textured)
	{
		// Untextured tile pipelines use a different vertex layout on Vulkan.
		if(!Textured || !m_pGraphics->IsTileBufferingEnabled() || !m_pGraphics->HasTextureArraysSupport())
		{
			DrawTiles(Editor, Layer.m_vCells, Visible, Color);
			return;
		}
		if(Layer.m_vCells.empty() || Visible.m_BottomRight.x < 0 || Visible.m_BottomRight.y < 0 || Visible.m_TopLeft.x >= Layer.m_Width * 32.0f || Visible.m_TopLeft.y >= Layer.m_Height * 32.0f)
			return;
		auto &Buffer = m_Tiles[&Layer];
		const vec2 Min(std::max(Visible.m_TopLeft.x, 0.0f), std::max(Visible.m_TopLeft.y, 0.0f));
		const vec2 Max(std::min(Visible.m_BottomRight.x, Layer.m_Width * 32.0f), std::min(Visible.m_BottomRight.y, Layer.m_Height * 32.0f));
		constexpr float Margin = 16 * 32;
		if(!Buffer.m_Valid || Min.x < Buffer.m_Bounds.m_TopLeft.x || Min.y < Buffer.m_Bounds.m_TopLeft.y || Max.x > Buffer.m_Bounds.m_BottomRight.x || Max.y > Buffer.m_Bounds.m_BottomRight.y || Buffer.m_Bounds.Size().x > Max.x - Min.x + 4 * Margin || Buffer.m_Bounds.Size().y > Max.y - Min.y + 4 * Margin)
		{
			m_pGraphics->DeleteBufferContainer(Buffer.m_Container);
			Buffer.m_Bounds = CScreenRect(vec2(std::max(Min.x - Margin, 0.0f), std::max(Min.y - Margin, 0.0f)), vec2(std::min(Max.x + Margin, Layer.m_Width * 32.0f), std::min(Max.y + Margin, Layer.m_Height * 32.0f)));
			struct CVertex
			{
				vec2 m_Pos;
				ubvec4 m_Tex;
			};
			std::vector<CVertex> vVertices;
			ForVisibleCells(Layer.m_vCells, Buffer.m_Bounds, [&](const auto &Cell) {
				if(!Cell.m_Tile.m_Index)
					return;
				const auto UV = TileUV(Cell.m_Tile);
				const vec2 Pos(Cell.m_X * 32.0f, Cell.m_Y * 32.0f);
				const vec2 aCorners[] = {vec2(0, 0), vec2(32, 0), vec2(32, 32), vec2(0, 32)};
				for(int p = 0; p < 4; ++p)
					vVertices.push_back({Pos + aCorners[p], ubvec4((unsigned char)UV[p].x, (unsigned char)UV[p].y, Cell.m_Tile.m_Index, 0)});
			});
			Buffer.m_Indices = vVertices.size() / 4 * 6;
			if(!vVertices.empty())
			{
				SBufferContainerInfo Info;
				Info.m_Stride = sizeof(CVertex);
				Info.m_VertBufferBindingIndex = m_pGraphics->CreateBufferObject(vVertices.size() * sizeof(CVertex), vVertices.data(), 0);
				Info.m_vAttributes = {{2, GRAPHICS_TYPE_FLOAT, false, nullptr, 0}, {4, GRAPHICS_TYPE_UNSIGNED_BYTE, false, (void *)offsetof(CVertex, m_Tex), 1}};
				Buffer.m_Container = m_pGraphics->CreateBufferContainer(&Info);
				m_pGraphics->IndicesNumRequiredNotify(Buffer.m_Indices);
			}
			Buffer.m_Valid = true;
		}
		if(Buffer.m_Indices)
		{
			char *pOffset = nullptr;
			m_pGraphics->RenderTileLayer(Buffer.m_Container, Color, &pOffset, &Buffer.m_Indices, 1);
		}
	}
};

struct CMapCompare::CObjectViewCache
{
	float m_SettingsWidth = -1;
	std::vector<float> m_vSettingOffsets;
	std::vector<int> m_vPropertyRows;
	bool m_EnvelopeReady = false;
	float m_MinTime = 0, m_MaxTime = 1, m_MinValue = 0, m_MaxValue = 1;
	struct CEnvelopePlot
	{
		std::vector<vec2> m_avCurves[4];
		std::vector<int> m_vMarkers;
		std::vector<bool> m_vChanged;
	};
	CEnvelopePlot m_aPlots[2];
};

CMapCompare::CMapCompare() = default;

CMapCompare::~CMapCompare()
{
	if(m_pData)
		m_pData->m_Cancel = true;
}
void CMapCompare::CancelWork()
{
	if(m_pData)
		m_pData->m_Cancel = true;
	if(m_pOwner)
		m_pOwner->Retire(*this);
}
void CMapCompare::ReleaseBuffers()
{
	m_pRenderCache.reset();
	m_pObjectViewCache.reset();
	m_Settings.Reset();
	m_ScrollToSetting = false;
	m_FitAfterSettings = false;
	m_pData.reset();
	m_Result = {};
	std::vector<CDetail>().swap(m_vDetails);
	std::vector<MapDiff::CPropertyChange>().swap(m_vValueRows);
	std::vector<int>().swap(m_vNavigator);
	std::vector<int>().swap(m_vChangedNavigator);
	std::vector<CLayerState>().swap(m_vLayerStates);
	m_LayerTree.Reset();
	m_ScrollToLayer = false;
	m_UnchangedCount = 0;
	m_SelectedEntry = m_SelectedDetail = m_FocusEntry = -1;
	m_ValueRowsEntry = m_ValueRowsDetail = -2;
}
vec2 CMapCompare::ProjectionCenter(const CLayerGroup &Group, bool Inspection) const
{
	// The selected owning group retains an independent inspection translation,
	// including its siblings. Other map context follows the current camera.
	if(!Inspection && m_Context == CONTEXT_MAP && m_SelectedEntry >= 0)
	{
		const auto &Selected = m_Result.m_vEntries[m_SelectedEntry];
		for(int Side = 0; Side < 2; ++Side)
		{
			const int G = Side == 0 ? Selected.m_BeforeGroup : Selected.m_AfterGroup;
			if(G >= 0 && m_apSources[Side] && m_apSources[Side]->m_vpGroups[G].get() == &Group)
				Inspection = true;
		}
		if(!Inspection)
			return vec2((m_ParallaxOrigin.x + m_Center.x) * Group.m_ParallaxX / 100.0f + Group.m_OffsetX,
				(m_ParallaxOrigin.y + m_Center.y) * Group.m_ParallaxY / 100.0f + Group.m_OffsetY);
	}
	return m_Center + vec2(m_ParallaxOrigin.x * Group.m_ParallaxX / 100.0f + Group.m_OffsetX, m_ParallaxOrigin.y * Group.m_ParallaxY / 100.0f + Group.m_OffsetY);
}
bool CMapCompare::ValidSources(const CEditor &Editor) const
{
	if(!m_apSources[0] || !m_apSources[1] || m_apSources[0] == m_apSources[1])
		return false;
	for(const CEditorMap *pSource : m_apSources)
		if(std::none_of(Editor.m_vpMaps.begin(), Editor.m_vpMaps.end(), [&](const auto &pMap) { return pMap.get() == pSource; }))
			return false;
	return true;
}
void CMapCompare::OpenSelector(CEditor &Editor)
{
	if(Editor.m_vpMaps.size() < 2)
		return;
	CancelWork();
	ReleaseBuffers();
	m_apSources[0] = Editor.Map();
	m_apSources[1] = nullptr;
	for(const auto &pMap : Editor.m_vpMaps)
		if(pMap.get() != m_apSources[0])
		{
			m_apSources[1] = pMap.get();
			break;
		}
	m_State = EState::SELECT;
	m_Selector = true;
	m_Message.clear();
}
void CMapCompare::Open(CEditor &Editor, const CEditorMap *pBefore, const CEditorMap *pAfter)
{
	CancelWork();
	ReleaseBuffers();
	m_State = EState::CLOSED;
	m_apSources[0] = pBefore;
	m_apSources[1] = pAfter;
	if(!ValidSources(Editor))
		return;
	m_State = EState::CAPTURE;
	m_ParallaxOrigin = pBefore->m_MapViewState.m_WorldOffset;
	m_Center = vec2(0, 0);
	m_Message.clear();
	m_Context = CONTEXT_OFF;
	m_UnchangedExpanded = false;
	m_Section = SECTION_CHANGES;
	m_LayerTreeActive = true;
	m_Panning = false;
}
void CMapCompare::Invalidate(const char *pReason)
{
	CancelWork();
	ReleaseBuffers();
	m_Message = pReason;
	m_State = EState::UNAVAILABLE;
}
void CMapCompare::Close()
{
	SaveView();
	CancelWork();
	ReleaseBuffers();
	m_State = EState::CLOSING;
}
void CMapCompare::OnInput(CEditor &Editor, const IInput::CEvent &Event)
{
	// Dialog input is handled by the shared shell before navigation reaches us.
	if(Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen() && !Editor.Input()->ModifierIsPressed() && !Editor.Input()->AltIsPressed() && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			Editor.m_MapCompare.SelectMap(Editor, Editor.Map());
			return;
		}
		if(m_State == EState::READY && Current(Editor))
		{
			if(Event.m_Key == KEY_N || Event.m_Key == KEY_P)
			{
				Navigate(Event.m_Key == KEY_N ? 1 : -1);
				return;
			}
			if(Event.m_Key == KEY_HOME)
			{
				FitSelection();
				return;
			}
			vec2 Mouse(Editor.Ui()->MouseX(), Editor.Ui()->MouseY());
			if(MapView() && Editor.Ui()->ActiveItem() == nullptr && (m_aPanes[0].Inside(Mouse) || m_aPanes[1].Inside(Mouse)))
			{
				if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
					m_Zoom = std::max(MIN_ZOOM, m_Zoom / 1.2f);
				if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
					m_Zoom = std::min(MAX_ZOOM, m_Zoom * 1.2f);
				if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
					return;
			}
		}
	}
	if(!(Event.m_Flags & IInput::FLAG_TEXT))
		Editor.Ui()->OnInput(Event);
}
bool CMapCompare::Current(const CEditor &Editor) const
{
	return ValidSources(Editor) && m_aRevisions[0] == m_apSources[0]->m_ContentRevision && m_aRevisions[1] == m_apSources[1]->m_ContentRevision;
}

CMapCompare::CBookmark CMapCompare::Bookmark(const MapDiff::CEntry &Entry) const
{
	CBookmark Result;
	Result.m_Present = true;
	Result.m_Kind = Entry.m_Kind;
	for(int Side = 0; Side < 2; ++Side)
	{
		const auto *pMap = m_apSources[Side];
		const int Index = Side == 0 ? Entry.m_Before : Entry.m_After;
		const int Group = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
		if(Index < 0)
			continue;
		switch(Entry.m_Kind)
		{
		case MapDiff::EEntryKind::GROUP: Result.m_apObjects[Side] = pMap->m_vpGroups[Index]; break;
		case MapDiff::EEntryKind::LAYER: Result.m_apObjects[Side] = pMap->m_vpGroups[Group]->m_vpLayers[Index]; break;
		case MapDiff::EEntryKind::IMAGE: Result.m_apObjects[Side] = pMap->m_vpImages[Index]; break;
		case MapDiff::EEntryKind::SOUND: Result.m_apObjects[Side] = pMap->m_vpSounds[Index]; break;
		case MapDiff::EEntryKind::ENVELOPE: Result.m_apObjects[Side] = pMap->m_vpEnvelopes[Index]; break;
		default: break;
		}
	}
	return Result;
}

CMapCompare::CDetailBookmark CMapCompare::DetailBookmark(int Index) const
{
	CDetailBookmark Saved;
	if(Index < 0 || Index >= (int)m_vDetails.size())
		return Saved;
	const auto &Detail = m_vDetails[Index];
	const auto &Entry = m_Result.m_vEntries[Detail.m_Entry];
	Saved.m_Entry = m_vBookmarks[Detail.m_Entry];
	if(Detail.m_Kind == 1)
	{
		Saved.m_Kind = 1;
		Saved.m_TileX = Entry.m_vTiles[Detail.m_Index].m_X;
		Saved.m_TileY = Entry.m_vTiles[Detail.m_Index].m_Y;
	}
	else if(Detail.m_Kind == 0 && Entry.m_Kind != MapDiff::EEntryKind::SETTINGS && Entry.m_Kind != MapDiff::EEntryKind::ENVELOPE)
	{
		Saved.m_Kind = 0;
		Saved.m_Property = Entry.m_vProperties[Detail.m_Index].m_Name;
	}
	else if(Detail.m_Kind == 4)
		Saved.m_Kind = 4;
	return Saved;
}

void CMapCompare::SaveView()
{
	if(m_State != EState::READY)
		return;
	auto Get = [&](int Index) { return Index >= 0 && Index < (int)m_vBookmarks.size() ? m_vBookmarks[Index] : CBookmark{}; };
	m_SavedView.m_Selection = Get(m_SelectedEntry);
	m_SavedView.m_Focus = Get(m_FocusEntry);
	m_SavedView.m_Detail = DetailBookmark(m_SelectedDetail);
	const int SelectedEntry = m_SelectedEntry, SelectedDetail = m_SelectedDetail;
	const bool ByGroup = m_ByGroup;
	for(int i = 0; i < NUM_SECTIONS; ++i)
	{
		const CSectionState State = i == m_Section ? CSectionState{SelectedEntry, SelectedDetail, m_Center, m_Zoom, ByGroup} : m_aSectionStates[i];
		m_SavedView.m_aSections[i] = Get(State.m_Entry);
		m_SavedView.m_aStates[i] = State;
		m_SavedView.m_aNavigators[i] = m_aNavigators[i];
		m_SelectedEntry = State.m_Entry;
		m_ByGroup = State.m_ByGroup;
		RebuildDetails();
		m_SavedView.m_aSectionDetails[i] = DetailBookmark(State.m_Detail);
	}
	m_SelectedEntry = SelectedEntry;
	m_ByGroup = ByGroup;
	RebuildDetails();
	m_SelectedDetail = SelectedDetail;
	m_SavedView.m_Settings = m_Settings;
	m_SavedView.m_Layers = m_LayerTree;
	m_SavedView.m_Values = m_Values;
	m_SavedView.m_Details = m_Details;
	m_SavedView.m_vLayers.clear();
	for(size_t i = 0; i < m_vLayerStates.size(); ++i)
		if(m_vBookmarks[i].m_Kind == MapDiff::EEntryKind::GROUP || m_vBookmarks[i].m_Kind == MapDiff::EEntryKind::LAYER)
			m_SavedView.m_vLayers.push_back({m_vBookmarks[i], {m_vLayerStates[i].m_Visible, m_vLayerStates[i].m_Expanded}});
}

void CMapCompare::RestoreView()
{
	auto Find = [&](const CBookmark &Saved) {
		if(!Saved.m_Present)
			return -1;
		for(int i = 0; i < (int)m_vBookmarks.size(); ++i)
		{
			const auto &New = m_vBookmarks[i];
			if(New.m_Kind != Saved.m_Kind)
				continue;
			if(New.m_Kind == MapDiff::EEntryKind::INFO || New.m_Kind == MapDiff::EEntryKind::SETTINGS)
				return i;
			bool Match = false, Conflict = false;
			for(int Side = 0; Side < 2; ++Side)
			{
				auto OldObject = Saved.m_apObjects[Side].lock();
				auto NewObject = New.m_apObjects[Side].lock();
				if(OldObject && NewObject)
				{
					Match |= OldObject == NewObject;
					Conflict |= OldObject != NewObject;
				}
			}
			if(Match && !Conflict)
				return i;
		}
		return -1;
	};
	for(const auto &Layer : m_SavedView.m_vLayers)
	{
		int Index = Find(Layer.first);
		if(Index >= 0)
		{
			m_vLayerStates[Index].m_Visible = Layer.second.first;
			m_vLayerStates[Index].m_Expanded = Layer.second.second;
		}
	}
	auto ResolveDetail = [&](const CDetailBookmark &Saved) {
		const int DetailEntry = Find(Saved.m_Entry);
		for(int i = 0; i < (int)m_vDetails.size(); ++i)
		{
			const auto &Detail = m_vDetails[i];
			if(Detail.m_Entry != DetailEntry || Detail.m_Kind != Saved.m_Kind)
				continue;
			const auto &Entry = m_Result.m_vEntries[Detail.m_Entry];
			if(Detail.m_Kind == 4 ||
				(Detail.m_Kind == 0 && Entry.m_vProperties[Detail.m_Index].m_Name == Saved.m_Property) ||
				(Detail.m_Kind == 1 && Entry.m_vTiles[Detail.m_Index].m_X == Saved.m_TileX && Entry.m_vTiles[Detail.m_Index].m_Y == Saved.m_TileY))
				return i;
		}
		return -1;
	};
	const bool ByGroup = m_ByGroup;
	for(int i = 0; i < NUM_SECTIONS; ++i)
	{
		m_aSectionStates[i] = m_SavedView.m_aStates[i];
		m_aNavigators[i] = m_SavedView.m_aNavigators[i];
		m_aSectionStates[i].m_Entry = Find(m_SavedView.m_aSections[i]);
		m_SelectedEntry = m_aSectionStates[i].m_Entry;
		m_ByGroup = m_aSectionStates[i].m_ByGroup;
		RebuildDetails();
		m_aSectionStates[i].m_Detail = ResolveDetail(m_SavedView.m_aSectionDetails[i]);
	}
	m_ByGroup = ByGroup;
	m_SelectedEntry = Find(m_SavedView.m_Selection);
	m_FocusEntry = Find(m_SavedView.m_Focus);
	m_SelectedDetail = -1;
	RebuildNavigator(true);
	// A lost object must not fall through to another object at its former index.
	m_SelectedEntry = Find(m_SavedView.m_Selection);
	RebuildDetails();
	m_SelectedDetail = -1;
	m_SelectedDetail = ResolveDetail(m_SavedView.m_Detail);
	m_Settings = m_SavedView.m_Settings;
	m_LayerTree = m_SavedView.m_Layers;
	m_Values = m_SavedView.m_Values;
	m_Details = m_SavedView.m_Details;
	m_ScrollToSetting = m_ScrollToLayer = false;
}

void CMapCompare::Start(CEditor &Editor)
{
	if(!ValidSources(Editor))
	{
		Invalidate("A comparison source is no longer open.");
		return;
	}
	SaveView();
	// Caches contain snapshot addresses: destroy them before replacing snapshots.
	m_pRenderCache.reset();
	m_pObjectViewCache.reset();
	m_pData = std::make_shared<CData>();
	for(int Side = 0; Side < 2; ++Side)
	{
		m_aRevisions[Side] = m_apSources[Side]->m_ContentRevision;
		MapDiff::Capture(*m_apSources[Side], m_pData->m_aSnapshots[Side]);
	}
	const auto pData = m_pData;
	m_Future = std::async(std::launch::async, [pData] {
		return MapDiff::Compare(pData->m_aSnapshots[0], pData->m_aSnapshots[1], [pData] { return pData->m_Cancel.load(); });
	});
	m_State = EState::COMPUTING;
	m_vDetails.clear();
	m_vValueRows.clear();
	m_ValueRowsEntry = m_ValueRowsDetail = -2;
}

void CMapCompare::OnUpdate(CEditor &Editor)
{
	if((m_State == EState::COMPUTING || m_State == EState::READY) && !Current(Editor))
	{
		SaveView();
		CancelWork();
		m_State = EState::CAPTURE;
		return;
	}
	if(m_State == EState::CAPTURE)
	{
		try
		{
			Start(Editor);
		}
		catch(const std::exception &Exception)
		{
			Invalidate((std::string("Could not capture comparison: ") + Exception.what()).c_str());
		}
		return;
	}
	if(m_State == EState::COMPUTING && m_Future.valid() && m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		try
		{
			auto Result = m_Future.get();
			if(Result.m_Cancelled || !Current(Editor))
			{
				m_State = EState::CAPTURE;
				return;
			}
			m_Result = std::move(Result);
			m_vBookmarks.clear();
			m_vLayerStates.assign(m_Result.m_vEntries.size(), CLayerState{});
			for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
			{
				const auto &Entry = m_Result.m_vEntries[i];
				m_vBookmarks.push_back(Bookmark(Entry));
				m_vLayerStates[i].m_Changes += Entry.ChangeCount();
				if(Entry.m_Parent >= 0)
					m_vLayerStates[Entry.m_Parent].m_Changes += Entry.ChangeCount();
			}
			m_State = EState::READY;
			if(m_HasResult)
				RestoreView();
			else
			{
				RebuildNavigator();
				if(!m_vNavigator.empty() && m_vNavigator.front() >= 0)
					Select(m_vNavigator.front());
			}
			m_HasResult = true;
		}
		catch(const std::exception &Exception)
		{
			Invalidate((std::string("Comparison failed: ") + Exception.what()).c_str());
		}
	}
}
bool CMapCompare::InSection(const MapDiff::CEntry &Entry) const
{
	switch(m_Section)
	{
	case SECTION_ENVELOPES: return Entry.m_Kind == MapDiff::EEntryKind::ENVELOPE;
	case SECTION_SETTINGS: return Entry.m_Kind == MapDiff::EEntryKind::SETTINGS;
	case SECTION_LAYERS: return Entry.m_Kind == MapDiff::EEntryKind::GROUP || Entry.m_Kind == MapDiff::EEntryKind::LAYER;
	default: return Entry.m_Kind != MapDiff::EEntryKind::ENVELOPE && Entry.m_Kind != MapDiff::EEntryKind::SETTINGS;
	}
}
void CMapCompare::SetSection(int Section)
{
	if(Section == m_Section)
		return;
	m_aSectionStates[m_Section] = {m_SelectedEntry, m_SelectedDetail, m_Center, m_Zoom, m_ByGroup};
	const int PreviousEntry = m_SelectedEntry;
	const int PreviousDetail = m_SelectedDetail;
	const vec2 Center = m_Center;
	const float Zoom = m_Zoom;
	const bool LeavingSettings = PreviousEntry >= 0 && m_Result.m_vEntries[PreviousEntry].m_Kind == MapDiff::EEntryKind::SETTINGS;
	m_Section = Section;
	m_LayerTreeActive = true;
	auto Available = [&](int Entry) {
		return Entry >= 0 && Entry < (int)m_Result.m_vEntries.size() && InSection(m_Result.m_vEntries[Entry]);
	};
	const auto Saved = m_aSectionStates[Section];
	const bool RestoreSaved = Available(Saved.m_Entry);
	const int Entry = RestoreSaved ? Saved.m_Entry : Available(PreviousEntry) ? PreviousEntry :
										    -1;
	if(!RestoreSaved && Entry == PreviousEntry && Entry >= 0)
	{
		RebuildNavigator(true);
		return; // An unvisited destination inherits the current inspection.
	}
	if(Entry >= 0 && Entry == Saved.m_Entry && Section <= SECTION_LAYERS)
		m_ByGroup = Saved.m_ByGroup;
	RebuildNavigator(true);
	Select(Entry);
	if(Entry >= 0)
	{
		const int Detail = Entry == Saved.m_Entry ? Saved.m_Detail : PreviousDetail;
		if(Detail >= 0 && Detail < (int)m_vDetails.size())
		{
			m_SelectedDetail = Detail;
			m_FocusEntry = m_vDetails[Detail].m_Entry;
			m_Details.ScrollToSelected();
		}
	}
	else
	{
		RebuildNavigator();
		m_FitAfterSettings = LeavingSettings && MapView();
	}
	if(Entry >= 0 && Entry == Saved.m_Entry && MapView())
	{
		m_Center = Saved.m_Center;
		m_Zoom = Saved.m_Zoom;
		m_FitAfterSettings = false;
	}
	if(Entry >= 0 && Entry == Saved.m_Entry && Section == SECTION_SETTINGS)
		m_ScrollToSetting = false;
	// Resource sections preserve the independent map inspection camera.
	if(!MapView())
	{
		m_Center = Center;
		m_Zoom = Zoom;
	}
}
void CMapCompare::RebuildNavigator(bool KeepSelection)
{
	m_vNavigator.clear();
	m_vChangedNavigator.clear();
	std::vector<int> vUnchanged;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(!InSection(Entry))
			continue;
		const bool Changed = Entry.m_Kind == MapDiff::EEntryKind::GROUP && m_ByGroup ? m_vLayerStates[i].m_Changes > 0 : Entry.Changed();
		if(m_ByGroup && Entry.m_Kind == MapDiff::EEntryKind::LAYER)
			continue;
		// In layer mode retain changed group properties in the navigator, too.
		if(m_Section != SECTION_LAYERS && !m_ByGroup && Entry.m_Kind == MapDiff::EEntryKind::GROUP && !Entry.Changed())
			continue;
		(Changed ? m_vChangedNavigator : vUnchanged).push_back(i);
	}
	m_vNavigator = m_vChangedNavigator;
	m_UnchangedCount = vUnchanged.size();
	if(m_Section != SECTION_CHANGES)
		m_vNavigator.insert(m_vNavigator.end(), vUnchanged.begin(), vUnchanged.end());
	else if(m_Context != CONTEXT_OFF && !vUnchanged.empty())
	{
		m_vNavigator.push_back(-1); // Collapsible section header, never a selection.
		if(m_UnchangedExpanded)
			m_vNavigator.insert(m_vNavigator.end(), vUnchanged.begin(), vUnchanged.end());
	}
	if(!KeepSelection && (m_SelectedEntry < 0 || std::find(m_vNavigator.begin(), m_vNavigator.end(), m_SelectedEntry) == m_vNavigator.end()))
	{
		int Replacement = -1;
		if(std::find(m_vNavigator.begin(), m_vNavigator.end(), m_FocusEntry) != m_vNavigator.end())
			Replacement = m_FocusEntry;
		// Opening context on equal maps should immediately show map content.
		if(Replacement < 0 && m_Context == CONTEXT_SELECTION && m_vChangedNavigator.empty())
			for(int Index : m_vNavigator)
			{
				if(Index < 0)
					continue;
				const auto Kind = m_Result.m_vEntries[Index].m_Kind;
				if(Kind == MapDiff::EEntryKind::LAYER || (Kind == MapDiff::EEntryKind::GROUP &&
										 std::any_of(m_Result.m_vEntries.begin(), m_Result.m_vEntries.end(), [Index](const auto &Entry) { return Entry.m_Parent == Index; })))
				{
					Replacement = Index;
					break;
				}
			}
		if(Replacement < 0)
			for(int Index : m_vNavigator)
				if(Index >= 0)
				{
					Replacement = Index;
					break;
				}
		Select(Replacement);
	}
}
bool CMapCompare::LayerVisible(int Entry) const
{
	const int Parent = m_Result.m_vEntries[Entry].m_Parent;
	return m_vLayerStates[Entry].m_Visible && (Parent < 0 || m_vLayerStates[Parent].m_Visible);
}
void CMapCompare::RevealLayerSelection()
{
	if(m_FocusEntry < 0)
		return;
	const int Parent = m_Result.m_vEntries[m_FocusEntry].m_Parent;
	if(Parent >= 0)
		m_vLayerStates[Parent].m_Expanded = true;
	m_ScrollToLayer = true;
}
void CMapCompare::Select(int Entry)
{
	// Leaving the full-height settings view changes the available map height.
	// Fit once more after layout, using the new map pane dimensions.
	m_FitAfterSettings = m_SelectedEntry >= 0 && m_Result.m_vEntries[m_SelectedEntry].m_Kind == MapDiff::EEntryKind::SETTINGS && Entry >= 0 &&
			     (m_Result.m_vEntries[Entry].m_Kind == MapDiff::EEntryKind::LAYER || m_Result.m_vEntries[Entry].m_Kind == MapDiff::EEntryKind::GROUP);
	if(m_SelectedEntry != Entry)
	{
		m_pObjectViewCache.reset();
		m_ScrollToSetting = true;
	}
	m_Panning = false;
	m_SelectedEntry = m_FocusEntry = Entry;
	m_SelectedDetail = -1;
	m_Details.Reset();
	m_Values.Reset();
	m_ValueRowsEntry = m_ValueRowsDetail = -2;
	RebuildDetails();
	FitSelection();
}
bool CMapCompare::MapView() const
{
	return m_SelectedEntry >= 0 && (m_Result.m_vEntries[m_SelectedEntry].m_Kind == MapDiff::EEntryKind::LAYER || m_Result.m_vEntries[m_SelectedEntry].m_Kind == MapDiff::EEntryKind::GROUP);
}
void CMapCompare::SelectDetail(int Detail)
{
	if(Detail < 0 || Detail >= (int)m_vDetails.size())
		return;
	m_SelectedDetail = Detail;
	m_FocusEntry = m_vDetails[Detail].m_Entry;
	m_Values.Reset();
	m_Details.ScrollToSelected();
	NavigatorList().ScrollToSelected();
	m_ScrollToSetting = true;
	FitSelection();
	RevealLayerSelection();
}
void CMapCompare::RebuildDetails()
{
	m_vDetails.clear();
	if(m_SelectedEntry < 0)
		return;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(i != m_SelectedEntry && (!m_ByGroup || Entry.m_Parent != m_SelectedEntry))
			continue;
		if(Entry.m_Before < 0 || Entry.m_After < 0 || Entry.m_Reordered || Entry.m_Ambiguous)
			m_vDetails.push_back({i, 4, 0});
		for(int p = 0; p < (int)Entry.m_vProperties.size(); ++p)
			m_vDetails.push_back({i, 0, p});
		for(int t = 0; t < (int)Entry.m_vTiles.size(); ++t)
			m_vDetails.push_back({i, 1, t});
		for(int o = 0; o < (int)Entry.m_vObjects.size(); ++o)
			m_vDetails.push_back({i, 2, o});
		for(int e = 0; e < (int)Entry.m_vIssues.size(); ++e)
			m_vDetails.push_back({i, 3, e});
	}
	const auto &Selected = m_Result.m_vEntries[m_SelectedEntry];
	if(Selected.m_Kind != MapDiff::EEntryKind::INFO && Selected.m_Kind != MapDiff::EEntryKind::SETTINGS && Selected.m_Before >= 0 && Selected.m_After >= 0 && !Selected.m_Reordered && !Selected.m_Ambiguous)
		m_vDetails.push_back({m_SelectedEntry, 4, 0});
	if(!m_vDetails.empty())
		m_SelectedDetail = 0;
}
bool CMapCompare::IsDifference(const CDetail &Detail) const
{
	if(Detail.m_Kind != 4)
		return true;
	const auto &Entry = m_Result.m_vEntries[Detail.m_Entry];
	return Entry.m_Before < 0 || Entry.m_After < 0 || Entry.m_Reordered;
}
std::vector<MapDiff::CPropertyChange> CMapCompare::DetailValues(const CDetail &Detail) const
{
	const auto &Entry = m_Result.m_vEntries[Detail.m_Entry];
	if(Detail.m_Kind == 0)
		return {Entry.m_vProperties[Detail.m_Index]};
	if(Detail.m_Kind == 1)
	{
		const auto &Tile = Entry.m_vTiles[Detail.m_Index];
		const auto *pA = Layer(m_pData->m_aSnapshots[0], Entry, 0);
		const auto *pB = Layer(m_pData->m_aSnapshots[1], Entry, 1);
		auto Kind = pA ? pA->m_Kind : pB->m_Kind;
		return {{"Tile (" + std::to_string(Tile.m_X) + ", " + std::to_string(Tile.m_Y) + ")", MapDiff::TileDescription(Tile.m_Before, Kind), MapDiff::TileDescription(Tile.m_After, Kind)}};
	}
	if(Detail.m_Kind == 2)
	{
		const auto &Object = Entry.m_vObjects[Detail.m_Index];
		const auto *pA = Layer(m_pData->m_aSnapshots[0], Entry, 0);
		const auto *pB = Layer(m_pData->m_aSnapshots[1], Entry, 1);
		MapDiff::TProperties Empty;
		auto Values = MapDiff::CompareProperties(pA && Object.m_Before >= 0 ? pA->m_vObjects[Object.m_Before].m_vProperties : Empty, pB && Object.m_After >= 0 ? pB->m_vObjects[Object.m_After].m_vProperties : Empty);
		Values.insert(Values.begin(), {"Object (zero-based index)", Object.m_Before < 0 ? "(absent)" : std::to_string(Object.m_Before), Object.m_After < 0 ? "(absent)" : std::to_string(Object.m_After)});
		if(Object.m_Reordered)
			Values.push_back({"Draw order", "Previous relative order", "Changed relative order"});
		if(Object.m_Ambiguous)
			Values.push_back({"Matching", Object.m_Positional ? "Positional fallback in compatible ordered span" : "Ambiguous duplicate content match", "Historical identity is unknown"});
		return Values;
	}
	if(Detail.m_Kind == 3)
		return {{"Content unavailable / invalid reference", Entry.m_vIssues[Detail.m_Index], Entry.m_vIssues[Detail.m_Index]}};
	std::vector<MapDiff::CPropertyChange> Values{{"Structure (zero-based index)", Entry.m_Before < 0 ? "(absent)" : std::to_string(Entry.m_Before), Entry.m_After < 0 ? "(absent)" : std::to_string(Entry.m_After)}};
	if(Entry.m_Reordered)
		Values.push_back({"Draw order", "Previous relative order", "Changed relative order among matched items"});
	if(Entry.m_Ambiguous)
		Values.push_back({"Matching", Entry.m_Positional ? "Positional fallback in compatible ordered span" : "Ambiguous duplicate content match", "Historical identity is unknown"});
	else if(Entry.m_Before >= 0 && Entry.m_After >= 0)
		Values.push_back({"Matching", "Unique role, content or name match", "Source indices do not establish historical identity"});
	if(Entry.m_Kind == MapDiff::EEntryKind::LAYER)
		Values.push_back({"Group (zero-based index)", std::to_string(Entry.m_BeforeGroup), std::to_string(Entry.m_AfterGroup)});
	return Values;
}
void CMapCompare::Navigate(int Direction)
{
	if(m_vChangedNavigator.empty())
		return;
	auto Seek = [&](int Start) {
		for(int Detail = Start; Detail >= 0 && Detail < (int)m_vDetails.size(); Detail += Direction)
			if(IsDifference(m_vDetails[Detail]))
			{
				SelectDetail(Detail);
				return true;
			}
		return false;
	};
	if(Seek(m_SelectedDetail + Direction))
		return;
	auto It = std::find(m_vChangedNavigator.begin(), m_vChangedNavigator.end(), m_SelectedEntry);
	const int Count = m_vChangedNavigator.size();
	int Index = It == m_vChangedNavigator.end() ? (Direction > 0 ? -1 : 0) : It - m_vChangedNavigator.begin();
	for(int Visited = 0; Visited < Count; ++Visited)
	{
		Index = (Index + Direction + Count) % Count;
		Select(m_vChangedNavigator[Index]);
		if(Seek(Direction > 0 ? 0 : (int)m_vDetails.size() - 1))
			return;
	}
}
void CMapCompare::FitSelection()
{
	if(m_SelectedEntry < 0 || !m_pData)
		return;
	float Left = std::numeric_limits<float>::max(), Top = Left, Right = -Left, Bottom = -Left;
	vec2 ContentCenter;
	int ContentPriority = 0;
	auto Extend = [&](const MapDiff::CEntry &Entry, int Side, std::array<float, 4> Bounds, bool Content = true) {
		int G = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
		if(G < 0 || !m_apSources[Side])
			return false;
		const auto &Group = *m_apSources[Side]->m_vpGroups[G];
		const vec2 Origin = ProjectionCenter(Group, true) - m_Center;
		float X0 = Bounds[0] - Origin.x, X1 = Bounds[2] - Origin.x;
		float Y0 = Bounds[1] - Origin.y, Y1 = Bounds[3] - Origin.y;
		if(Group.m_UseClipping)
		{
			const auto &Map = *m_apSources[Side];
			const vec2 GameOrigin = Map.m_pGameGroup ? ProjectionCenter(*Map.m_pGameGroup, true) - m_Center : vec2(0, 0);
			X0 = std::max(X0, Group.m_ClipX - GameOrigin.x);
			Y0 = std::max(Y0, Group.m_ClipY - GameOrigin.y);
			X1 = std::min(X1, Group.m_ClipX + (float)Group.m_ClipW - GameOrigin.x);
			Y1 = std::min(Y1, Group.m_ClipY + (float)Group.m_ClipH - GameOrigin.y);
			if(X0 >= X1 || Y0 >= Y1)
				return false;
		}
		Left = std::min(Left, std::min(X0, X1));
		Top = std::min(Top, std::min(Y0, Y1));
		Right = std::max(Right, std::max(X0, X1));
		Bottom = std::max(Bottom, std::max(Y0, Y1));
		const int Priority = (Entry.Changed() ? 2 : 1) + (m_FocusEntry >= 0 && &Entry == &m_Result.m_vEntries[m_FocusEntry] ? 2 : 0);
		if(Content && Priority > ContentPriority)
		{
			ContentCenter = vec2((X0 + X1) / 2, (Y0 + Y1) / 2);
			ContentPriority = Priority;
		}
		return true;
	};
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(i != m_SelectedEntry && Entry.m_Parent != m_SelectedEntry)
			continue;
		const auto &Selected = m_Result.m_vEntries[m_SelectedEntry];
		if(!m_Context && !Entry.Changed() && !Selected.Changed())
			continue;
		if(m_SelectedDetail >= 0)
		{
			const auto &Detail = m_vDetails[m_SelectedDetail];
			if(IsDifference(Detail) && m_Result.m_vEntries[Detail.m_Entry].m_Kind == MapDiff::EEntryKind::LAYER && Detail.m_Entry != i)
				continue;
		}
		for(int Side = 0; Side < 2; ++Side)
		{
			const auto *pLayer = Layer(m_pData->m_aSnapshots[Side], Entry, Side);
			if(!pLayer)
				continue;
			const int G = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
			const int L = Side == 0 ? Entry.m_Before : Entry.m_After;
			const auto &SourceLayer = *m_apSources[Side]->m_vpGroups[G]->m_vpLayers[L];
			if(m_SelectedDetail >= 0 && m_vDetails[m_SelectedDetail].m_Entry == i)
			{
				const auto &Detail = m_vDetails[m_SelectedDetail];
				if(Detail.m_Kind == 1)
				{
					const auto &Tile = Entry.m_vTiles[Detail.m_Index];
					Extend(Entry, Side, {Tile.m_X * 32.0f - 96, Tile.m_Y * 32.0f - 96, Tile.m_X * 32.0f + 128, Tile.m_Y * 32.0f + 128});
					continue;
				}
				if(Detail.m_Kind == 2)
				{
					const auto &Object = Entry.m_vObjects[Detail.m_Index];
					int O = Side == 0 ? Object.m_Before : Object.m_After;
					if(O >= 0)
						Extend(Entry, Side, ObjectBounds(*m_apSources[Side], SourceLayer, O, pLayer->m_vObjects[O].m_aBounds));
					continue;
				}
			}
			if(pLayer->m_Width > 0)
			{
				Extend(Entry, Side, {0, 0, pLayer->m_Width * 32.0f, pLayer->m_Height * 32.0f}, false);
				auto AddCell = [&](const auto &Cell) {
					return Extend(Entry, Side, {Cell.m_X * 32.0f, Cell.m_Y * 32.0f, Cell.m_X * 32.0f + 32, Cell.m_Y * 32.0f + 32});
				};
				auto Change = std::find_if(Entry.m_vTiles.begin(), Entry.m_vTiles.end(), [&](const auto &Cell) {
					return !((Side == 0 ? Cell.m_Before : Cell.m_After) == MapDiff::CTile{}) && AddCell(Cell);
				});
				if(Change == Entry.m_vTiles.end())
					std::find_if(pLayer->m_vCells.begin(), pLayer->m_vCells.end(), AddCell);
			}
			for(int O = 0; O < (int)pLayer->m_vObjects.size(); ++O)
				Extend(Entry, Side, ObjectBounds(*m_apSources[Side], SourceLayer, O, pLayer->m_vObjects[O].m_aBounds));
		}
	}
	if(Left <= Right && Top <= Bottom)
	{
		m_Center = vec2((Left + Right) / 2, (Top + Bottom) / 2);
		const float FitZoom = std::max((Right - Left + 64) / std::max(200.0f, m_aPanes[0].w), (Bottom - Top + 64) / std::max(160.0f, m_aPanes[0].h));
		m_Zoom = std::clamp(FitZoom, MIN_ZOOM, MAX_ZOOM);
		if(FitZoom > MAX_ZOOM && ContentPriority > 0)
			m_Center = ContentCenter;
	}
}

void CMapCompare::RenderSelector(CEditor &Editor, CUIRect View)
{
	CUIRect Heading, Buttons;
	View.HSplitTop(32, &Heading, &View);
	Label(Editor, Heading, "Choose Before and After. Current contents include unsaved edits.", 14);
	View.HSplitBottom(32, &View, &Buttons);
	CUIRect aSides[2];
	View.VSplitMid(&aSides[0], &aSides[1], 10);
	for(int Side = 0; Side < 2; ++Side)
	{
		CUIRect Title;
		aSides[Side].HSplitTop(24, &Title, &aSides[Side]);
		Label(Editor, Title, Side == 0 ? "- Before" : "+ After", 14, Side == 0 ? REMOVE_COLOR : ADD_COLOR);
		int Selected = -1;
		for(int i = 0; i < (int)Editor.m_vpMaps.size(); ++i)
			if(Editor.m_vpMaps[i].get() == m_apSources[Side])
				Selected = i;
		m_aPairLists[Side].SetActive(Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen());
		m_aPairLists[Side].DoStart(42, Editor.m_vpMaps.size(), 1, 3, Selected, &aSides[Side]);
		for(const auto &pMap : Editor.m_vpMaps)
		{
			auto Item = m_aPairLists[Side].DoNextItem(&m_aSelectorIds[Side][pMap.get()]);
			if(Item.m_Visible)
			{
				CUIRect Name, Path;
				Item.m_Rect.Margin(4, &Item.m_Rect);
				Item.m_Rect.HSplitTop(17, &Name, &Path);
				Label(Editor, Name, SourceName(pMap.get()));
				Label(Editor, Path, pMap->m_aFilename[0] ? pMap->m_aFilename : "No saved path", 10);
			}
		}
		int Result = m_aPairLists[Side].DoEnd();
		if(Result >= 0)
			m_apSources[Side] = Editor.m_vpMaps[Result].get();
	}
	Buttons.VSplitRight(160, nullptr, &Buttons);
	Buttons.Margin(4, &Buttons);
	if(Editor.DoButton_Editor(&m_aButtonIds[9], "Compare current contents", ValidSources(Editor) ? 0 : -1, &Buttons, BUTTONFLAG_LEFT, "Open the read-only comparison.") && ValidSources(Editor))
		Editor.m_MapCompare.Open(Editor, m_apSources[0], m_apSources[1]);
}

void CMapCompare::RenderInspector(CEditor &Editor, CUIRect View)
{
	View.Draw(ColorRGBA(0.12f, 0.12f, 0.14f, 1), IGraphics::CORNER_NONE, 0);
	View.Margin(4, &View);
	CUIRect List, Values;
	View.VSplitLeft(std::min(270.0f, View.w * 0.34f), &List, &Values);
	Values.VMargin(5, &Values);
	m_Details.SetActive(!m_LayerTreeActive && Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen());
	m_Details.DoStart(19, m_vDetails.size(), 1, 3, m_SelectedDetail, &List);
	for(const auto &Detail : m_vDetails)
	{
		auto Item = m_Details.DoNextItem(&Detail);
		if(Item.m_Visible)
		{
			const auto &Entry = m_Result.m_vEntries[Detail.m_Entry];
			std::string Name;
			if(Detail.m_Kind == 1)
			{
				const auto &Tile = Entry.m_vTiles[Detail.m_Index];
				Name = "Tile (" + std::to_string(Tile.m_X) + ", " + std::to_string(Tile.m_Y) + ")";
			}
			else if(Detail.m_Kind == 2)
				Name = "Object " + std::to_string(Detail.m_Index + 1);
			else if(Detail.m_Kind == 0)
				Name = Entry.m_vProperties[Detail.m_Index].m_Name;
			else
				Name = Detail.m_Kind == 3 ? "Unavailable / invalid content" : "Structure / matching";
			Label(Editor, Item.m_Rect, EntryName(Entry) + ": " + Name, 10);
		}
	}
	int Selected = m_Details.DoEnd();
	if(Selected >= 0 && (Selected != m_SelectedDetail || m_Details.WasItemSelected()))
		SelectDetail(Selected);
	if(m_ValueRowsEntry != m_SelectedEntry || m_ValueRowsDetail != m_SelectedDetail)
	{
		m_ValueRowsEntry = m_SelectedEntry;
		m_ValueRowsDetail = m_SelectedDetail;
		m_vValueRows.clear();
		auto &vValues = m_vValueRows;
		if(m_SelectedDetail >= 0 && m_SelectedDetail < (int)m_vDetails.size())
			vValues = DetailValues(m_vDetails[m_SelectedDetail]);
		if(m_SelectedEntry >= 0)
		{
			const auto &Entry = m_Result.m_vEntries[m_SelectedEntry];
			if(Entry.m_Kind == MapDiff::EEntryKind::IMAGE || Entry.m_Kind == MapDiff::EEntryKind::SOUND || Entry.m_Kind == MapDiff::EEntryKind::ENVELOPE)
			{
				for(int Side = 0; Side < 2; ++Side)
				{
					const auto &Snapshot = m_pData->m_aSnapshots[Side];
					int Index = Side == 0 ? Entry.m_Before : Entry.m_After;
					if(Index < 0)
						continue;
					const auto Kind = Entry.m_Kind == MapDiff::EEntryKind::IMAGE ? MapDiff::EResourceKind::IMAGE : Entry.m_Kind == MapDiff::EEntryKind::SOUND ? MapDiff::EResourceKind::SOUND :
																						    MapDiff::EResourceKind::ENVELOPE;
					auto Uses = [&](const auto &References) { return std::any_of(References.begin(), References.end(), [&](const auto &Reference) { return Reference.m_Kind == Kind && Reference.m_Index == Index; }); };
					for(size_t G = 0; G < Snapshot.m_vGroups.size(); ++G)
						for(size_t L = 0; L < Snapshot.m_vGroups[G].m_vLayers.size(); ++L)
						{
							const auto &Layer = Snapshot.m_vGroups[G].m_vLayers[L];
							if(Uses(Layer.m_vReferences) || std::any_of(Layer.m_vObjects.begin(), Layer.m_vObjects.end(), [&](const auto &Object) { return Uses(Object.m_vReferences); }))
							{
								std::string Name = "Group " + std::to_string(G) + " / layer " + std::to_string(L) + " " + Layer.m_Name;
								vValues.push_back({"Affected layer reference", Side == 0 ? Name : "", Side == 1 ? Name : ""});
							}
						}
				}
			}
		}
	}
	auto &vValues = m_vValueRows;
	if(vValues.empty())
	{
		Label(Editor, Values, "No changed properties in this selection.");
		return;
	}
	// Values have a scrollable row of their own; object/envelope fields never
	// disappear beyond a fixed number of lines in the inspector.
	m_Values.Begin(&Values);
	for(const auto &Value : vValues)
	{
		const std::string Before = "- " + Value.m_Before, After = "+ " + Value.m_After;
		const float ColumnWidth = std::max(1.0f, (Values.w - 14) / 2);
		auto Measure = [&](const std::string &Text) {
			CTextCursor Cursor;
			Cursor.m_FontSize = 10;
			Cursor.m_LineWidth = ColumnWidth;
			Cursor.m_Flags = 0;
			Editor.TextRender()->TextEx(&Cursor, Text.c_str());
			return Cursor.Height();
		};
		const float Height = 23 + std::max(Measure(Before), Measure(After));
		CUIRect Row;
		Values.HSplitTop(Height, &Row, &Values);
		if(!m_Values.AddRect(Row))
			continue;
		CUIRect Title, A, B;
		Row.Margin(3, &Row);
		Row.HSplitTop(15, &Title, &Row);
		Row.VSplitMid(&A, &B, 8);
		Label(Editor, Title, Value.m_Name, 11);
		Editor.TextRender()->TextColor(REMOVE_COLOR);
		Editor.TextRender()->Text(A.x, A.y, 10, Before.c_str(), A.w);
		Editor.TextRender()->TextColor(ADD_COLOR);
		Editor.TextRender()->Text(B.x, B.y, 10, After.c_str(), B.w);
		Editor.TextRender()->TextColor(ColorRGBA(1, 1, 1, 1));
	}
	m_Values.End();
}

void CMapCompare::RenderSettings(CEditor &Editor, CUIRect View)
{
	const auto &Entry = m_Result.m_vEntries[m_SelectedEntry];
	if(!m_pObjectViewCache)
		m_pObjectViewCache = std::make_unique<CObjectViewCache>();
	auto &Cache = *m_pObjectViewCache;
	View.Draw(ColorRGBA(0.08f, 0.09f, 0.11f, 1), IGraphics::CORNER_NONE, 0);
	CUIRect Notice;
	View.HSplitTop(20, &Notice, &View);
	Label(Editor, Notice, Entry.m_SettingsConservative ? "Large unmatched span: aligned by position. Equal prefix/suffix preserved." : "Ordered commands / shared scroll / click a changed row / P and N reveal differences", 10);
	if(Entry.m_vSettings.empty())
	{
		Label(Editor, View, "No server settings on either side.");
		return;
	}
	if(Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen() && Editor.Ui()->MouseButtonClicked(0) && View.Inside(vec2(Editor.Ui()->MouseX(), Editor.Ui()->MouseY())))
		m_LayerTreeActive = false;
	const CUIRect Clip = View;
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 45;
	Params.m_ForceShowScrollbar = true;
	m_Settings.Begin(&View, &Params);
	const float ColumnWidth = std::max(1.0f, (View.w - 8) / 2 - 43);
	const bool Remeasured = Cache.m_SettingsWidth < 0 || (Editor.Ui()->ActiveItem() != &Editor.m_ToolBoxDivider && Cache.m_SettingsWidth != ColumnWidth);
	if(Remeasured)
	{
		Cache.m_SettingsWidth = ColumnWidth;
		Cache.m_vSettingOffsets.assign(1, 0);
		Cache.m_vPropertyRows.assign(Entry.m_vProperties.size(), -1);
		for(int RowIndex = 0; RowIndex < (int)Entry.m_vSettings.size(); ++RowIndex)
		{
			const auto &Row = Entry.m_vSettings[RowIndex];
			float Height = 15;
			for(int Side = 0; Side < 2; ++Side)
			{
				const int Line = Side == 0 ? Row.m_Before : Row.m_After;
				if(Line < 0)
					continue;
				CTextCursor Cursor;
				Cursor.m_FontSize = 10;
				Cursor.m_LineWidth = ColumnWidth;
				Cursor.m_Flags = 0;
				Editor.TextRender()->TextEx(&Cursor, m_pData->m_aSnapshots[Side].m_vSettings[Line].c_str());
				Height = std::max(Height, Cursor.Height() + 8);
			}
			Cache.m_vSettingOffsets.push_back(Cache.m_vSettingOffsets.back() + Height);
			if(Row.m_Property >= 0)
				Cache.m_vPropertyRows[Row.m_Property] = RowIndex;
		}
	}
	const int Property = m_SelectedDetail >= 0 && m_vDetails[m_SelectedDetail].m_Kind == 0 ? m_vDetails[m_SelectedDetail].m_Index : -1;
	const int SelectedRow = Property >= 0 ? Cache.m_vPropertyRows[Property] : -1;
	auto RectForRow = [&](int Index) { return CUIRect{View.x, View.y + Cache.m_vSettingOffsets[Index], View.w, Cache.m_vSettingOffsets[Index + 1] - Cache.m_vSettingOffsets[Index]}; };
	if(m_ScrollToSetting && SelectedRow >= 0)
		m_Settings.AddRect(RectForRow(SelectedRow), true);
	m_ScrollToSetting = false;
	// Register total height once and binary-search the first visible row.
	m_Settings.AddRect({View.x, View.y, View.w, Cache.m_vSettingOffsets.back()});
	const int Start = std::max(0, (int)(std::upper_bound(Cache.m_vSettingOffsets.begin(), Cache.m_vSettingOffsets.end(), Clip.y - View.y) - Cache.m_vSettingOffsets.begin()) - 1);
	for(int i = Start; i < (int)Entry.m_vSettings.size(); ++i)
	{
		CUIRect Row = RectForRow(i);
		if(Row.y >= Clip.y + Clip.h)
			break;
		const auto &Setting = Entry.m_vSettings[i];
		const bool Changed = Setting.m_Property >= 0;
		if(i == SelectedRow)
			Row.Draw(ColorRGBA(1, 1, 1, 0.12f), IGraphics::CORNER_NONE, 0);
		if(Changed && Editor.Ui()->DoButtonLogic(&Setting, 0, &Row, BUTTONFLAG_LEFT))
		{
			m_LayerTreeActive = false;
			SelectDetail(Setting.m_Property);
		}
		CUIRect aSides[2];
		Row.VSplitMid(&aSides[0], &aSides[1], 8);
		for(int Side = 0; Side < 2; ++Side)
		{
			Editor.Ui()->ClipEnable(&aSides[Side]);
			const int Line = Side == 0 ? Setting.m_Before : Setting.m_After;
			CUIRect Number, Text;
			aSides[Side].Margin(4, &Text);
			Text.VSplitLeft(35, &Number, &Text);
			Number.h = 12; // Source line numbers align with the first wrapped line.
			const ColorRGBA Color = Changed ? (Side == 0 ? REMOVE_COLOR : ADD_COLOR) : ColorRGBA(0.76f, 0.78f, 0.8f, 1);
			Label(Editor, Number, Line < 0 ? "-" : std::to_string(Line + 1), 9, Color);
			Editor.TextRender()->TextColor(Line < 0 ? ColorRGBA(0.5f, 0.5f, 0.5f, 1) : ColorRGBA(0.76f, 0.78f, 0.8f, 1));
			// Keep wrapping and cached row heights together during divider drags.
			// TextEx mutates color splits, so the immutable worker result is copied
			// into a fresh cursor. One draw retains whole-command kerning/wrapping.
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(Text.x, Text.y));
			Cursor.m_FontSize = 10;
			Cursor.m_LineWidth = Cache.m_SettingsWidth;
			for(const auto &Range : Setting.m_avChanged[Side])
				Cursor.m_vColorSplits.emplace_back(Range.m_Start, Range.m_End - Range.m_Start, Color);
			Editor.TextRender()->TextEx(&Cursor, Line < 0 ? "[no line]" : m_pData->m_aSnapshots[Side].m_vSettings[Line].c_str());
			Editor.TextRender()->TextColor(ColorRGBA(1, 1, 1, 1));
			Editor.Ui()->ClipDisable();
		}
	}
	m_Settings.End();
	if(Remeasured && !m_Settings.ContentOverflows())
		m_Settings.Reset();
	if(!m_LayerTreeActive && Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen())
	{
		if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_DOWN))
			Navigate(1);
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_UP))
			Navigate(-1);
	}
}

void CMapCompare::RenderEnvelope(CEditor &Editor, CUIRect Pane, int Side)
{
	const auto &Entry = m_Result.m_vEntries[m_SelectedEntry];
	const CEnvelope *apEnvelopes[2]{};
	for(int S = 0; S < 2; ++S)
	{
		const int Index = S == 0 ? Entry.m_Before : Entry.m_After;
		if(Index >= 0 && Index < (int)m_apSources[S]->m_vpEnvelopes.size())
			apEnvelopes[S] = m_apSources[S]->m_vpEnvelopes[Index].get();
	}
	if(!m_pObjectViewCache)
		m_pObjectViewCache = std::make_unique<CObjectViewCache>();
	auto &Cache = *m_pObjectViewCache;
	if(!Cache.m_EnvelopeReady)
	{
		Cache.m_EnvelopeReady = true;
		Cache.m_MinValue = std::numeric_limits<float>::max();
		Cache.m_MaxValue = std::numeric_limits<float>::lowest();
		Cache.m_MaxTime = 0;
		for(int S = 0; S < 2; ++S)
		{
			const auto *pEnv = apEnvelopes[S];
			if(!pEnv || pEnv->m_vPoints.empty())
				continue;
			auto &Plot = Cache.m_aPlots[S];
			Plot.m_vChanged.assign(pEnv->m_vPoints.size(), Entry.m_Before < 0 || Entry.m_After < 0);
			for(const auto &Property : Entry.m_vProperties)
			{
				int Index;
				if(std::sscanf(Property.m_Name.c_str(), "Point %d:", &Index) == 1 && Index >= 0 && Index < (int)Plot.m_vChanged.size())
					Plot.m_vChanged[Index] = true;
			}
			for(int i = 0; i < (int)pEnv->m_vPoints.size(); ++i)
			{
				const auto &Point = pEnv->m_vPoints[i];
				Cache.m_MinTime = std::min(Cache.m_MinTime, Point.m_Time.AsSeconds());
				Cache.m_MaxTime = std::max(Cache.m_MaxTime, Point.m_Time.AsSeconds());
				for(int c = 0; c < pEnv->GetChannels(); ++c)
				{
					auto Include = [&](float Value) {
						Cache.m_MinValue = std::min(Cache.m_MinValue, Value);
						Cache.m_MaxValue = std::max(Cache.m_MaxValue, Value);
					};
					const float Value = fx2f(Point.m_aValues[c]);
					Include(Value);
					if(i + 1 < (int)pEnv->m_vPoints.size() && Point.m_Curvetype == CURVETYPE_BEZIER)
						Include(Value + fx2f(Point.m_Bezier.m_aOutTangentDeltaY[c]));
					if(i > 0 && pEnv->m_vPoints[i - 1].m_Curvetype == CURVETYPE_BEZIER)
						Include(Value + fx2f(Point.m_Bezier.m_aInTangentDeltaY[c]));
				}
				if(Plot.m_vChanged[i] && Plot.m_vMarkers.size() < 256)
					Plot.m_vMarkers.push_back(i);
			}
			const int Count = pEnv->m_vPoints.size();
			for(int i = 0; i < std::min(Count, 256); ++i)
				Plot.m_vMarkers.push_back(Count <= 256 ? i : (int)((int64_t)i * (Count - 1) / 255));
			std::sort(Plot.m_vMarkers.begin(), Plot.m_vMarkers.end());
			Plot.m_vMarkers.erase(std::unique(Plot.m_vMarkers.begin(), Plot.m_vMarkers.end()), Plot.m_vMarkers.end());
			const float Start = pEnv->m_vPoints.front().m_Time.AsSeconds(), End = pEnv->EndTime();
			const int Steps = Count > 1 && End > Start ? 1024 : 0;
			for(int i = 0; i <= Steps; ++i)
			{
				const float Time = Steps ? Start + (End - Start) * i / Steps : Start;
				ColorRGBA Value(0, 0, 0, 0);
				// Eval wraps at EndTime. The graph ends at the actual final value.
				if(i == Steps || Time >= End)
					for(int c = 0; c < pEnv->GetChannels(); ++c)
						Value[c] = fx2f(pEnv->m_vPoints.back().m_aValues[c]);
				else
					pEnv->Eval(std::min(Time, std::nextafter(End, 0.0f)), Value, pEnv->GetChannels());
				for(int c = 0; c < pEnv->GetChannels(); ++c)
					if(std::isfinite(Value[c]))
						Plot.m_avCurves[c].emplace_back(Time, Value[c]);
			}
		}
		if(Cache.m_MinValue > Cache.m_MaxValue)
		{
			Cache.m_MinValue = 0;
			Cache.m_MaxValue = 1;
		}
		const float Pad = std::max({0.05f, (Cache.m_MaxValue - Cache.m_MinValue) * 0.08f,
			std::max(std::abs(Cache.m_MinValue), std::abs(Cache.m_MaxValue)) * std::numeric_limits<float>::epsilon()});
		Cache.m_MinValue = std::min(Cache.m_MinValue - Pad, std::nextafter(Cache.m_MinValue, -std::numeric_limits<float>::infinity()));
		Cache.m_MaxValue = std::max(Cache.m_MaxValue + Pad, std::nextafter(Cache.m_MaxValue, std::numeric_limits<float>::infinity()));
		Cache.m_MaxTime = std::max(Cache.m_MaxTime, Cache.m_MinTime + 0.001f);
	}
	Pane.Draw(ColorRGBA(0.08f, 0.09f, 0.11f, 1), IGraphics::CORNER_NONE, 0);
	Pane.Margin(6, &Pane);
	CUIRect Title, Legend;
	Pane.HSplitTop(19, &Title, &Pane);
	Pane.HSplitTop(19, &Legend, &Pane);
	const auto *pEnv = apEnvelopes[Side];
	const char *pType = !pEnv ? "Absent envelope" : pEnv->GetChannels() == 4 ? "Color" :
						pEnv->GetChannels() == 3         ? "Position" :
										   "Sound";
	Label(Editor, Title, std::string(pType) + (pEnv ? " / " + std::to_string(pEnv->m_vPoints.size()) + " points" : "") + (Entry.m_Ambiguous ? " / ambiguous pairing" : ""), 10);
	const ColorRGBA aColors[] = {ColorRGBA(1, 0.2f, 0.2f, 1), ColorRGBA(0.2f, 1, 0.2f, 1), ColorRGBA(0.2f, 0.2f, 1, 1), ColorRGBA(1, 1, 0.2f, 1)};
	const char *aNames[] = {"R", "G", "B", "A"};
	if(pEnv)
	{
		if(pEnv->GetChannels() == 3)
		{
			aNames[0] = "X";
			aNames[1] = "Y";
			aNames[2] = "Rot";
		}
		else if(pEnv->GetChannels() == 1)
			aNames[0] = "Volume";
		for(int c = 0; c < pEnv->GetChannels(); ++c)
		{
			CUIRect Button;
			Legend.VSplitLeft(std::min(54.0f, Pane.w / pEnv->GetChannels()), &Button, &Legend);
			Button.VMargin(2, &Button);
			if(Editor.DoButton_Env(&m_aChannelIds[Side][c], aNames[c], (m_EnvelopeChannels & (1 << c)) != 0, &Button, "Toggle this channel in both plots.", aColors[c], IGraphics::CORNER_ALL))
				m_EnvelopeChannels ^= 1 << c;
			Editor.TextRender()->TextColor(ColorRGBA(1, 1, 1, 1));
		}
	}
	CUIRect Footer, Axis;
	Pane.HSplitBottom(20, &Pane, &Footer);
	Pane.VSplitLeft(45, &Axis, &Pane);
	Pane.Margin(4, &Pane);
	auto Position = [&](vec2 Point) { return vec2(Pane.x + (Point.x - Cache.m_MinTime) / (Cache.m_MaxTime - Cache.m_MinTime) * Pane.w, Pane.y + Pane.h - (Point.y - Cache.m_MinValue) / (Cache.m_MaxValue - Cache.m_MinValue) * Pane.h); };
	auto *pGraphics = Editor.Graphics();
	pGraphics->TextureClear();
	pGraphics->LinesBegin();
	pGraphics->SetColor(1, 1, 1, 0.15f);
	for(int Tick = 0; Tick <= 4; ++Tick)
	{
		const float Y = Pane.y + Pane.h * Tick / 4;
		const float X = Pane.x + Pane.w * Tick / 4;
		const IGraphics::CLineItem aLines[] = {{Pane.x, Y, Pane.x + Pane.w, Y}, {X, Pane.y, X, Pane.y + Pane.h}};
		pGraphics->LinesDraw(aLines, 2);
	}
	pGraphics->LinesEnd();
	for(int Tick = 0; Tick <= 4; ++Tick)
	{
		char aText[48];
		str_format(aText, sizeof(aText), "%.3g", Cache.m_MaxValue - (Cache.m_MaxValue - Cache.m_MinValue) * Tick / 4);
		Label(Editor, {Axis.x, Pane.y + Pane.h * Tick / 4 - 5, Axis.w, 10}, aText, 9);
	}
	char aTime[96];
	str_format(aTime, sizeof(aTime), "Sampled preview / %.3g to %.3g s", Cache.m_MinTime, Cache.m_MaxTime);
	Label(Editor, Footer, aTime, 9);
	if(!pEnv || pEnv->m_vPoints.empty())
	{
		Label(Editor, Pane, pEnv ? "Empty envelope (no points)" : "No envelope on this side.", 11);
		return;
	}
	if(!(m_EnvelopeChannels & ((1 << pEnv->GetChannels()) - 1)))
	{
		Label(Editor, Pane, "No channels visible", 10);
		return;
	}
	int SelectedPoint = -1;
	if(m_SelectedDetail >= 0 && m_vDetails[m_SelectedDetail].m_Kind == 0)
		std::sscanf(Entry.m_vProperties[m_vDetails[m_SelectedDetail].m_Index].m_Name.c_str(), "Point %d:", &SelectedPoint);
	const auto &Plot = Cache.m_aPlots[Side];
	Editor.Ui()->ClipEnable(&Pane);
	pGraphics->LinesBegin();
	for(int c = 0; c < pEnv->GetChannels(); ++c)
	{
		if(!(m_EnvelopeChannels & (1 << c)))
			continue;
		pGraphics->SetColor(aColors[c]);
		const auto &Curve = Plot.m_avCurves[c];
		for(size_t i = 1; i < Curve.size(); ++i)
		{
			const vec2 A = Position(Curve[i - 1]), B = Position(Curve[i]);
			const IGraphics::CLineItem Line(A.x, A.y, B.x, B.y);
			pGraphics->LinesDraw(&Line, 1);
		}
	}
	pGraphics->LinesEnd();
	int HitPoint = -1;
	float HitDistance = 5.0f;
	const vec2 Mouse(Editor.Ui()->MouseX(), Editor.Ui()->MouseY());
	auto DrawPoint = [&](int Index) {
		const auto &Point = pEnv->m_vPoints[Index];
		for(int c = 0; c < pEnv->GetChannels(); ++c)
		{
			if(!(m_EnvelopeChannels & (1 << c)))
				continue;
			const vec2 P = Position(vec2(Point.m_Time.AsSeconds(), fx2f(Point.m_aValues[c])));
			const bool Selected = Index == SelectedPoint;
			const float Size = Selected ? 7 : Plot.m_vChanged[Index] ? 5 :
										   3;
			CUIRect Dot{P.x - Size / 2, P.y - Size / 2, Size, Size};
			Dot.Draw(Selected ? ColorRGBA(1, 1, 1, 1) : Plot.m_vChanged[Index] ? (Side == 0 ? REMOVE_COLOR : ADD_COLOR) :
											     aColors[c],
				IGraphics::CORNER_NONE, 0);
			const float Distance = distance(Mouse, P);
			if(Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen() && Plot.m_vChanged[Index] && Editor.Ui()->MouseButtonClicked(0) && Pane.Inside(Mouse) && Distance < HitDistance)
			{
				HitPoint = Index;
				HitDistance = Distance;
			}
		}
	};
	for(int Index : Plot.m_vMarkers)
		if(Index != SelectedPoint)
			DrawPoint(Index);
	if(SelectedPoint >= 0 && SelectedPoint < (int)pEnv->m_vPoints.size())
		DrawPoint(SelectedPoint);
	Editor.Ui()->ClipDisable();
	if(HitPoint >= 0)
	{
		const std::string Prefix = "Point " + std::to_string(HitPoint) + ":";
		for(int d = 0; d < (int)m_vDetails.size(); ++d)
			if(m_vDetails[d].m_Kind == 0 && Entry.m_vProperties[m_vDetails[d].m_Index].m_Name.compare(0, Prefix.size(), Prefix) == 0)
			{
				SelectDetail(d);
				m_LayerTreeActive = false;
				m_Details.SetActive(true);
				break;
			}
	}
}

void CMapCompare::RenderPane(CEditor &Editor, CUIRect Pane, int Side)
{
	auto *pGraphics = Editor.Graphics();
	const ColorRGBA Highlight = Side == 0 ? REMOVE_COLOR : ADD_COLOR;
	Pane.Draw(ColorRGBA(0.08f, 0.09f, 0.11f, 1), IGraphics::CORNER_NONE, 0);
	if(m_SelectedEntry < 0 || !ValidSources(Editor))
		return;
	const auto &Selected = m_Result.m_vEntries[m_SelectedEntry];
	const auto &Map = *m_apSources[Side];
	const auto &Snapshot = m_pData->m_aSnapshots[Side];
	if(Selected.m_Kind == MapDiff::EEntryKind::IMAGE)
	{
		int Index = Side == 0 ? Selected.m_Before : Selected.m_After;
		if(Index >= 0 && Map.m_vpImages[Index]->m_Texture.IsValid())
		{
			const auto &Image = *Map.m_vpImages[Index];
			CUIRect ImageRect = Pane;
			ImageRect.Margin(12, &ImageRect);
			float Scale = std::min(ImageRect.w / std::max(1.0f, (float)Image.m_Width), ImageRect.h / std::max(1.0f, (float)Image.m_Height));
			pGraphics->TextureSet(Image.m_Texture);
			pGraphics->QuadsBegin();
			pGraphics->SetColor(1, 1, 1, 1);
			IGraphics::CQuadItem Quad(ImageRect.x, ImageRect.y, Image.m_Width * Scale, Image.m_Height * Scale);
			pGraphics->QuadsDrawTL(&Quad, 1);
			pGraphics->QuadsEnd();
			Marker(Editor, ImageRect.x, ImageRect.y, Quad.m_Width, Quad.m_Height, Side, 1, Highlight);
		}
		return;
	}
	CFixedEnvelopeEval Envelope(Map);
	if(!m_pRenderCache)
		m_pRenderCache = std::make_unique<CRenderCache>(pGraphics);
	auto InSelection = [&](int Index) { return Index == m_SelectedEntry || m_Result.m_vEntries[Index].m_Parent == m_SelectedEntry; };
	std::vector<int> vLayers;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER && LayerVisible(i) && (m_Context == CONTEXT_MAP || InSelection(i)))
			vLayers.push_back(i);
	}
	std::sort(vLayers.begin(), vLayers.end(), [&](int A, int B) {
		const auto &EA = m_Result.m_vEntries[A];
		const auto &EB = m_Result.m_vEntries[B];
		// Context preserves each source's draw order; selected content is drawn
		// last so foreground scenery cannot hide the changes being inspected.
		return std::make_tuple(InSelection(A), Side == 0 ? EA.m_BeforeGroup : EA.m_AfterGroup, Side == 0 ? EA.m_Before : EA.m_After) <
		       std::make_tuple(InSelection(B), Side == 0 ? EB.m_BeforeGroup : EB.m_AfterGroup, Side == 0 ? EB.m_Before : EB.m_After);
	});
	// Finish all dim context before any diff overlay. In group mode later
	// unchanged siblings must not cover earlier layers' highlighted changes.
	for(int Pass = 0; Pass < 2; ++Pass)
		for(int i : vLayers)
		{
			const auto &Entry = m_Result.m_vEntries[i];
			const bool Focused = InSelection(i);
			const bool ContextPass = Pass == 0;
			if((ContextPass && !m_Context) || (!ContextPass && !Focused))
				continue;
			const auto *pLayer = Layer(Snapshot, Entry, Side);
			if(!pLayer)
				continue;
			int G = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
			int L = Side == 0 ? Entry.m_Before : Entry.m_After;
			const auto &Group = *Map.m_vpGroups[G];
			const auto &SourceLayer = *Group.m_vpLayers[L];
			const vec2 GroupCenter = ProjectionCenter(Group);
			const vec2 TopLeft = GroupCenter - Pane.Size() * m_Zoom / 2;
			const vec2 FullTopLeft = TopLeft - Pane.TopLeft() * m_Zoom;
			CUIRect Clip = Pane;
			if(Group.m_UseClipping)
			{
				vec2 GameCenter = m_Center;
				if(Map.m_pGameGroup)
					GameCenter = ProjectionCenter(*Map.m_pGameGroup);
				vec2 ClipStart = Pane.Center() + (vec2(Group.m_ClipX, Group.m_ClipY) - GameCenter) / m_Zoom;
				vec2 ClipEnd = ClipStart + vec2(Group.m_ClipW, Group.m_ClipH) / m_Zoom;
				float Right = std::min(Pane.x + Pane.w, ClipEnd.x), Bottom = std::min(Pane.y + Pane.h, ClipEnd.y);
				Clip.x = std::max(Pane.x, ClipStart.x);
				Clip.y = std::max(Pane.y, ClipStart.y);
				Clip.w = std::max(0.0f, Right - Clip.x);
				Clip.h = std::max(0.0f, Bottom - Clip.y);
			}
			if(Clip.w <= 0 || Clip.h <= 0)
				continue;
			const vec2 VisibleStart = TopLeft + (Clip.TopLeft() - Pane.TopLeft()) * m_Zoom;
			const CScreenRect Visible(VisibleStart, VisibleStart + Clip.Size() * m_Zoom);
			Editor.Ui()->ClipEnable(&Clip);
			pGraphics->MapScreen(CScreenRect(FullTopLeft, FullTopLeft + Editor.Ui()->Screen()->Size() * m_Zoom));
			pGraphics->BlendNormal();
			const bool WholeLayer = Focused && (Entry.m_Before < 0 || Entry.m_After < 0 || Entry.m_Reordered || !Entry.m_vProperties.empty() || Selected.m_Reordered || (Selected.m_Kind == MapDiff::EEntryKind::GROUP && !Selected.m_vProperties.empty()));
			if(SourceLayer.m_Type == LAYERTYPE_TILES)
			{
				const auto &Tiles = static_cast<const CLayerTiles &>(SourceLayer);
				IGraphics::CTextureHandle Texture;
				switch(pLayer->m_Kind)
				{
				case MapDiff::ELayerKind::GAME: Texture = Editor.GetEntitiesTexture(); break;
				case MapDiff::ELayerKind::FRONT: Texture = Editor.GetFrontTexture(); break;
				case MapDiff::ELayerKind::TELE: Texture = Editor.GetTeleTexture(); break;
				case MapDiff::ELayerKind::SPEEDUP: Texture = Editor.GetSpeedupTexture(); break;
				case MapDiff::ELayerKind::SWITCH: Texture = Editor.GetSwitchTexture(); break;
				case MapDiff::ELayerKind::TUNE: Texture = Editor.GetTuneTexture(); break;
				default:
					if(Tiles.m_Image >= 0 && Tiles.m_Image < (int)Map.m_vpImages.size())
						Texture = Map.m_vpImages[Tiles.m_Image]->m_Texture;
					break;
				}
				pGraphics->TextureSet(Texture);
				ColorRGBA Color(Tiles.m_Color.r / 255.0f, Tiles.m_Color.g / 255.0f, Tiles.m_Color.b / 255.0f, Tiles.m_Color.a / 255.0f);
				ColorRGBA EnvColor(1, 1, 1, 1);
				Envelope.EnvelopeEval(Tiles.m_ColorEnvOffset, Tiles.m_ColorEnv, EnvColor, 4);
				Color = Color.Multiply(EnvColor);
				if(ContextPass)
				{
					m_pRenderCache->Draw(Editor, *pLayer, Visible, Color.Multiply(ColorRGBA(1, 1, 1, Focused ? SELECTED_CONTEXT_ALPHA : MAP_CONTEXT_ALPHA)), Texture.IsValid());
					Editor.Ui()->MapScreen();
					Editor.Ui()->ClipDisable();
					continue;
				}
				if(WholeLayer)
					m_pRenderCache->Draw(Editor, *pLayer, Visible, Color.Multiply(Highlight), Texture.IsValid());
				std::vector<MapDiff::CCell> vVisibleChanges;
				if(Focused)
					ForVisibleCells(Entry.m_vTiles, Visible, [&](const auto &Change) {
						vVisibleChanges.push_back({Change.m_X, Change.m_Y, Side == 0 ? Change.m_Before : Change.m_After});
					});
				if(!WholeLayer)
					DrawTiles(Editor, vVisibleChanges, Visible, Color.Multiply(Highlight));
				pGraphics->TextureClear();
				pGraphics->LinesBegin();
				pGraphics->SetColor(Highlight);
				for(const auto &Cell : vVisibleChanges)
					if(!(Cell.m_Tile == MapDiff::CTile{}))
						MarkerLines(Editor, Cell.m_X * 32.0f, Cell.m_Y * 32.0f, 32, 32, Side, m_Zoom);
				pGraphics->LinesEnd();
				if(WholeLayer)
					Marker(Editor, 0, 0, pLayer->m_Width * 32.0f, pLayer->m_Height * 32.0f, Side, m_Zoom, Highlight);
				// Source overlay functions are read-only and include number, delay,
				// force, max speed and direction. Restrict them to visible content.
				if(Focused && m_Zoom <= 4 && (m_Context || WholeLayer || !vVisibleChanges.empty()))
				{
					const auto &Cells = m_Context || WholeLayer ? pLayer->m_vCells : vVisibleChanges;
					ForVisibleCells(Cells, Visible, [&](const auto &Cell) {
						vec2 Pos(Cell.m_X * 32.0f, Cell.m_Y * 32.0f);
						if(!Visible.Inside(Pos) || pLayer->m_Kind == MapDiff::ELayerKind::TILES || pLayer->m_Kind == MapDiff::ELayerKind::GAME || pLayer->m_Kind == MapDiff::ELayerKind::FRONT)
							return;
						std::string Text = pLayer->m_Kind == MapDiff::ELayerKind::SPEEDUP ? std::to_string(Cell.m_Tile.m_Force) + "/" + std::to_string(Cell.m_Tile.m_MaxSpeed) + "\n" + std::to_string(Cell.m_Tile.m_Angle) + " deg" : std::to_string(Cell.m_Tile.m_Number);
						if(pLayer->m_Kind == MapDiff::ELayerKind::SWITCH)
							Text += "/" + std::to_string(Cell.m_Tile.m_Delay);
						Editor.TextRender()->Text(Pos.x + 2, Pos.y + 18, 7, Text.c_str(), 32);
					});
				}
			}
			else if(SourceLayer.m_Type == LAYERTYPE_QUADS)
			{
				const auto &Quads = static_cast<const CLayerQuads &>(SourceLayer);
				std::vector<bool> vChanged(Quads.m_vQuads.size(), !ContextPass && WholeLayer);
				for(const auto &Change : Entry.m_vObjects)
				{
					int Index = Side == 0 ? Change.m_Before : Change.m_After;
					if(!ContextPass && Index >= 0)
						vChanged[Index] = true;
				}
				std::vector<CQuad> vVisibleQuads;
				std::vector<std::array<float, 4>> vMarkers;
				m_pRenderCache->VisibleObjects(Map, SourceLayer, *pLayer, Visible, [&](size_t q, const auto &B) {
					if(!ContextPass && !vChanged[q])
						return;
					CQuad Quad = Quads.m_vQuads[q];
					for(auto &Color : Quad.m_aColors)
					{
						if(vChanged[q])
						{
							Color.r = (int)(Color.r * Highlight.r);
							Color.g = (int)(Color.g * Highlight.g);
							Color.b = (int)(Color.b * Highlight.b);
						}
						else
							Color.a = (int)(Color.a * (Focused ? SELECTED_CONTEXT_ALPHA : MAP_CONTEXT_ALPHA));
					}
					vVisibleQuads.push_back(Quad);
					if(vChanged[q])
						vMarkers.push_back(B);
				});
				// Keep source order and envelope evaluation while submitting one
				// batch per visible layer/pass instead of rebinding every quad.
				if(!vVisibleQuads.empty())
				{
					if(Quads.m_Image >= 0 && Quads.m_Image < (int)Map.m_vpImages.size())
						pGraphics->TextureSet(Map.m_vpImages[Quads.m_Image]->m_Texture);
					else
						pGraphics->TextureClear();
					Editor.RenderMap()->ForceRenderQuads(vVisibleQuads.data(), vVisibleQuads.size(), LAYERRENDERFLAG_TRANSPARENT, &Envelope);
				}
				if(!vMarkers.empty())
				{
					pGraphics->TextureClear();
					pGraphics->LinesBegin();
					pGraphics->SetColor(Highlight);
					for(const auto &B : vMarkers)
						MarkerLines(Editor, B[0], B[1], B[2] - B[0], B[3] - B[1], Side, m_Zoom);
					pGraphics->LinesEnd();
				}
			}
			else if(!ContextPass && SourceLayer.m_Type == LAYERTYPE_SOUNDS)
			{
				const auto &Sounds = static_cast<const CLayerSounds &>(SourceLayer);
				std::vector<bool> vChanged(Sounds.m_vSources.size(), WholeLayer);
				for(const auto &Change : Entry.m_vObjects)
				{
					int Index = Side == 0 ? Change.m_Before : Change.m_After;
					if(Index >= 0)
						vChanged[Index] = true;
				}
				for(size_t s = 0; s < Sounds.m_vSources.size(); ++s)
				{
					if(!vChanged[s] && !m_Context)
						continue;
					const auto &Source = Sounds.m_vSources[s];
					ColorRGBA Offset(0, 0, 0, 0);
					Envelope.EnvelopeEval(Source.m_PosEnvOffset, Source.m_PosEnv, Offset, 2);
					vec2 Position(Source.m_Position.x / 1024.0f + Offset.r, Source.m_Position.y / 1024.0f + Offset.g);
					pGraphics->TextureClear();
					pGraphics->QuadsBegin();
					pGraphics->SetColor(vChanged[s] ? Highlight.Multiply(ColorRGBA(1, 1, 1, 0.25f)) : ColorRGBA(0.6f, 0.7f, 0.9f, 0.15f));
					if(Source.m_Shape.m_Type == CSoundShape::SHAPE_CIRCLE)
						pGraphics->DrawCircle(Position.x, Position.y, Source.m_Shape.m_Circle.m_Radius, 32);
					else if(Source.m_Shape.m_Type == CSoundShape::SHAPE_RECTANGLE)
					{
						float W = Source.m_Shape.m_Rectangle.m_Width / 1024.0f, H = Source.m_Shape.m_Rectangle.m_Height / 1024.0f;
						pGraphics->DrawRectExt(Position.x - W / 2, Position.y - H / 2, W, H, 0, 0);
					}
					pGraphics->QuadsEnd();
					Marker(Editor, Position.x - 8, Position.y - 8, 16, 16, Side, m_Zoom, vChanged[s] ? Highlight : ColorRGBA(0.7f, 0.7f, 0.7f, 1));
				}
			}
			Editor.Ui()->MapScreen();
			Editor.Ui()->ClipDisable();
		}
	Editor.Ui()->MapScreen();
	Pane.DrawOutline(Highlight.Multiply(ColorRGBA(1, 1, 1, 0.5f)));
	const int Focus = m_SelectedDetail >= 0 ? m_vDetails[m_SelectedDetail].m_Entry : m_SelectedEntry;
	const auto FocusKind = m_Result.m_vEntries[Focus].m_Kind;
	const bool SpatialFocus = FocusKind == MapDiff::EEntryKind::LAYER || FocusKind == MapDiff::EEntryKind::GROUP;
	const char *pNotice = SpatialFocus && !LayerVisible(Focus)                                             ? "Selection hidden. Use Layers or Show all." :
			      SpatialFocus && m_Context == CONTEXT_OFF && m_vLayerStates[Focus].m_Changes == 0 ? "Enable Context to see unchanged content." :
														 nullptr;
	if(pNotice)
	{
		CUIRect Notice = Pane;
		Notice.Margin(5, &Notice);
		Notice.HSplitTop(18, &Notice, nullptr);
		Notice.Draw(ColorRGBA(0.08f, 0.09f, 0.11f, 0.9f), IGraphics::CORNER_NONE, 0);
		Label(Editor, Notice, pNotice, 10, CHANGE_COLOR);
	}
}

void CMapCompare::RenderLayerTree(CEditor &Editor, CUIRect View)
{
	std::vector<int> vRows;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if((Entry.m_Kind == MapDiff::EEntryKind::GROUP || Entry.m_Kind == MapDiff::EEntryKind::LAYER) &&
			(Entry.m_Parent < 0 || m_vLayerStates[Entry.m_Parent].m_Expanded))
			vRows.push_back(i);
	}
	bool FoldChanged = false;
	if(m_LayerTreeActive && !vRows.empty() && Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen() && !Editor.Input()->ModifierIsPressed() && !Editor.Input()->ShiftIsPressed() && !Editor.Input()->AltIsPressed())
	{
		const auto It = std::find(vRows.begin(), vRows.end(), m_FocusEntry);
		int Row = It == vRows.end() ? -1 : It - vRows.begin();
		int NewRow = Row;
		if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_DOWN))
			NewRow = std::min(Row + 1, (int)vRows.size() - 1);
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_UP))
			NewRow = std::max(Row - 1, 0);
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_PAGE_DOWN))
			NewRow = std::min(Row + 12, (int)vRows.size() - 1);
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_PAGE_UP))
			NewRow = std::max(Row - 12, 0);
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_END))
			NewRow = vRows.size() - 1;
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_LEFT) && Row >= 0)
		{
			auto &State = m_vLayerStates[vRows[Row]];
			const auto &Entry = m_Result.m_vEntries[vRows[Row]];
			if(Entry.m_Kind == MapDiff::EEntryKind::GROUP && State.m_Expanded)
			{
				State.m_Expanded = false;
				FoldChanged = true;
			}
			else if(Entry.m_Parent >= 0)
				NewRow = std::find(vRows.begin(), vRows.end(), Entry.m_Parent) - vRows.begin();
		}
		else if(Editor.Ui()->ConsumeHotkey(CUi::HOTKEY_RIGHT) && Row >= 0 && m_Result.m_vEntries[vRows[Row]].m_Kind == MapDiff::EEntryKind::GROUP)
		{
			if(!m_vLayerStates[vRows[Row]].m_Expanded)
			{
				m_vLayerStates[vRows[Row]].m_Expanded = true;
				FoldChanged = true;
			}
			else if(Row + 1 < (int)vRows.size() && m_Result.m_vEntries[vRows[Row + 1]].m_Parent == vRows[Row])
				NewRow = Row + 1;
		}
		if(NewRow >= 0 && NewRow != Row)
		{
			m_ByGroup = m_Result.m_vEntries[vRows[NewRow]].m_Kind == MapDiff::EEntryKind::GROUP;
			Select(vRows[NewRow]);
			RebuildNavigator(true);
			RevealLayerSelection();
		}
	}
	View.Draw(ColorRGBA(0, 0, 0, 0.15f), IGraphics::CORNER_ALL, 5);
	if(vRows.empty())
	{
		Label(Editor, View, "No groups or layers on either side.", 10);
		return;
	}
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 12 * 5;
	Params.m_ScrollbarThickness = 10;
	Params.m_ScrollbarMargin = 3;
	m_LayerTree.Begin(&View, &Params);
	bool HadGroup = false;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		const bool Group = Entry.m_Kind == MapDiff::EEntryKind::GROUP;
		if(!Group && Entry.m_Kind != MapDiff::EEntryKind::LAYER)
			continue;
		if(Entry.m_Parent >= 0 && !m_vLayerStates[Entry.m_Parent].m_Expanded)
			continue;
		if(Group)
		{
			if(HadGroup)
				View.HSplitTop(5, nullptr, &View);
			HadGroup = true;
		}
		auto &State = m_vLayerStates[i];
		CUIRect Row, Space;
		View.HSplitTop(12, &Row, &View);
		View.HSplitTop(2, &Space, &View);
		m_LayerTree.AddRect(Space);
		const bool Focused = i == m_FocusEntry;
		if(!m_LayerTree.AddRect(Row, m_ScrollToLayer && Focused))
			continue;
		if(!Group)
			Row.VSplitLeft(12, nullptr, &Row);
		CUIRect Eye, Badge;
		Row.VSplitLeft(15, &Eye, &Row);
		const bool ParentHidden = Entry.m_Parent >= 0 && !m_vLayerStates[Entry.m_Parent].m_Visible;
		const int EyeClick = Editor.DoButton_FontIcon(&State.m_Visible, LayerVisible(i) ? FontIcon::EYE : FontIcon::EYE_SLASH, ParentHidden ? -1 : Group && !State.m_Expanded ? 1 :
																							0,
			&Eye, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT,
			ParentHidden ? "Hidden by its group. Show the group first; this layer's choice is preserved." : "Left click: toggle comparison visibility. Right click: solo this item; repeat to restore peers.", IGraphics::CORNER_L, 8);
		if(EyeClick && !ParentHidden)
		{
			if(EyeClick == 1)
				State.m_Visible = !State.m_Visible;
			else
			{
				bool HadVisiblePeer = false;
				for(int Peer = 0; Peer < (int)m_Result.m_vEntries.size(); ++Peer)
					if(Peer != i && m_Result.m_vEntries[Peer].m_Kind == Entry.m_Kind && m_Result.m_vEntries[Peer].m_Parent == Entry.m_Parent)
					{
						HadVisiblePeer |= m_vLayerStates[Peer].m_Visible;
						m_vLayerStates[Peer].m_Visible = false;
					}
				State.m_Visible = true;
				if(!HadVisiblePeer)
					for(int Peer = 0; Peer < (int)m_Result.m_vEntries.size(); ++Peer)
						if(m_Result.m_vEntries[Peer].m_Kind == Entry.m_Kind && m_Result.m_vEntries[Peer].m_Parent == Entry.m_Parent)
							m_vLayerStates[Peer].m_Visible = true;
			}
		}
		const bool Added = Entry.m_Before < 0, Removed = Entry.m_After < 0, Changed = State.m_Changes > 0;
		std::string Description = Added ? "Added / After only" : Removed ? "Removed / Before only" :
								 Changed         ? "Changed / " + std::to_string(State.m_Changes) + " changes" :
										   "Unchanged";
		if(Entry.m_Ambiguous)
			Description += Entry.m_Positional ? ". Positional fallback; historical identity is unknown" : ". Ambiguous duplicate content; historical identity is unknown";
		Description += ". " + SourceIndices(Entry);
		if(!LayerVisible(i))
			Description += ParentHidden ? ". Group hidden" : ". Hidden";
		if(Group)
			Description += State.m_Expanded ? ". Double click to fold" : ". Double click to expand";
		const auto *pLayer = Group ? nullptr : Layer(m_pData->m_aSnapshots[Added ? 1 : 0], Entry, Added ? 1 : 0);
		const bool Gameplay = pLayer && pLayer->m_Kind >= MapDiff::ELayerKind::GAME && pLayer->m_Kind <= MapDiff::ELayerKind::TUNE;
		const std::string Title = (Group && Entry.m_Name != "(unnamed)" && !Entry.m_Name.empty() ? "#" + std::to_string(Added ? Entry.m_After : Entry.m_Before) + " " : "") + EntryName(Entry);
		Row.VSplitRight(Entry.m_Ambiguous ? 23 : 13, &Row, &Badge);
		const ColorRGBA Color = Added ? ADD_COLOR : Removed ? REMOVE_COLOR :
						    Changed         ? CHANGE_COLOR :
								      ColorRGBA(0.75f, 0.75f, 0.75f, 1);
		const int Checked = (Focused ? 1 : 0) + (Gameplay ? 6 : 0);
		Row.Draw(Editor.GetButtonColor(&Entry, Checked), IGraphics::CORNER_R, 3);
		CUIRect Name;
		Row.VMargin(5, &Name);
		SLabelProperties Props;
		Props.m_MaxWidth = Name.w;
		Props.m_EllipsisAtEnd = true;
		Props.SetColor(Color);
		Editor.Ui()->DoLabel(&Name, Title.c_str(), 10, TEXTALIGN_ML, Props);
		if(Editor.DoButtonLogic(&Entry, Checked, &Row, BUTTONFLAG_LEFT, Description.c_str()))
		{
			if(m_SelectedEntry != i || m_ByGroup != Group)
			{
				m_ByGroup = Group;
				Select(i);
				RebuildNavigator(true);
			}
			if(Group && Editor.Ui()->DoDoubleClickLogic(&Entry))
			{
				State.m_Expanded = !State.m_Expanded;
				FoldChanged = true;
			}
		}
		Label(Editor, Badge, std::string(Added ? "+" : Removed ? "-" :
						       Changed         ? "~" :
									 "=") +
					     (Entry.m_Ambiguous ? " ?" : ""),
			9, Color);
	}
	m_LayerTree.End();
	// The generic region retains its old animation position when content fits.
	// Clear it on this transition so expanding cannot revive an obsolete offset.
	if(FoldChanged && !m_LayerTree.ContentOverflows())
		m_LayerTree.Reset();
	m_ScrollToLayer = false;
}

void CMapCompare::Render(CEditor &Editor, CUIRect View, CUIRect Toolbar, CUIRect Sidebar)
{
	Editor.Ui()->MapScreen();
	CUIRect ToolbarTop, ToolbarBottom;
	CEditor::SplitToolbarRows(Toolbar, ToolbarTop, ToolbarBottom);
	auto Button = [&](CUIRect &Row, int Id, const char *pText, float Width, int Checked = 0, const char *pTooltip = nullptr) {
		CUIRect Rect;
		Row.VSplitLeft(Width, &Rect, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		return Editor.DoButton_Editor(&m_aButtonIds[Id], pText, Checked, &Rect, BUTTONFLAG_LEFT, pTooltip ? pTooltip : pText) != 0;
	};
	if(Button(ToolbarTop, 0, "Back", 45, 0, "[Esc] Return to editing."))
		Editor.m_MapCompare.SelectMap(Editor, Editor.Map());
	if(m_State == EState::CLOSING)
	{
		Label(Editor, View, "Returning to the editor. Release keys and mouse buttons to continue.", 14);
		return;
	}
	if(m_State == EState::SELECT)
	{
		RenderSelector(Editor, View);
		return;
	}
	if(m_State == EState::UNAVAILABLE)
	{
		Editor.TextRender()->Text(View.x + 12, View.y + 30, 14, m_Message.c_str(), View.w - 24);
		return;
	}
	if(m_State == EState::CAPTURE || m_State == EState::COMPUTING || !Current(Editor))
	{
		Label(Editor, View, "Updating comparison to current map contents...", 14);
		return;
	}
	if(Button(ToolbarTop, 1, "Swap", 45))
	{
		Editor.m_MapCompare.Open(Editor, m_apSources[1], m_apSources[0]);
		return;
	}
	if(Button(ToolbarTop, 2, m_ByGroup ? "By group" : "By layer", 70))
	{
		m_ByGroup = !m_ByGroup;
		const int FocusedChild = m_FocusEntry;
		if(m_ByGroup && m_SelectedEntry >= 0 && m_Result.m_vEntries[m_SelectedEntry].m_Parent >= 0)
			m_SelectedEntry = m_Result.m_vEntries[m_SelectedEntry].m_Parent;
		if(!m_ByGroup && FocusedChild >= 0 && m_Result.m_vEntries[FocusedChild].m_Parent == m_SelectedEntry)
			m_SelectedEntry = FocusedChild;
		RebuildNavigator();
		Select(m_SelectedEntry);
		if(m_ByGroup && FocusedChild >= 0)
			m_FocusEntry = FocusedChild;
	}
	const char *pContextLabel = m_Context == CONTEXT_OFF ? "Context: off" : m_Context == CONTEXT_MAP ? "Context: map" :
									m_ByGroup                        ? "Context: group" :
													   "Context: layer";
	if(Button(ToolbarTop, 3, pContextLabel, 92, m_Context != CONTEXT_OFF, "Cycle context: changes only, selected layer/group, full map. Changes stay emphasized."))
	{
		m_Context = (m_Context + 1) % 3;
		if(m_vChangedNavigator.empty())
			m_UnchangedExpanded = true;
		RebuildNavigator();
		NavigatorList().ScrollToSelected();
		if(m_SelectedEntry < 0 && !m_vNavigator.empty() && m_vNavigator.front() >= 0)
			Select(m_vNavigator.front());
	}
	if(Button(ToolbarBottom, 4, "Prev", 45, m_vChangedNavigator.empty() ? -1 : 0, "[P] Go to the previous difference, skipping unchanged items."))
		Navigate(-1);
	if(Button(ToolbarBottom, 5, "Next", 45, m_vChangedNavigator.empty() ? -1 : 0, "[N] Go to the next difference, skipping unchanged items."))
		Navigate(1);
	if(Button(ToolbarBottom, 6, "Fit", 40, 0, "[Home] Fit the selected difference."))
		FitSelection();
	Label(Editor, ToolbarBottom, "READ ONLY", 11);
	CUIRect Navigator, Inspector{};
	if(Editor.Ui()->ActiveItem() == &Editor.m_ToolBoxDivider)
		m_Panning = false;
	const bool NarrowSidebar = Sidebar.w < 180;
	CUIRect Tabs, aRows[2], aTabs[4];
	Sidebar.HSplitTop(NarrowSidebar ? 96 : 48, &Tabs, &Navigator);
	Tabs.VSplitRight(4, &Tabs, nullptr);
	if(NarrowSidebar)
	{
		for(auto &Tab : aTabs)
		{
			Tabs.HSplitTop(24, &Tab, &Tabs);
			Tab.HMargin(2, &Tab);
		}
	}
	else
	{
		Tabs.HSplitMid(&aRows[0], &aRows[1], 3);
		for(int Row = 0; Row < 2; ++Row)
		{
			aRows[Row].HMargin(2, &aRows[Row]);
			aRows[Row].VSplitMid(&aTabs[Row * 2], &aTabs[Row * 2 + 1], 3);
		}
	}
	const char *apSections[] = {"Changes", "Layers", "Envelopes", "Server settings"};
	const char *apTips[] = {"Browse map, image, sound and layer differences.", "Inspect layers and comparison visibility.", "Inspect all envelopes, including unchanged ones.", "Inspect ordered server commands, including unchanged ones."};
	const int aIds[] = {12, 13, 15, 16};
	for(int Section = 0; Section < NUM_SECTIONS; ++Section)
		if(Editor.DoButton_Editor(&m_aButtonIds[aIds[Section]], apSections[Section], m_Section == Section, &aTabs[Section], BUTTONFLAG_LEFT, apTips[Section]))
			SetSection(Section);
	CUIRect Summary;
	View.HSplitTop(24, &Summary, &View);
	Label(Editor, Summary, m_Result.ChangeCount() == 0 ? "No differences in map content. Enable context to inspect matching layers." : std::to_string(m_Result.ChangeCount()) + " changes. - red: Before   + green: After   [?]: uncertain pairing." + (MapView() ? " Drag a canvas to pan; wheel to zoom." : ""), 11);
	const CUIRect ContentView = View;
	const bool SettingsView = m_SelectedEntry >= 0 && m_Result.m_vEntries[m_SelectedEntry].m_Kind == MapDiff::EEntryKind::SETTINGS;
	if(!SettingsView)
		View.HSplitBottom(std::clamp(View.h * 0.36f, 130.0f, 235.0f), &View, &Inspector);
	CUIRect aColumns[2];
	View.VSplitMid(&aColumns[0], &aColumns[1], 8);
	for(int Side = 0; Side < 2; ++Side)
	{
		CUIRect Name, Path;
		aColumns[Side].HSplitTop(20, &Name, &aColumns[Side]);
		aColumns[Side].HSplitTop(26, &Path, &m_aPanes[Side]);
		Label(Editor, Name, std::string(Side == 0 ? "- Before: " : "+ After: ") + SourceName(m_apSources[Side]), 12, Side == 0 ? REMOVE_COLOR : ADD_COLOR);
		Label(Editor, Path, m_apSources[Side]->m_aFilename[0] ? m_apSources[Side]->m_aFilename : "No saved path", 10);
	}
	vec2 Mouse(Editor.Ui()->MouseX(), Editor.Ui()->MouseY());
	const bool Interactive = Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen();
	if(!MapView() || !Interactive)
		m_Panning = false;
	if(Interactive && MapView() && Editor.Ui()->ActiveItem() == nullptr && !m_Panning && (m_aPanes[0].Inside(Mouse) || m_aPanes[1].Inside(Mouse)) && (Editor.Ui()->MouseButtonClicked(0) || Editor.Ui()->MouseButtonClicked(2)))
	{
		m_Panning = true;
		m_LastMouse = Mouse;
	}
	if(m_Panning)
	{
		m_Center -= (Mouse - m_LastMouse) * m_Zoom;
		m_LastMouse = Mouse;
		if(!Editor.Ui()->MouseButton(0) && !Editor.Ui()->MouseButton(2))
			m_Panning = false;
	}
	CUIRect VisibilityBar, ShowAll;
	Navigator.HSplitBottom(NarrowSidebar ? 42 : 24, &Navigator, &VisibilityBar);
	if(Interactive && Editor.Ui()->MouseButtonClicked(0) && (Navigator.Inside(Mouse) || Inspector.Inside(Mouse)))
		m_LayerTreeActive = Navigator.Inside(Mouse);
	VisibilityBar.Margin(2, &VisibilityBar);
	if(NarrowSidebar)
		VisibilityBar.HSplitBottom(20, &VisibilityBar, &ShowAll);
	else
		VisibilityBar.VSplitRight(64, &VisibilityBar, &ShowAll);
	int Hidden = 0;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
		if((m_Result.m_vEntries[i].m_Kind == MapDiff::EEntryKind::LAYER || m_Result.m_vEntries[i].m_Kind == MapDiff::EEntryKind::GROUP) && !LayerVisible(i))
			++Hidden;
	Label(Editor, VisibilityBar, Hidden ? std::to_string(Hidden) + (NarrowSidebar ? " hidden" : " hidden / both maps") : "All layers visible", 9);
	if(Editor.DoButton_Editor(&m_aButtonIds[14], "Show all", Hidden ? 0 : -1, &ShowAll, BUTTONFLAG_LEFT, "Restore all comparison visibility. Source editor visibility is unchanged."))
		for(auto &State : m_vLayerStates)
			State.m_Visible = true;
	if(m_vNavigator.empty() && m_Section != SECTION_LAYERS)
		Label(Editor, Navigator, m_Section == SECTION_ENVELOPES ? "No envelopes on either side." : "No differences in this section.", 10);
	if(m_Section == SECTION_LAYERS)
		RenderLayerTree(Editor, Navigator);
	else if(!m_vNavigator.empty())
	{
		NavigatorList().SetActive(m_LayerTreeActive && Editor.m_Dialog == DIALOG_NONE && !Editor.Ui()->IsPopupOpen());
		int NavSelected = -1;
		for(int i = 0; i < (int)m_vNavigator.size(); ++i)
			if(m_SelectedEntry >= 0 && m_vNavigator[i] == m_SelectedEntry)
				NavSelected = i;
		NavigatorList().DoStart(36, m_vNavigator.size(), 1, 3, NavSelected, &Navigator);
		bool ToggleUnchanged = false;
		for(int Index : m_vNavigator)
		{
			if(Index < 0)
			{
				auto Item = NavigatorList().DoSubheader();
				if(Item.m_Visible)
				{
					Item.m_Rect.Margin(3, &Item.m_Rect);
					const std::string Title = std::string(m_UnchangedExpanded ? "- " : "+ ") + "Unchanged (" + std::to_string(m_UnchangedCount) + ")";
					ToggleUnchanged = Editor.DoButton_Editor(&m_UnchangedButtonId, Title.c_str(), 0, &Item.m_Rect, BUTTONFLAG_LEFT, "Expand or collapse unchanged items. Prev/Next always skip these.");
				}
				continue;
			}
			const auto &Entry = m_Result.m_vEntries[Index];
			auto Item = NavigatorList().DoNextItem(&Entry);
			if(Item.m_Visible)
			{
				CUIRect Name, Count;
				Item.m_Rect.Margin(3, &Item.m_Rect);
				Item.m_Rect.HSplitTop(15, &Name, &Count);
				size_t Changes = Entry.ChangeCount();
				if(m_ByGroup && Entry.m_Kind == MapDiff::EEntryKind::GROUP)
					for(const auto &Child : m_Result.m_vEntries)
						if(Child.m_Parent == Index)
							Changes += Child.ChangeCount();
				std::string RowLabel = EntryLabel(Entry);
				if(Entry.m_Kind == MapDiff::EEntryKind::GROUP && !Entry.Changed() && Changes > 0)
					RowLabel = "-/+ Group: " + EntryName(Entry) + (Entry.m_Ambiguous ? " [?]" : "");
				const ColorRGBA StatusColor = Entry.m_Before < 0 ? ADD_COLOR : Entry.m_After < 0 ? REMOVE_COLOR :
										       Changes > 0               ? CHANGE_COLOR :
														   ColorRGBA(0.75f, 0.75f, 0.75f, 1);
				Label(Editor, Name, RowLabel, 10, StatusColor);
				Editor.UpdateTooltip(&Entry, &Name, (RowLabel + ". " + SourceIndices(Entry)).c_str());
				std::string Text = std::to_string(Changes) + " changes";
				if(!LayerVisible(Index))
					Text = "Hidden / " + Text;
				if(Entry.m_Parent >= 0)
					Text += " / " + m_Result.m_vEntries[Entry.m_Parent].m_Name;
				else if(Entry.m_Kind != MapDiff::EEntryKind::GROUP)
					Text += " / map-wide";
				Label(Editor, Count, Text, 9);
			}
		}
		int NewSelected = NavigatorList().DoEnd();
		if(NewSelected >= 0 && NewSelected != NavSelected && m_vNavigator[NewSelected] < 0)
		{
			const int Direction = NewSelected > NavSelected ? 1 : -1;
			NewSelected += Direction;
			if(NewSelected < 0 || NewSelected >= (int)m_vNavigator.size())
				NewSelected -= 2 * Direction;
			if(NewSelected < 0 || NewSelected >= (int)m_vNavigator.size())
				NewSelected = -1;
		}
		if(ToggleUnchanged)
		{
			m_UnchangedExpanded = !m_UnchangedExpanded;
			RebuildNavigator(true);
			if(m_vNavigator.size() * 36 <= Navigator.h)
				NavigatorList().Reset();
		}
		else if(NewSelected >= 0 && NewSelected != NavSelected)
			Select(m_vNavigator[NewSelected]);
	}
	const bool NowSettingsView = m_SelectedEntry >= 0 && m_Result.m_vEntries[m_SelectedEntry].m_Kind == MapDiff::EEntryKind::SETTINGS;
	if(SettingsView != NowSettingsView)
	{
		// A navigator click can change view kind during this frame.
		View = ContentView;
		Inspector = {};
		if(!NowSettingsView)
			View.HSplitBottom(std::clamp(View.h * 0.36f, 130.0f, 235.0f), &View, &Inspector);
		for(auto &Pane : m_aPanes)
			Pane.h = View.h - 46;
	}
	if(m_FitAfterSettings)
	{
		FitSelection();
		m_FitAfterSettings = false;
	}
	if(NowSettingsView)
		RenderSettings(Editor, {m_aPanes[0].x, m_aPanes[0].y, m_aPanes[1].x + m_aPanes[1].w - m_aPanes[0].x, m_aPanes[0].h});
	else
	{
		if(m_SelectedEntry >= 0 && m_Result.m_vEntries[m_SelectedEntry].m_Kind == MapDiff::EEntryKind::ENVELOPE)
		{
			RenderEnvelope(Editor, m_aPanes[0], 0);
			RenderEnvelope(Editor, m_aPanes[1], 1);
		}
		else
		{
			RenderPane(Editor, m_aPanes[0], 0);
			RenderPane(Editor, m_aPanes[1], 1);
		}
		if(Inspector.h > 0)
			RenderInspector(Editor, Inspector);
	}
}

CMapCompareTabs::~CMapCompareTabs()
{
	for(auto &pDocument : m_vpDocuments)
		if(pDocument->m_pData)
			pDocument->m_pData->m_Cancel = true;
}

void CMapCompareTabs::Retire(CMapCompare &Document)
{
	if(!Document.m_Future.valid())
		return;
	// Only the selected document can launch work, and launching is gated on
	// this slot being empty. A tab transition never destroys a running future.
	dbg_assert(!m_RetiredWork.valid(), "Comparison worker already retiring");
	m_RetiredWork = std::move(Document.m_Future);
}

void CMapCompareTabs::Select(CEditor &Editor, CMapCompare *pDocument)
{
	if(Editor.m_Dialog != DIALOG_NONE || Editor.m_PopupEventActivated || Editor.m_PopupEventWasActivated)
		return;
	if(!m_Transition && pDocument == m_pSelected)
		return;
	if(!m_Transition && !Active())
		Editor.FinishMapInteraction();
	if(m_pSelected)
	{
		m_pSelected->m_Panning = false;
		if(m_pSelected->m_State == CMapCompare::EState::COMPUTING)
		{
			m_pSelected->CancelWork();
			m_pSelected->m_State = CMapCompare::EState::CAPTURE;
		}
	}
	m_pPending = pDocument;
	m_pPendingMap = nullptr;
	m_Transition = true;
	Editor.m_MapTabsRevealSelected = true;
}

void CMapCompareTabs::SelectMap(CEditor &Editor, const CEditorMap *pMap)
{
	if(Editor.m_Dialog != DIALOG_NONE || Editor.m_PopupEventActivated || Editor.m_PopupEventWasActivated)
		return;
	if(!Active() && !m_Transition && Editor.Map() == pMap)
		return;
	// Map-to-map transitions also finish gestures and wait for held input.
	if(!Active() && !m_Transition)
		Editor.FinishMapInteraction();
	Select(Editor, nullptr);
	m_pPending = nullptr;
	m_pPendingMap = pMap;
	m_Transition = true;
	Editor.m_MapTabsRevealSelected = true;
}

void CMapCompareTabs::CloseMap(CEditor &Editor, const CEditorMap *pMap)
{
	if(Editor.m_Dialog != DIALOG_NONE || Editor.m_PopupEventActivated || Editor.m_PopupEventWasActivated)
		return;
	if(!Active() && !m_Transition)
		Editor.FinishMapInteraction();
	// Dirty maps need their own editing context for the save/discard dialog.
	// Closing a clean background map keeps the active document selected.
	if(pMap->m_Modified)
		SelectMap(Editor, pMap);
	if(!m_Transition)
	{
		m_pPending = m_pSelected;
		m_pPendingMap = Active() ? nullptr : Editor.Map();
	}
	m_pCloseMap = pMap;
	m_Transition = true;
	Editor.m_MapTabsRevealSelected = true;
}

void CMapCompareTabs::OpenSelector(CEditor &Editor)
{
	if(Editor.m_vpMaps.size() < 2 || (Editor.m_Dialog != DIALOG_NONE || Editor.m_PopupEventActivated || Editor.m_PopupEventWasActivated))
		return;
	for(auto &pDocument : m_vpDocuments)
		if(pDocument->m_State == CMapCompare::EState::SELECT)
		{
			Select(Editor, pDocument.get());
			return;
		}
	auto pDocument = std::make_unique<CMapCompare>();
	pDocument->m_pOwner = this;
	pDocument->OpenSelector(Editor);
	auto *pSelect = pDocument.get();
	m_vpDocuments.push_back(std::move(pDocument));
	Select(Editor, pSelect);
}

void CMapCompareTabs::Open(CEditor &Editor, const CEditorMap *pBefore, const CEditorMap *pAfter)
{
	if(Editor.m_Dialog != DIALOG_NONE || Editor.m_PopupEventActivated || Editor.m_PopupEventWasActivated)
		return;
	for(auto &pDocument : m_vpDocuments)
		if(pDocument->m_State != CMapCompare::EState::SELECT && pDocument->m_State != CMapCompare::EState::CLOSING && pDocument->m_apSources[0] == pBefore && pDocument->m_apSources[1] == pAfter)
		{
			Select(Editor, pDocument.get());
			for(auto &pOther : m_vpDocuments)
				if(pOther->m_Selector)
					pOther->Close();
			return;
		}
	auto pDocument = std::make_unique<CMapCompare>();
	pDocument->m_pOwner = this;
	pDocument->Open(Editor, pBefore, pAfter);
	if(!pDocument->Active())
		return;
	auto *pSelect = pDocument.get();
	m_vpDocuments.push_back(std::move(pDocument));
	Select(Editor, pSelect);
	// The selector is transient; its replacement is applied at the frame boundary.
	for(auto &pOther : m_vpDocuments)
		if(pOther->m_State == CMapCompare::EState::SELECT)
			pOther->Close();
}

void CMapCompareTabs::Dispose(CEditor &Editor, CMapCompare *pDocument, bool Remember)
{
	if(Remember && !pDocument->m_Selector && pDocument->ValidSources(Editor))
	{
		pDocument->SaveView();
		m_vClosed.push_back({{pDocument->m_apSources[0], pDocument->m_apSources[1]}, pDocument->m_SavedView,
			pDocument->m_Center, pDocument->m_ParallaxOrigin, pDocument->m_Zoom,
			pDocument->m_Context, pDocument->m_Section, pDocument->m_EnvelopeChannels, pDocument->m_ByGroup, pDocument->m_UnchangedExpanded, pDocument->m_HasResult, pDocument->m_LayerTreeActive});
		if(m_vClosed.size() > 10)
			m_vClosed.erase(m_vClosed.begin());
	}
	pDocument->CancelWork();
	if(m_pSelected == pDocument)
		m_pSelected = nullptr;
	if(m_pPending == pDocument)
		m_pPending = nullptr;
	m_vpDocuments.erase(std::find_if(m_vpDocuments.begin(), m_vpDocuments.end(), [&](const auto &pOther) { return pOther.get() == pDocument; }));
}

void CMapCompareTabs::SourceClosing(CEditor &Editor, const CEditorMap *pSource)
{
	if(m_pPendingMap == pSource)
		m_pPendingMap = nullptr;
	if(m_pCloseMap == pSource)
		m_pCloseMap = nullptr;
	for(size_t i = 0; i < m_vpDocuments.size();)
	{
		auto *pDocument = m_vpDocuments[i].get();
		if(pDocument->m_apSources[0] == pSource || pDocument->m_apSources[1] == pSource)
		{
			if(pDocument == m_pSelected)
				m_Transition = true;
			Dispose(Editor, pDocument, false);
		}
		else
			++i;
	}
	m_vClosed.erase(std::remove_if(m_vClosed.begin(), m_vClosed.end(), [&](const CClosed &Closed) { return Closed.m_apSources[0] == pSource || Closed.m_apSources[1] == pSource; }), m_vClosed.end());
}

void CMapCompareTabs::Reopen(CEditor &Editor)
{
	if(m_vClosed.empty() || (Editor.m_Dialog != DIALOG_NONE || Editor.m_PopupEventActivated || Editor.m_PopupEventWasActivated))
		return;
	auto Closed = std::move(m_vClosed.back());
	m_vClosed.pop_back();
	Open(Editor, Closed.m_apSources[0], Closed.m_apSources[1]);
	auto *pDocument = m_pPending;
	if(!pDocument || pDocument->m_HasResult)
		return;
	pDocument->m_SavedView = std::move(Closed.m_View);
	pDocument->m_HasResult = Closed.m_HasResult;
	pDocument->m_Center = Closed.m_Center;
	pDocument->m_ParallaxOrigin = Closed.m_Origin;
	pDocument->m_Zoom = Closed.m_Zoom;
	pDocument->m_Context = Closed.m_Context;
	pDocument->m_Section = Closed.m_Section;
	pDocument->m_EnvelopeChannels = Closed.m_EnvelopeChannels;
	pDocument->m_ByGroup = Closed.m_ByGroup;
	pDocument->m_UnchangedExpanded = Closed.m_UnchangedExpanded;
	pDocument->m_LayerTreeActive = Closed.m_LayerTreeActive;
}

void CMapCompareTabs::OnUpdate(CEditor &Editor)
{
	if(m_RetiredWork.valid() && m_RetiredWork.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		try
		{
			m_RetiredWork.get();
		}
		catch(const std::exception &)
		{
		}
	}
	if(m_pSelected && !m_Transition && !m_RetiredWork.valid())
		m_pSelected->OnUpdate(Editor);
}

void CMapCompareTabs::OnInput(CEditor &Editor, const IInput::CEvent &Event)
{
	if(m_Transition)
		return;
	if(m_pSelected)
		m_pSelected->OnInput(Editor, Event);
}

void CMapCompareTabs::Render(CEditor &Editor, CUIRect View, CUIRect Toolbar, CUIRect Sidebar)
{
	if(m_Transition)
		Label(Editor, View, "Release held keys and mouse buttons to switch tabs.", 14);
	else if(m_pSelected)
		m_pSelected->Render(Editor, View, Toolbar, Sidebar);
}

void CMapCompareTabs::EndFrame(CEditor &Editor)
{
	for(const auto &pDocument : m_vpDocuments)
		if(!m_Transition && pDocument.get() == m_pSelected && pDocument->m_State == CMapCompare::EState::CLOSING)
			SelectMap(Editor, Editor.Map());
	if(m_Transition)
	{
		for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
			if(Editor.Input()->KeyIsPressed(Key))
				return;
		Editor.Reset();
		Editor.Ui()->SetHotItem(nullptr);
		if(CLineInput::GetActiveInput())
			CLineInput::GetActiveInput()->Deactivate();
		m_pSelected = m_pPending;
		if(m_pSelected && m_pSelected->m_State == CMapCompare::EState::READY && !m_pSelected->Current(Editor))
		{
			m_pSelected->SaveView();
			m_pSelected->m_State = CMapCompare::EState::CAPTURE;
		}
		if(m_pPendingMap)
			for(size_t i = 0; i < Editor.m_vpMaps.size(); ++i)
				if(Editor.m_vpMaps[i].get() == m_pPendingMap)
					Editor.m_SelectedMap = i;
		m_pPending = nullptr;
		m_pPendingMap = nullptr;
		m_Transition = false;
		Editor.m_MapTabsRevealSelected = true;
		if(m_pCloseMap)
		{
			const auto *pClose = m_pCloseMap;
			m_pCloseMap = nullptr;
			for(size_t i = 0; i < Editor.m_vpMaps.size(); ++i)
				if(Editor.m_vpMaps[i].get() == pClose)
				{
					Editor.CloseMap(i, true);
					break;
				}
		}
	}
	for(size_t i = 0; i < m_vpDocuments.size();)
	{
		if(m_vpDocuments[i]->m_State == CMapCompare::EState::CLOSING)
			Dispose(Editor, m_vpDocuments[i].get(), true);
		else
			++i;
	}
}
