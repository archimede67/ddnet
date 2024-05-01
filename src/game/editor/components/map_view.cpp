#include "map_view.h"

#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/render.h>
#include <game/client/ui.h>

#include <game/editor/editor.h>

void CMapView::Init(CEditor *pEditor)
{
	CEditorComponent::Init(pEditor);
	RegisterSubComponent(m_MapGrid);
	RegisterSubComponent(m_ProofMode);
	InitSubComponents();
}

void CMapView::OnReset()
{
	m_Zoom = CSmoothValue(200.0f, 10.0f, 2000.0f);
	m_Zoom.Init(Editor());
	m_WorldZoom = 1.0f;

	SetWorldOffset({0, 0});
	SetEditorOffset({0, 0});

	m_ProofMode.OnReset();
	m_MapGrid.OnReset();
	m_ShowPicker = false;
}

void CMapView::OnMapLoad()
{
	m_ProofMode.OnMapLoad();
}

bool CMapView::IsFocused()
{
	if(m_ProofMode.m_ProofBorders == CProofMode::PROOF_BORDER_MENU)
		return GetWorldOffset() == m_ProofMode.m_vMenuBackgroundPositions[m_ProofMode.m_CurrentMenuProofIndex];
	else
		return GetWorldOffset() == vec2(0, 0);
}

void CMapView::Focus()
{
	if(m_ProofMode.m_ProofBorders == CProofMode::PROOF_BORDER_MENU)
		SetWorldOffset(m_ProofMode.m_vMenuBackgroundPositions[m_ProofMode.m_CurrentMenuProofIndex]);
	else
		SetWorldOffset({0, 0});
}

void CMapView::RenderGroupBorder()
{
	std::shared_ptr<CLayerGroup> pGroup = Editor()->GetSelectedGroup();
	if(pGroup)
	{
		pGroup->MapScreen();

		for(size_t i = 0; i < Editor()->m_vSelectedLayers.size(); i++)
		{
			std::shared_ptr<CLayer> pLayer = Editor()->GetSelectedLayerType(i, LAYERTYPE_TILES);
			if(pLayer)
			{
				float w, h;
				pLayer->GetSize(&w, &h);

				IGraphics::CLineItem aArray[4] = {
					IGraphics::CLineItem(0, 0, w, 0),
					IGraphics::CLineItem(w, 0, w, h),
					IGraphics::CLineItem(w, h, 0, h),
					IGraphics::CLineItem(0, h, 0, 0)};
				Graphics()->TextureClear();
				Graphics()->LinesBegin();
				Graphics()->LinesDraw(aArray, std::size(aArray));
				Graphics()->LinesEnd();
			}
		}
	}
}

void CMapView::RenderMap()
{
	CEditorMap &Map = Editor()->m_Map;

	if(Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && Input()->KeyPress(KEY_G))
	{
		const bool AnyHidden =
			!Map.m_pGameLayer->m_Visible ||
			(Map.m_pFrontLayer && !Map.m_pFrontLayer->m_Visible) ||
			(Map.m_pTeleLayer && !Map.m_pTeleLayer->m_Visible) ||
			(Map.m_pSpeedupLayer && !Map.m_pSpeedupLayer->m_Visible) ||
			(Map.m_pTuneLayer && !Map.m_pTuneLayer->m_Visible) ||
			(Map.m_pSwitchLayer && !Map.m_pSwitchLayer->m_Visible);
		Map.m_pGameLayer->m_Visible = AnyHidden;
		if(Map.m_pFrontLayer)
			Map.m_pFrontLayer->m_Visible = AnyHidden;
		if(Map.m_pTeleLayer)
			Map.m_pTeleLayer->m_Visible = AnyHidden;
		if(Map.m_pSpeedupLayer)
			Map.m_pSpeedupLayer->m_Visible = AnyHidden;
		if(Map.m_pTuneLayer)
			Map.m_pTuneLayer->m_Visible = AnyHidden;
		if(Map.m_pSwitchLayer)
			Map.m_pSwitchLayer->m_Visible = AnyHidden;
	}

	// TODO: iterate over objects here...
	for(auto &pObject : Map.m_vpObjects)
	{
		pObject->Render();
	}
	//for(int g = 0; g < (int)Map.m_vpGroups.size(); g++)
	//{
	//	if(ResolveVisibility(g))
	//		Map.m_vpGroups.at(g)->Render();
	//}

	// render the game, tele, speedup, front, tune and switch above everything else
	int GameGroupIndex = std::find(Map.m_vpGroups.begin(), Map.m_vpGroups.end(), Map.m_pGameGroup) - Map.m_vpGroups.begin();
	if(ResolveVisibility(GameGroupIndex))
	{
		Map.m_pGameGroup->MapScreen();
		for(auto &pLayer : Map.m_pGameGroup->m_vpLayers)
		{
			if(pLayer->m_Visible && pLayer->IsEntitiesLayer())
				pLayer->Render();
		}
	}

	std::shared_ptr<CLayerTiles> pSelectedTilesLayer = std::static_pointer_cast<CLayerTiles>(Editor()->GetSelectedLayerType(0, LAYERTYPE_TILES));
	if(Editor()->m_ShowTileInfo != CEditor::SHOW_TILE_OFF && pSelectedTilesLayer && pSelectedTilesLayer->m_Visible && m_Zoom.GetValue() <= 300.0f)
	{
		Editor()->GetSelectedGroup()->MapScreen();
		pSelectedTilesLayer->ShowInfo();
	}
}

