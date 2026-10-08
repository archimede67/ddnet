#include "map_compare.h"

#include "editor.h"
#include "map_diff_snapshot.h"

#include <base/str.h>

#include <engine/keys.h>
#include <engine/textrender.h>

#include <game/client/lineinput.h>
#include <game/editor/mapitems/image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace
{
	constexpr ColorRGBA REMOVE_COLOR(1.0f, 0.3f, 0.3f, 1.0f);
	constexpr ColorRGBA ADD_COLOR(0.25f, 1.0f, 0.45f, 1.0f);
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
		return std::string(Entry.Status()) + " " + pKind + Entry.m_Name + (Entry.m_Ambiguous ? " [?]" : "");
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
		auto It = std::lower_bound(Cells.begin(), Cells.end(), (int)std::floor(Visible.m_TopLeft.y / 32) - 1, [](const auto &Cell, int Y) { return Cell.m_Y < Y; });
		for(; It != Cells.end() && It->m_Y * 32 <= Visible.m_BottomRight.y; ++It)
		{
			const auto &Cell = *It;
			if(!Cell.m_Tile.m_Index || Cell.m_X * 32 + 32 < Visible.m_TopLeft.x || Cell.m_X * 32 > Visible.m_BottomRight.x)
				continue;
			const auto &T = Cell.m_Tile;
			const float X = (T.m_Index % 16) / 16.0f, Y = (T.m_Index / 16) / 16.0f;
			vec2 aUV[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
			for(auto &UV : aUV)
			{
				if(T.m_Flags & TILEFLAG_XFLIP)
					UV.x = 1 - UV.x;
				if(T.m_Flags & TILEFLAG_YFLIP)
					UV.y = 1 - UV.y;
			}
			if(T.m_Flags & TILEFLAG_ROTATE)
				std::rotate(std::begin(aUV), std::end(aUV) - 1, std::end(aUV));
			if(!Arrays)
				for(auto &UV : aUV)
					UV = vec2(X, Y) + vec2(0.0005f, 0.0005f) + UV * (1.0f / 16 - 0.001f);
			pGraphics->QuadsSetSubsetFree(aUV[0].x, aUV[0].y, aUV[1].x, aUV[1].y, aUV[2].x, aUV[2].y, aUV[3].x, aUV[3].y, Arrays ? T.m_Index : -1);
			IGraphics::CQuadItem Quad(Cell.m_X * 32.0f, Cell.m_Y * 32.0f, 32, 32);
			if(Arrays)
				pGraphics->QuadsTex3DDrawTL(&Quad, 1);
			else
				pGraphics->QuadsDrawTL(&Quad, 1);
		}
		if(Arrays)
			pGraphics->QuadsTex3DEnd();
		else
			pGraphics->QuadsEnd();
	}
	void Marker(CEditor &Editor, float X, float Y, float W, float H, int Side, float Zoom, ColorRGBA Color)
	{
		CUIRect Rect{X, Y, std::max(W, 2.0f * Zoom), std::max(H, 2.0f * Zoom)};
		Rect.DrawOutline(Color);
		const float Size = std::min(10.0f * Zoom, 10.0f);
		Editor.Graphics()->TextureClear();
		Editor.Graphics()->LinesBegin();
		Editor.Graphics()->SetColor(Color);
		IGraphics::CLineItem aLines[2] = {{X + 2, Y + 2 + Size / 2, X + 2 + Size, Y + 2 + Size / 2}, {X + 2 + Size / 2, Y + 2, X + 2 + Size / 2, Y + 2 + Size}};
		Editor.Graphics()->LinesDraw(aLines, Side == 0 ? 1 : 2);
		Editor.Graphics()->LinesEnd();
	}
}