void CMapView::ResetZoom()
{
	SetEditorOffset({0, 0});
	m_Zoom.SetValue(100.0f);
}

float CMapView::ScaleLength(float Value) const
{
	return m_WorldZoom * Value;
}

void CMapView::ZoomMouseTarget(float ZoomFactor)
{
	// zoom to the current mouse position
	// get absolute mouse position
	float aPoints[4];
	RenderTools()->MapScreenToWorld(
		GetWorldOffset().x, GetWorldOffset().y,
		100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), m_WorldZoom, aPoints);

	float WorldWidth = aPoints[2] - aPoints[0];
	float WorldHeight = aPoints[3] - aPoints[1];

	float Mwx = aPoints[0] + WorldWidth * (Ui()->MouseX() / Ui()->Screen()->w);
	float Mwy = aPoints[1] + WorldHeight * (Ui()->MouseY() / Ui()->Screen()->h);

	// adjust camera
	OffsetWorld((vec2(Mwx, Mwy) - GetWorldOffset()) * (1.0f - ZoomFactor));
}

void CMapView::UpdateZoom()
{
	float OldLevel = m_Zoom.GetValue();
	bool UpdatedZoom = m_Zoom.UpdateValue();
	m_Zoom.SetValueRange(10.0f, g_Config.m_EdLimitMaxZoomLevel ? 2000.0f : std::numeric_limits<float>::max());
	float NewLevel = m_Zoom.GetValue();
	if(UpdatedZoom && g_Config.m_EdZoomTarget)
		ZoomMouseTarget(NewLevel / OldLevel);
	m_WorldZoom = NewLevel / 100.0f;
}

CSmoothValue *CMapView::Zoom()
{
	return &m_Zoom;
}

const CSmoothValue *CMapView::Zoom() const
{
	return &m_Zoom;
}

CProofMode *CMapView::ProofMode()
{
	return &m_ProofMode;
}

const CProofMode *CMapView::ProofMode() const
{
	return &m_ProofMode;
}

CMapGrid *CMapView::MapGrid()
{
	return &m_MapGrid;
}

const CMapGrid *CMapView::MapGrid() const
{
	return &m_MapGrid;
}

void CMapView::OffsetWorld(vec2 Offset)
{
	m_WorldOffset += Offset;
}

void CMapView::OffsetEditor(vec2 Offset)
{
	m_EditorOffset += Offset;
}

void CMapView::SetWorldOffset(vec2 WorldOffset)
{
	m_WorldOffset = WorldOffset;
}

void CMapView::SetEditorOffset(vec2 EditorOffset)
{
	m_EditorOffset = EditorOffset;
}

vec2 CMapView::GetWorldOffset() const
{
	return m_WorldOffset;
}

vec2 CMapView::GetEditorOffset() const
{
	return m_EditorOffset;
}

float CMapView::GetWorldZoom() const
{
	return m_WorldZoom;
}

bool CMapView::ResolveVisibility(int GroupIndex)
{
	CEditorMap &Map = Editor()->m_Map;

	// If group is not visible, then simply return false
	if(!Map.m_vpGroups[GroupIndex]->m_Visible)
		return false;

	// Otherwise, we need to check for parent groups

	// First find the corresponding group info
	//int GroupInfoIndex = Map.GroupInfoIndex(CEditorGroupInfo::TYPE_LAYER_GROUP, GroupIndex);
	//int Parent = Map.m_vGroupInfos[GroupInfoIndex].m_ParentIndex;

	//// Traverse the parents from bottom to up
	//while(Parent != CEditorGroupInfo::PARENT_NONE)
	//{
	//	CEditorGroupInfo &Info = Map.m_vGroupInfos.at(Parent);
	//	if(Info.m_Type != CEditorGroupInfo::TYPE_PARENT_GROUP)
	//		continue;

	//	// If we find a parent that shouldn't be visible, then return false
	//	if(!Map.m_vpGroupParents[Info.m_GroupIndex]->m_Visible)
	//		return false;

	//	Parent = Info.m_ParentIndex;
	//}

	// If all parents are visible, it means the group is visible
	return true;
}