CMapCompare::~CMapCompare()
{
	if(m_pData)
		m_pData->m_Cancel = true;
}
void CMapCompare::CancelWork()
{
	if(m_pData)
		m_pData->m_Cancel = true;
	if(m_Future.valid())
		m_vRetiredWork.push_back(std::move(m_Future));
}
void CMapCompare::ReleaseBuffers()
{
	m_pData.reset();
	m_Result = {};
	std::vector<CDetail>().swap(m_vDetails);
	std::vector<MapDiff::CPropertyChange>().swap(m_vValueRows);
	std::vector<int>().swap(m_vNavigator);
	m_SelectedEntry = m_SelectedDetail = m_FocusEntry = -1;
	m_ValueRowsEntry = m_ValueRowsDetail = -2;
}
vec2 CMapCompare::ProjectionCenter(const CLayerGroup &Group) const
{
	// Parallax is evaluated at one shared reference position. Independent
	// inspection translation allows even zero-parallax axes to be panned.
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
	m_Message.clear();
	Editor.Ui()->SetActiveItem(nullptr);
	if(CLineInput::GetActiveInput())
		CLineInput::GetActiveInput()->Deactivate();
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
	m_Context = false;
	m_Panning = false;
	Editor.Ui()->SetActiveItem(nullptr);
	if(CLineInput::GetActiveInput())
		CLineInput::GetActiveInput()->Deactivate();
}
void CMapCompare::SourceClosing(const CEditorMap *pSource)
{
	if(Active() && (m_apSources[0] == pSource || m_apSources[1] == pSource))
		Invalidate("A comparison source became unavailable because its tab was closed. Return to the editor to choose another pair.");
}
void CMapCompare::Invalidate(const char *pReason)
{
	CancelWork();
	ReleaseBuffers();
	m_apSources[0] = m_apSources[1] = nullptr;
	m_Message = pReason;
	m_State = EState::UNAVAILABLE;
}
void CMapCompare::Close()
{
	CancelWork();
	ReleaseBuffers();
	m_State = EState::CLOSING;
}
void CMapCompare::EndFrame(CEditor &Editor)
{
	if(m_State != EState::CLOSING)
		return;
	// A held button or repeating shortcut must not start editing on return.
	for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
		if(Editor.Input()->KeyIsPressed(Key))
			return;
	Editor.Ui()->SetActiveItem(nullptr);
	Editor.Ui()->SetHotItem(nullptr);
	Editor.Ui()->ClosePopupMenus();
	m_State = EState::CLOSED;
	m_apSources[0] = m_apSources[1] = nullptr;
	m_Panning = false;
}
void CMapCompare::OnInput(CEditor &Editor, const IInput::CEvent &Event)
{
	// Text, editing shortcuts, and editor components receive no events.
	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			Close();
			return;
		}
		if(m_State == EState::READY)
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
			if(m_aPanes[0].Inside(Mouse) || m_aPanes[1].Inside(Mouse))
			{
				if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
					m_Zoom = std::max(0.05f, m_Zoom / 1.2f);
				if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
					m_Zoom = std::min(4096.0f, m_Zoom * 1.2f);
				if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
					return;
			}
		}
	}
	if(!(Event.m_Flags & IInput::FLAG_TEXT))
		Editor.Ui()->OnInput(Event);
}
void CMapCompare::Start(CEditor &Editor)
{
	if(!ValidSources(Editor))
	{
		Invalidate("A comparison source is no longer open.");
		return;
	}
	if(m_pData)
		m_pData->m_Cancel = true;
	m_pData = std::make_shared<CData>();
	// Capture is a const read on the UI thread; only owned values cross to the
	// worker. Autosave and source disposal can therefore never race a capture.
	MapDiff::Capture(*m_apSources[0], m_pData->m_aSnapshots[0]);
	MapDiff::Capture(*m_apSources[1], m_pData->m_aSnapshots[1]);
	const auto pData = m_pData;
	m_Future = std::async(std::launch::async, [pData] {
		return MapDiff::Compare(pData->m_aSnapshots[0], pData->m_aSnapshots[1], [pData] { return pData->m_Cancel.load(); });
	});
	m_State = EState::COMPUTING;
	m_SelectedEntry = m_SelectedDetail = m_FocusEntry = -1;
	m_vDetails.clear();
	m_vValueRows.clear();
	m_ValueRowsEntry = m_ValueRowsDetail = -2;
	m_Navigator.Reset();
	m_Details.Reset();
	m_Values.Reset();
	Editor.Ui()->ClosePopupMenus();
}
void CMapCompare::OnUpdate(CEditor &Editor)
{
	for(auto It = m_vRetiredWork.begin(); It != m_vRetiredWork.end();)
	{
		if(It->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			try
			{
				It->get();
			}
			catch(const std::exception &)
			{
			}
			It = m_vRetiredWork.erase(It);
		}
		else
			++It;
	}
	if(m_State == EState::CAPTURE && m_vRetiredWork.empty())
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
	if((m_State == EState::COMPUTING || m_State == EState::READY) && !ValidSources(Editor))
		Invalidate("A comparison source is no longer open.");
	if(m_State == EState::COMPUTING && m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		try
		{
			m_Result = m_Future.get();
			if(m_Result.m_Cancelled)
			{
				Close();
				return;
			}
			m_State = EState::READY;
			RebuildNavigator();
			if(!m_vNavigator.empty())
				Select(m_vNavigator.front());
		}
		catch(const std::exception &Exception)
		{
			Invalidate((std::string("Comparison failed: ") + Exception.what()).c_str());
		}
	}
}
void CMapCompare::RebuildNavigator()
{
	m_vNavigator.clear();
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		bool Changed = Entry.Changed();
		if(Entry.m_Kind == MapDiff::EEntryKind::GROUP)
			for(const auto &Child : m_Result.m_vEntries)
				if(Child.m_Parent == i && Child.Changed())
					Changed = true;
		if(!m_Context && !Changed)
			continue;
		if(m_ByGroup && Entry.m_Kind == MapDiff::EEntryKind::LAYER)
			continue;
		// In layer mode retain changed group properties in the navigator, too.
		if(!m_ByGroup && Entry.m_Kind == MapDiff::EEntryKind::GROUP && !Entry.Changed())
			continue;
		m_vNavigator.push_back(i);
	}
	if(std::find(m_vNavigator.begin(), m_vNavigator.end(), m_SelectedEntry) == m_vNavigator.end())
	{
		int Replacement = -1;
		if(std::find(m_vNavigator.begin(), m_vNavigator.end(), m_FocusEntry) != m_vNavigator.end())
			Replacement = m_FocusEntry;
		if(Replacement < 0 && !m_vNavigator.empty())
			Replacement = m_vNavigator.front();
		Select(Replacement);
	}
}
void CMapCompare::Select(int Entry)
{
	m_SelectedEntry = m_FocusEntry = Entry;
	m_SelectedDetail = -1;
	m_Details.Reset();
	m_Values.Reset();
	m_ValueRowsEntry = m_ValueRowsDetail = -2;
	RebuildDetails();
	FitSelection();
}
void CMapCompare::RebuildDetails()
{
	m_vDetails.clear();
	if(m_SelectedEntry < 0)
		return;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(i != m_SelectedEntry && Entry.m_Parent != m_SelectedEntry)
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
	if(!m_vDetails.empty())
		m_SelectedDetail = 0;
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
			Values.push_back({"Matching", "Ambiguous content/ordered match", "Historical identity is unknown"});
		return Values;
	}
	if(Detail.m_Kind == 3)
		return {{"Content unavailable / invalid reference", Entry.m_vIssues[Detail.m_Index], Entry.m_vIssues[Detail.m_Index]}};
	std::vector<MapDiff::CPropertyChange> Values{{"Structure (zero-based index)", Entry.m_Before < 0 ? "(absent)" : std::to_string(Entry.m_Before), Entry.m_After < 0 ? "(absent)" : std::to_string(Entry.m_After)}};
	if(Entry.m_Reordered)
		Values.push_back({"Draw order", "Previous relative order", "Changed relative order among matched items"});
	if(Entry.m_Ambiguous)
		Values.push_back({"Matching", "Ambiguous content/ordered match", "Historical identity is unknown"});
	return Values;
}
void CMapCompare::Navigate(int Direction)
{
	if(m_vNavigator.empty())
		return;
	if(m_SelectedDetail >= 0 && m_SelectedDetail + Direction >= 0 && m_SelectedDetail + Direction < (int)m_vDetails.size())
	{
		m_SelectedDetail += Direction;
		m_FocusEntry = m_vDetails[m_SelectedDetail].m_Entry;
		m_Details.ScrollToSelected();
	}
	else
	{
		auto It = std::find(m_vNavigator.begin(), m_vNavigator.end(), m_SelectedEntry);
		int Index = It == m_vNavigator.end() ? 0 : It - m_vNavigator.begin();
		Index = (Index + Direction + m_vNavigator.size()) % m_vNavigator.size();
		Select(m_vNavigator[Index]);
		if(Direction < 0 && !m_vDetails.empty())
			m_SelectedDetail = m_vDetails.size() - 1;
		m_Navigator.ScrollToSelected();
	}
	m_Values.Reset();
	FitSelection();
}
void CMapCompare::FitSelection()
{
	if(m_SelectedEntry < 0 || !m_pData)
		return;
	float Left = std::numeric_limits<float>::max(), Top = Left, Right = -Left, Bottom = -Left;
	auto Extend = [&](const MapDiff::CEntry &Entry, int Side, std::array<float, 4> Bounds) {
		int G = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
		if(G < 0 || !m_apSources[Side])
			return;
		const auto &Group = *m_apSources[Side]->m_vpGroups[G];
		const vec2 Origin = ProjectionCenter(Group) - m_Center;
		float X0 = Bounds[0] - Origin.x, X1 = Bounds[2] - Origin.x;
		float Y0 = Bounds[1] - Origin.y, Y1 = Bounds[3] - Origin.y;
		Left = std::min(Left, std::min(X0, X1));
		Top = std::min(Top, std::min(Y0, Y1));
		Right = std::max(Right, std::max(X0, X1));
		Bottom = std::max(Bottom, std::max(Y0, Y1));
	};
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(i != m_SelectedEntry && Entry.m_Parent != m_SelectedEntry)
			continue;
		if(m_SelectedDetail >= 0)
		{
			const auto &Detail = m_vDetails[m_SelectedDetail];
			if((Detail.m_Kind == 1 || Detail.m_Kind == 2) && Detail.m_Entry != i)
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
				Extend(Entry, Side, {0, 0, pLayer->m_Width * 32.0f, pLayer->m_Height * 32.0f});
			for(int O = 0; O < (int)pLayer->m_vObjects.size(); ++O)
				Extend(Entry, Side, ObjectBounds(*m_apSources[Side], SourceLayer, O, pLayer->m_vObjects[O].m_aBounds));
		}
	}
	if(Left <= Right && Top <= Bottom)
	{
		m_Center = vec2((Left + Right) / 2, (Top + Bottom) / 2);
		m_Zoom = std::clamp(std::max((Right - Left + 64) / std::max(200.0f, m_aPanes[0].w), (Bottom - Top + 64) / std::max(160.0f, m_aPanes[0].h)), 0.05f, 4096.0f);
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
		m_aPairLists[Side].DoStart(42, Editor.m_vpMaps.size(), 1, 3, Selected, &aSides[Side]);
		for(const auto &pMap : Editor.m_vpMaps)
		{
			auto Item = m_aPairLists[Side].DoNextItem(Side == 0 ? &pMap->m_TabSelectButtonId : &pMap->m_TabCloseButtonId);
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
		Open(Editor, m_apSources[0], m_apSources[1]);
}

void CMapCompare::RenderInspector(CEditor &Editor, CUIRect View)
{
	View.Draw(ColorRGBA(0.12f, 0.12f, 0.14f, 1), IGraphics::CORNER_NONE, 0);
	View.Margin(4, &View);
	CUIRect List, Values;
	View.VSplitLeft(std::min(270.0f, View.w * 0.34f), &List, &Values);
	Values.VMargin(5, &Values);
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
			Label(Editor, Item.m_Rect, Entry.m_Name + ": " + Name, 10);
		}
	}
	int Selected = m_Details.DoEnd();
	if(Selected != m_SelectedDetail && Selected >= 0)
	{
		m_SelectedDetail = Selected;
		m_FocusEntry = m_vDetails[Selected].m_Entry;
		m_Values.Reset();
	}
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
	std::vector<int> vLayers;
	for(int i = 0; i < (int)m_Result.m_vEntries.size(); ++i)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		if(Entry.m_Kind == MapDiff::EEntryKind::LAYER && (i == m_SelectedEntry || Entry.m_Parent == m_SelectedEntry))
			vLayers.push_back(i);
	}
	std::sort(vLayers.begin(), vLayers.end(), [&](int A, int B) {
		return Side == 0 ? m_Result.m_vEntries[A].m_Before < m_Result.m_vEntries[B].m_Before : m_Result.m_vEntries[A].m_After < m_Result.m_vEntries[B].m_After;
	});
	for(int i : vLayers)
	{
		const auto &Entry = m_Result.m_vEntries[i];
		const auto *pLayer = Layer(Snapshot, Entry, Side);
		if(!pLayer)
			continue;
		int G = Side == 0 ? Entry.m_BeforeGroup : Entry.m_AfterGroup;
		int L = Side == 0 ? Entry.m_Before : Entry.m_After;
		const auto &Group = *Map.m_vpGroups[G];
		const auto &SourceLayer = *Group.m_vpLayers[L];
		const vec2 GroupCenter = ProjectionCenter(Group);
		const vec2 TopLeft = GroupCenter - Pane.Size() * m_Zoom / 2;
		const CScreenRect Visible(TopLeft, TopLeft + Pane.Size() * m_Zoom);
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
		Editor.Ui()->ClipEnable(&Clip);
		pGraphics->MapScreen(CScreenRect(FullTopLeft, FullTopLeft + Editor.Ui()->Screen()->Size() * m_Zoom));
		pGraphics->BlendNormal();
		const bool WholeLayer = Entry.m_Before < 0 || Entry.m_After < 0 || Entry.m_Reordered || !Entry.m_vProperties.empty() || Selected.m_Reordered || (Selected.m_Kind == MapDiff::EEntryKind::GROUP && !Selected.m_vProperties.empty());
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
			if(m_Context || WholeLayer)
				DrawTiles(Editor, pLayer->m_vCells, Visible, WholeLayer ? Color.Multiply(Highlight) : Color.Multiply(ColorRGBA(1, 1, 1, 0.35f)));
			std::vector<MapDiff::CCell> vVisibleChanges;
			auto ChangeIt = std::lower_bound(Entry.m_vTiles.begin(), Entry.m_vTiles.end(), (int)std::floor(Visible.m_TopLeft.y / 32), [](const auto &Cell, int Y) { return Cell.m_Y < Y; });
			for(; ChangeIt != Entry.m_vTiles.end() && ChangeIt->m_Y * 32 <= Visible.m_BottomRight.y; ++ChangeIt)
			{
				const auto &Change = *ChangeIt;
				if(Change.m_X * 32 + 32 >= Visible.m_TopLeft.x && Change.m_X * 32 <= Visible.m_BottomRight.x && Change.m_Y * 32 + 32 >= Visible.m_TopLeft.y && Change.m_Y * 32 <= Visible.m_BottomRight.y)
					vVisibleChanges.push_back({Change.m_X, Change.m_Y, Side == 0 ? Change.m_Before : Change.m_After});
			}
			if(!WholeLayer)
				DrawTiles(Editor, vVisibleChanges, Visible, Color.Multiply(Highlight));
			for(const auto &Cell : vVisibleChanges)
				if(!(Cell.m_Tile == MapDiff::CTile{}))
					Marker(Editor, Cell.m_X * 32.0f, Cell.m_Y * 32.0f, 32, 32, Side, m_Zoom, Highlight);
			if(WholeLayer)
				Marker(Editor, 0, 0, pLayer->m_Width * 32.0f, pLayer->m_Height * 32.0f, Side, m_Zoom, Highlight);
			// Source overlay functions are read-only and include number, delay,
			// force, max speed and direction. Restrict them to visible content.
			if(m_Zoom <= 4 && (m_Context || WholeLayer || !vVisibleChanges.empty()))
			{
				const auto &Cells = m_Context || WholeLayer ? pLayer->m_vCells : vVisibleChanges;
				auto CellIt = std::lower_bound(Cells.begin(), Cells.end(), (int)std::floor(Visible.m_TopLeft.y / 32), [](const auto &Cell, int Y) { return Cell.m_Y < Y; });
				for(; CellIt != Cells.end() && CellIt->m_Y * 32 <= Visible.m_BottomRight.y; ++CellIt)
				{
					const auto &Cell = *CellIt;
					vec2 Pos(Cell.m_X * 32.0f, Cell.m_Y * 32.0f);
					if(!Visible.Inside(Pos) || pLayer->m_Kind == MapDiff::ELayerKind::TILES || pLayer->m_Kind == MapDiff::ELayerKind::GAME || pLayer->m_Kind == MapDiff::ELayerKind::FRONT)
						continue;
					std::string Text = pLayer->m_Kind == MapDiff::ELayerKind::SPEEDUP ? std::to_string(Cell.m_Tile.m_Force) + "/" + std::to_string(Cell.m_Tile.m_MaxSpeed) + "\n" + std::to_string(Cell.m_Tile.m_Angle) + " deg" : std::to_string(Cell.m_Tile.m_Number);
					if(pLayer->m_Kind == MapDiff::ELayerKind::SWITCH)
						Text += "/" + std::to_string(Cell.m_Tile.m_Delay);
					Editor.TextRender()->Text(Pos.x + 2, Pos.y + 18, 7, Text.c_str(), 32);
				}
			}
		}
		else if(SourceLayer.m_Type == LAYERTYPE_QUADS)
		{
			const auto &Quads = static_cast<const CLayerQuads &>(SourceLayer);
			std::vector<bool> vChanged(Quads.m_vQuads.size(), WholeLayer);
			for(const auto &Change : Entry.m_vObjects)
			{
				int Index = Side == 0 ? Change.m_Before : Change.m_After;
				if(Index >= 0)
					vChanged[Index] = true;
			}
			for(size_t q = 0; q < Quads.m_vQuads.size(); ++q)
			{
				if(!vChanged[q] && !m_Context)
					continue;
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
						Color.a = (int)(Color.a * 0.35f);
				}
				if(Quads.m_Image >= 0 && Quads.m_Image < (int)Map.m_vpImages.size())
					pGraphics->TextureSet(Map.m_vpImages[Quads.m_Image]->m_Texture);
				else
					pGraphics->TextureClear();
				Editor.RenderMap()->ForceRenderQuads(&Quad, 1, LAYERRENDERFLAG_TRANSPARENT, &Envelope);
				if(vChanged[q])
				{
					const auto B = ObjectBounds(Map, SourceLayer, q, pLayer->m_vObjects[q].m_aBounds);
					Marker(Editor, B[0], B[1], B[2] - B[0], B[3] - B[1], Side, m_Zoom, Highlight);
				}
			}
		}
		else if(SourceLayer.m_Type == LAYERTYPE_SOUNDS)
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
}

void CMapCompare::Render(CEditor &Editor)
{
	Editor.Graphics()->Clear(0.08f, 0.08f, 0.1f);
	Editor.Ui()->MapScreen();
	CUIRect View = *Editor.Ui()->Screen();
	View.Margin(6, &View);
	CUIRect Toolbar;
	View.HSplitTop(23, &Toolbar, &View);
	auto Button = [&](int Id, const char *pText, float Width, int Checked = 0) {
		CUIRect Rect;
		Toolbar.VSplitLeft(Width, &Rect, &Toolbar);
		Rect.VMargin(2, &Rect);
		return Editor.DoButton_Editor(&m_aButtonIds[Id], pText, Checked, &Rect, BUTTONFLAG_LEFT, pText) != 0;
	};
	if(Button(0, "Back [Esc]", 76))
		Close();
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
	if(m_State == EState::CAPTURE || m_State == EState::COMPUTING)
	{
		Label(Editor, View, "Comparing current map contents... Back / Escape cancels.", 14);
		return;
	}
	if(Button(1, "Swap", 52))
	{
		std::swap(m_apSources[0], m_apSources[1]);
		m_State = EState::CAPTURE;
		return;
	}
	if(Button(2, m_ByGroup ? "By group" : "By layer", 74))
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
	if(Button(3, m_Context ? "Context: on" : "Context: off", 84, m_Context))
	{
		m_Context = !m_Context;
		RebuildNavigator();
		if(m_SelectedEntry < 0 && !m_vNavigator.empty())
			Select(m_vNavigator.front());
	}
	if(Button(4, "Prev [P]", 65))
		Navigate(-1);
	if(Button(5, "Next [N]", 65))
		Navigate(1);
	if(Button(6, "Fit [Home]", 76))
		FitSelection();
	Label(Editor, Toolbar, "READ ONLY", 11);
	CUIRect Summary;
	View.HSplitTop(24, &Summary, &View);
	Label(Editor, Summary, m_Result.ChangeCount() == 0 ? "No differences in map content. Enable context to inspect matching layers." : std::to_string(m_Result.ChangeCount()) + " changes. - red: Before   + green: After   [?]: ambiguous pairing. Drag a canvas to pan; wheel to zoom.", 11);
	CUIRect Navigator, Inspector;
	View.VSplitLeft(std::min(225.0f, View.w * 0.25f), &Navigator, &View);
	View.VMargin(4, &View);
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
	if(!m_Panning && (m_aPanes[0].Inside(Mouse) || m_aPanes[1].Inside(Mouse)) && (Editor.Ui()->MouseButtonClicked(0) || Editor.Ui()->MouseButtonClicked(2)))
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
	int NavSelected = -1;
	for(int i = 0; i < (int)m_vNavigator.size(); ++i)
		if(m_vNavigator[i] == m_SelectedEntry)
			NavSelected = i;
	m_Navigator.DoStart(36, m_vNavigator.size(), 1, 3, NavSelected, &Navigator);
	for(int Index : m_vNavigator)
	{
		const auto &Entry = m_Result.m_vEntries[Index];
		auto Item = m_Navigator.DoNextItem(&Entry);
		if(Item.m_Visible)
		{
			CUIRect Name, Count;
			Item.m_Rect.Margin(3, &Item.m_Rect);
			Item.m_Rect.HSplitTop(15, &Name, &Count);
			size_t Changes = Entry.ChangeCount();
			if(Entry.m_Kind == MapDiff::EEntryKind::GROUP)
				for(const auto &Child : m_Result.m_vEntries)
					if(Child.m_Parent == Index)
						Changes += Child.ChangeCount();
			std::string RowLabel = EntryLabel(Entry);
			if(Entry.m_Kind == MapDiff::EEntryKind::GROUP && !Entry.Changed() && Changes > 0)
				RowLabel = "-/+ Group: " + Entry.m_Name + (Entry.m_Ambiguous ? " [?]" : "");
			Label(Editor, Name, RowLabel, 10);
			std::string Text = std::to_string(Changes) + " changes";
			if(Entry.m_Parent >= 0)
				Text += " / " + m_Result.m_vEntries[Entry.m_Parent].m_Name;
			else if(Entry.m_Kind != MapDiff::EEntryKind::GROUP)
				Text += " / map-wide";
			Label(Editor, Count, Text, 9);
		}
	}
	int NewSelected = m_Navigator.DoEnd();
	if(NewSelected >= 0 && NewSelected != NavSelected)
		Select(m_vNavigator[NewSelected]);
	RenderPane(Editor, m_aPanes[0], 0);
	RenderPane(Editor, m_aPanes[1], 1);
	RenderInspector(Editor, Inspector);
}
