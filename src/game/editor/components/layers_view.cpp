#include "layers_view.h"

#include <engine/keys.h>

#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>
#include <game/editor/popups.h>
#include <iterator>

using namespace FontIcons;

void CLayersView::Init(CEditor *pEditor)
{
	CEditorComponent::Init(pEditor);

	m_ScrollToSelectionNext = false;
	m_ParentPopupContext.m_pEditor = pEditor;

	SetOperation(OP_NONE);
}

void CLayersView::Render(CUIRect LayersBox)
{
	CEditor *pEditor = Editor();
	CEditorMap &Map = Editor()->m_Map;
	//auto &SelectedGroup = Editor()->m_SelectedGroup;
	//auto &vSelectedLayers = Editor()->m_vSelectedLayers;

	ResetDragContext();
	ResetRenderContext();

	CUIRect UnscrolledLayersBox = LayersBox;

	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarWidth = 10.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ScrollUnit = ROW_HEIGHT * 5.0f;
	m_ScrollRegion.Begin(&LayersBox, &ScrollOffset, &ScrollParams);
	LayersBox.y += ScrollOffset.y;

	m_RenderContext.m_vButtonsPerGroup.reserve(Map.m_vpGroups.size());
	for(const std::shared_ptr<CLayerGroup> &pGroup : Map.m_vpGroups)
	{
		m_RenderContext.m_vButtonsPerGroup.push_back(pGroup->m_vpLayers.size() + 1);
	}

	if(m_pDraggedButton != nullptr && Ui()->ActiveItem() != m_pDraggedButton)
	{
		SetOperation(OP_NONE);
	}

	//if(m_Operation == OP_LAYER_DRAG || m_Operation == OP_GROUP_DRAG)
	//{
	//	float MinDraggableValue = UnscrolledLayersBox.y;
	//	float MaxDraggableValue = MinDraggableValue;
	//	for(int NumButtons : m_RenderContext.m_vButtonsPerGroup)
	//	{
	//		MaxDraggableValue += NumButtons * (ROW_HEIGHT + 2.0f) + 5.0f;
	//	}
	//	MaxDraggableValue += ScrollOffset.y;

	//	if(m_Operation == OP_GROUP_DRAG)
	//	{
	//		MaxDraggableValue -= m_RenderContext.m_vButtonsPerGroup[SelectedGroup] * (ROW_HEIGHT + 2.0f) + 5.0f;
	//	}
	//	else if(m_Operation == OP_LAYER_DRAG)
	//	{
	//		MinDraggableValue += ROW_HEIGHT + 2.0f;
	//		MaxDraggableValue -= vSelectedLayers.size() * (ROW_HEIGHT + 2.0f) + 5.0f;
	//	}

	//	UnscrolledLayersBox.HSplitTop(m_InitialCutHeight, nullptr, &UnscrolledLayersBox);
	//	UnscrolledLayersBox.y -= m_InitialMouseY - Ui()->MouseY();

	//	UnscrolledLayersBox.y = clamp(UnscrolledLayersBox.y, MinDraggableValue, MaxDraggableValue);

	//	UnscrolledLayersBox.w = LayersBox.w;
	//}

	m_RenderContext.m_ScrollToSelection = pEditor->LayerSelector()->SelectByTile() || m_ScrollToSelectionNext;
	m_ScrollToSelectionNext = false;

	// TODO: you can select group parents but you can't do anything in the map view if so

	// find root nodes
	std::vector<bool> vRootNodes(Map.m_vGroupInfos.size());
	for(int g = 0; g < (int)Map.m_vGroupInfos.size(); g++)
	{
		auto &GroupInfo = Map.m_vGroupInfos.at(g);
		vRootNodes[g] = GroupInfo.m_ParentIndex == CEditorGroupInfo::PARENT_NONE;

		//for(int &Child : GroupInfo.m_Children)
		//	vNodesVisited[Child] = true;
	}

	// render layers
	for(int GroupInfoIndex = 0; GroupInfoIndex < (int)Map.m_vGroupInfos.size(); GroupInfoIndex++)
	{
		if(!vRootNodes[GroupInfoIndex])
			continue;

		const CEditorGroupInfo &Info = Map.m_vGroupInfos.at(GroupInfoIndex);

		if(Info.m_Type == CEditorGroupInfo::TYPE_LAYER_GROUP)
		{
			RenderLayersGroup(&LayersBox, Info);
		}
		else if(Info.m_Type == CEditorGroupInfo::TYPE_PARENT_GROUP)
		{
			RenderParentGroup(&LayersBox, GroupInfoIndex, Info);
		}
	}

	auto &[GroupAfterDraggedLayer, LayerAfterDraggedLayer, DraggedPositionFound, MoveLayers, MoveGroup, StartDragLayer, StartDragGroup] = m_DragContext;

	//if(!DraggedPositionFound && m_Operation == OP_LAYER_DRAG)
	//{
	//	GroupAfterDraggedLayer = Map.m_vpGroups.size();
	//	LayerAfterDraggedLayer = Map.m_vpGroups[GroupAfterDraggedLayer - 1]->m_vpLayers.size();

	//	CUIRect TmpSlot;
	//	LayersBox.HSplitTop(vSelectedLayers.size() * (ROW_HEIGHT + 2.0f), &TmpSlot, &LayersBox);
	//	m_ScrollRegion.AddRect(TmpSlot);
	//}

	//if(!DraggedPositionFound && m_Operation == OP_GROUP_DRAG)
	//{
	//	GroupAfterDraggedLayer = Map.m_vpGroups.size();

	//	CUIRect TmpSlot;
	//	if(Map.m_vpGroups[SelectedGroup]->m_Collapse)
	//		LayersBox.HSplitTop(ROW_HEIGHT + 7.0f, &TmpSlot, &LayersBox);
	//	else
	//		LayersBox.HSplitTop(m_RenderContext.m_vButtonsPerGroup[SelectedGroup] * (ROW_HEIGHT + 2.0f) + 5.0f, &TmpSlot, &LayersBox);
	//	m_ScrollRegion.AddRect(TmpSlot, false);
	//}

	//if(MoveLayers && 1 <= GroupAfterDraggedLayer && GroupAfterDraggedLayer <= (int)Map.m_vpGroups.size())
	//{
	//	std::vector<std::shared_ptr<CLayer>> &vpNewGroupLayers = Map.m_vpGroups[GroupAfterDraggedLayer - 1]->m_vpLayers;
	//	if(0 <= LayerAfterDraggedLayer && LayerAfterDraggedLayer <= (int)vpNewGroupLayers.size())
	//	{
	//		std::vector<std::shared_ptr<CLayer>> vpSelectedLayers;
	//		std::vector<std::shared_ptr<CLayer>> &vpSelectedGroupLayers = Map.m_vpGroups[SelectedGroup]->m_vpLayers;
	//		std::shared_ptr<CLayer> pNextLayer = nullptr;
	//		if(LayerAfterDraggedLayer < (int)vpNewGroupLayers.size())
	//			pNextLayer = vpNewGroupLayers[LayerAfterDraggedLayer];

	//		std::sort(vSelectedLayers.begin(), vSelectedLayers.end(), std::greater<>());
	//		for(int k : vSelectedLayers)
	//		{
	//			vpSelectedLayers.insert(vpSelectedLayers.begin(), vpSelectedGroupLayers[k]);
	//		}
	//		for(int k : vSelectedLayers)
	//		{
	//			vpSelectedGroupLayers.erase(vpSelectedGroupLayers.begin() + k);
	//		}

	//		auto InsertPosition = std::find(vpNewGroupLayers.begin(), vpNewGroupLayers.end(), pNextLayer);
	//		int InsertPositionIndex = InsertPosition - vpNewGroupLayers.begin();
	//		vpNewGroupLayers.insert(InsertPosition, vpSelectedLayers.begin(), vpSelectedLayers.end());

	//		int NumSelectedLayers = vSelectedLayers.size();
	//		vSelectedLayers.clear();
	//		for(int i = 0; i < NumSelectedLayers; i++)
	//			vSelectedLayers.push_back(InsertPositionIndex + i);

	//		SelectedGroup = GroupAfterDraggedLayer - 1;
	//		Map.OnModify();
	//	}
	//}

	//if(MoveGroup && 0 <= GroupAfterDraggedLayer && GroupAfterDraggedLayer <= (int)Map.m_vpGroups.size())
	//{
	//	std::shared_ptr<CLayerGroup> pSelectedGroup = Map.m_vpGroups[SelectedGroup];
	//	std::shared_ptr<CLayerGroup> pNextGroup = nullptr;
	//	if(GroupAfterDraggedLayer < (int)Map.m_vpGroups.size())
	//		pNextGroup = Map.m_vpGroups[GroupAfterDraggedLayer];

	//	Map.m_vpGroups.erase(Map.m_vpGroups.begin() + SelectedGroup);

	//	auto InsertPosition = std::find(Map.m_vpGroups.begin(), Map.m_vpGroups.end(), pNextGroup);
	//	Map.m_vpGroups.insert(InsertPosition, pSelectedGroup);

	//	auto Pos = std::find(Map.m_vpGroups.begin(), Map.m_vpGroups.end(), pSelectedGroup);
	//	SelectedGroup = Pos - Map.m_vpGroups.begin();

	//	Map.OnModify();
	//}

	static int s_InitialGroupIndex;
	static std::vector<int> s_vInitialLayerIndices;

	//if(MoveLayers || MoveGroup)
	//{
	//	SetOperation(OP_NONE);
	//}
	//if(StartDragLayer)
	//{
	//	SetOperation(OP_LAYER_DRAG);
	//	s_InitialGroupIndex = SelectedGroup;
	//	s_vInitialLayerIndices = std::vector(vSelectedLayers);
	//}
	//if(StartDragGroup)
	//{
	//	s_InitialGroupIndex = SelectedGroup;
	//	SetOperation(OP_GROUP_DRAG);
	//}

	//if(m_Operation == OP_LAYER_DRAG || m_Operation == OP_GROUP_DRAG)
	//{
	//	if(m_pDraggedButton == nullptr)
	//	{
	//		SetOperation(OP_NONE);
	//	}
	//	else
	//	{
	//		m_ScrollRegion.DoEdgeScrolling();
	//		Ui()->SetActiveItem(m_pDraggedButton);
	//	}
	//}

	if(Input()->KeyPress(KEY_DOWN) && Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && m_Operation == OP_NONE)
	{
		if(Input()->ShiftIsPressed())
		{
			//if(vSelectedLayers[vSelectedLayers.size() - 1] < (int)Map.m_vpGroups[SelectedGroup]->m_vpLayers.size() - 1)
			//	Editor()->AddSelectedLayer(vSelectedLayers[vSelectedLayers.size() - 1] + 1);
		}
		else
		{
			//int CurrentLayer = 0;
			//for(const auto &Selected : vSelectedLayers)
			//	CurrentLayer = maximum(Selected, CurrentLayer);
			//Editor()->SelectLayer(CurrentLayer);

			//if(vSelectedLayers[0] < (int)Map.m_vpGroups[SelectedGroup]->m_vpLayers.size() - 1)
			//{
			//	Editor()->SelectLayer(vSelectedLayers[0] + 1);
			//}
			//else
			//{
			//	for(size_t Group = SelectedGroup + 1; Group < Map.m_vpGroups.size(); Group++)
			//	{
			//		if(!Map.m_vpGroups[Group]->m_vpLayers.empty())
			//		{
			//			Editor()->SelectLayer(0, Group);
			//			break;
			//		}
			//	}
			//}
		}
		m_ScrollToSelectionNext = true;
	}
	if(Input()->KeyPress(KEY_UP) && Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && m_Operation == OP_NONE)
	{
		if(Input()->ShiftIsPressed())
		{
			//if(vSelectedLayers[vSelectedLayers.size() - 1] > 0)
			//	Editor()->AddSelectedLayer(vSelectedLayers[vSelectedLayers.size() - 1] - 1);
		}
		else
		{
			//int CurrentLayer = std::numeric_limits<int>::max();
			//for(const auto &Selected : vSelectedLayers)
			//	CurrentLayer = minimum(Selected, CurrentLayer);
			//Editor()->SelectLayer(CurrentLayer);

			//if(vSelectedLayers[0] > 0)
			//{
			//	Editor()->SelectLayer(vSelectedLayers[0] - 1);
			//}
			//else
			//{
			//	for(int Group = SelectedGroup - 1; Group >= 0; Group--)
			//	{
			//		if(!Map.m_vpGroups[Group]->m_vpLayers.empty())
			//		{
			//			Editor()->SelectLayer(Map.m_vpGroups[Group]->m_vpLayers.size() - 1, Group);
			//			break;
			//		}
			//	}
			//}
		}
		m_ScrollToSelectionNext = true;
	}

	CUIRect AddGroupButton;
	LayersBox.HSplitTop(ROW_HEIGHT + 1.0f, &AddGroupButton, &LayersBox);
	if(m_ScrollRegion.AddRect(AddGroupButton))
	{
		AddGroupButton.HSplitTop(ROW_HEIGHT, &AddGroupButton, 0);
		static int s_AddGroupButton = 0;
		if(Editor()->DoButton_Editor(&s_AddGroupButton, "Add group", 0, &AddGroupButton, IGraphics::CORNER_R, "Adds a new group"))
		{
			Map.NewGroup();
			SelectedGroup = Map.m_vpGroups.size() - 1;
			Editor()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(Editor(), SelectedGroup, false));
		}
	}

	m_ScrollRegion.End();

	if(m_Operation == OP_NONE)
	{
		if(m_PreviousOperation == OP_GROUP_DRAG)
		{
			m_PreviousOperation = OP_NONE;
			Editor()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditGroupProp>(Editor(), SelectedGroup, EGroupProp::PROP_ORDER, s_InitialGroupIndex, SelectedGroup));
		}
		else if(m_PreviousOperation == OP_LAYER_DRAG)
		{
			if(s_InitialGroupIndex != SelectedGroup)
			{
				Editor()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditLayersGroupAndOrder>(Editor(), s_InitialGroupIndex, s_vInitialLayerIndices, SelectedGroup, vSelectedLayers));
			}
			else
			{
				std::vector<std::shared_ptr<IEditorAction>> vpActions;
				std::vector<int> vLayerIndices = vSelectedLayers;
				std::sort(vLayerIndices.begin(), vLayerIndices.end());
				std::sort(s_vInitialLayerIndices.begin(), s_vInitialLayerIndices.end());
				for(int k = 0; k < (int)vLayerIndices.size(); k++)
				{
					int LayerIndex = vLayerIndices[k];
					vpActions.push_back(std::make_shared<CEditorActionEditLayerProp>(Editor(), SelectedGroup, LayerIndex, ELayerProp::PROP_ORDER, s_vInitialLayerIndices[k], LayerIndex));
				}
				Editor()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Editor(), vpActions, nullptr, true));
			}
			m_PreviousOperation = OP_NONE;
		}
	}
}

void CLayersView::RenderParentGroup(CUIRect *pRect, int Index, const CEditorGroupInfo &Info)
{
	char aBuf[64];
	CEditorMap &Map = Editor()->m_Map;

	int g = Info.m_GroupIndex;
	std::shared_ptr<CEditorParentGroup> &pGroupParent = Map.m_vpGroupParents.at(g);

	std::vector<int> vChildren;
	for(size_t k = 0; k < Map.m_vGroupInfos.size(); k++)
	{
		if(Map.m_vGroupInfos[k].m_ParentIndex == Index)
			vChildren.push_back(k);
	}

	CUIRect Slot;
	pRect->HSplitTop(ROW_HEIGHT, &Slot, pRect);

	CUIRect TmpRect;
	pRect->HSplitTop(2.0f, &TmpRect, pRect);
	m_ScrollRegion.AddRect(TmpRect);

	if(m_ScrollRegion.AddRect(Slot))
	{
		str_format(aBuf, sizeof(aBuf), "%s", pGroupParent->m_aName);

		bool Clicked;
		bool Abrupted;
		if(int Result = DoGroupButton(&Info, aBuf, 0, &Slot, &Clicked, &Abrupted, "Something I guess"))
		{
			if(Clicked)
			{
				if(Result == 2)
				{
					m_ParentPopupContext.m_GroupInfoIndex = Index;
					Ui()->DoPopupMenu(&m_ParentPopupContext, Ui()->MouseX(), Ui()->MouseY(), 145, 120, &m_ParentPopupContext, Editor()->PopupParentGroup);
				}

				if(!vChildren.empty() && Input()->MouseDoubleClick())
					pGroupParent->m_Collapse ^= 1;
			}
		}
	}

	if(!pGroupParent->m_Collapse)
	{
		int StartY = pRect->y;

		// Render childrens
		CUIRect ChildrenBox = *pRect;
		ChildrenBox.VSplitLeft(INDENT, nullptr, &ChildrenBox);
		for(int Child : vChildren)
		{
			CEditorGroupInfo &ChildInfo = Map.m_vGroupInfos.at(Child);
			if(ChildInfo.m_Type == CEditorGroupInfo::TYPE_LAYER_GROUP)
				RenderLayersGroup(&ChildrenBox, ChildInfo);
			else if(ChildInfo.m_Type == CEditorGroupInfo::TYPE_PARENT_GROUP)
				RenderParentGroup(&ChildrenBox, Child, ChildInfo);
		}
		int EndY = ChildrenBox.y;

		CUIRect IndentIndicator;
		pRect->VSplitLeft(4.0f, nullptr, &IndentIndicator);
		IndentIndicator.VSplitLeft(1.0f, &IndentIndicator, nullptr);
		IndentIndicator.h = EndY - StartY - 4.0f;
		IndentIndicator.Draw(ColorRGBA(0.6f, 0.6f, 0.6f, 0.4f), IGraphics::CORNER_NONE, 0.0f);

		pRect->y = EndY;
	}
	else
	{
		pRect->HSplitTop(5.0f, &Slot, pRect);
		m_ScrollRegion.AddRect(Slot);
	}
}

void CLayersView::RenderLayersGroup(CUIRect *pRect, const CEditorGroupInfo &Info)
{
	char aBuf[64];
	CEditorMap &Map = Editor()->m_Map;
	auto &SelectedGroup = Editor()->m_SelectedGroup;
	auto &vSelectedLayers = Editor()->m_vSelectedLayers;

	auto &[GroupAfterDraggedLayer, LayerAfterDraggedLayer, DraggedPositionFound, MoveLayers, MoveGroup, StartDragLayer, StartDragGroup] = m_DragContext;
	auto &[UnscrolledLayersBox, vButtonsPerGroup, ScrollToSelection] = m_RenderContext;

	int g = Info.m_GroupIndex;
	if(m_Operation == OP_LAYER_DRAG && g > 0 && !DraggedPositionFound && Ui()->MouseY() < pRect->y + ROW_HEIGHT / 2)
	{
		DraggedPositionFound = true;
		GroupAfterDraggedLayer = g;

		LayerAfterDraggedLayer = Map.m_vpGroups[g - 1]->m_vpLayers.size();

		CUIRect Slot;
		pRect->HSplitTop(vSelectedLayers.size() * (ROW_HEIGHT + 2.0f), &Slot, pRect);
		m_ScrollRegion.AddRect(Slot);
	}

	CUIRect Slot, VisibleToggle;
	if(m_Operation == OP_GROUP_DRAG)
	{
		if(g == SelectedGroup)
		{
			UnscrolledLayersBox.HSplitTop(ROW_HEIGHT, &Slot, &UnscrolledLayersBox);
			UnscrolledLayersBox.HSplitTop(2.0f, nullptr, &UnscrolledLayersBox);
		}
		else if(!DraggedPositionFound && Ui()->MouseY() < pRect->y + ROW_HEIGHT * vButtonsPerGroup[g] / 2 + 3.0f)
		{
			DraggedPositionFound = true;
			GroupAfterDraggedLayer = g;

			CUIRect TmpSlot;
			if(Map.m_vpGroups[SelectedGroup]->m_Collapse)
				pRect->HSplitTop(ROW_HEIGHT + 7.0f, &TmpSlot, pRect);
			else
				pRect->HSplitTop(vButtonsPerGroup[SelectedGroup] * (ROW_HEIGHT + 2.0f) + 5.0f, &TmpSlot, pRect);
			m_ScrollRegion.AddRect(TmpSlot, false);
		}
	}
	if(m_Operation != OP_GROUP_DRAG || g != SelectedGroup)
	{
		pRect->HSplitTop(ROW_HEIGHT, &Slot, pRect);

		CUIRect TmpRect;
		pRect->HSplitTop(2.0f, &TmpRect, pRect);
		m_ScrollRegion.AddRect(TmpRect);
	}

	if(m_ScrollRegion.AddRect(Slot))
	{
		Slot.VSplitLeft(15.0f, &VisibleToggle, &Slot);
		if(Editor()->DoButton_FontIcon(&Map.m_vpGroups[g]->m_Visible, Map.m_vpGroups[g]->m_Visible ? FONT_ICON_EYE : FONT_ICON_EYE_SLASH, Map.m_vpGroups[g]->m_Collapse ? 1 : 0, &VisibleToggle, 0, "Toggle group visibility", IGraphics::CORNER_L, 8.0f))
			Map.m_vpGroups[g]->m_Visible = !Map.m_vpGroups[g]->m_Visible;

		str_format(aBuf, sizeof(aBuf), "#%d %s", g, Map.m_vpGroups[g]->m_aName);

		bool Clicked;
		bool Abrupted;
		if(int Result = Editor()->DoButton_DraggableEx(&Map.m_vpGroups[g], aBuf, g == SelectedGroup, &Slot, &Clicked, &Abrupted,
			   BUTTON_CONTEXT, Map.m_vpGroups[g]->m_Collapse ? "Select group. Shift click to select all layers. Double click to expand." : "Select group. Shift click to select all layers. Double click to collapse.", IGraphics::CORNER_R))
		{
			if(m_Operation == OP_NONE)
			{
				m_InitialMouseY = Ui()->MouseY();
				m_InitialCutHeight = m_InitialMouseY - UnscrolledLayersBox.y;
				SetOperation(OP_CLICK);

				if(g != SelectedGroup)
					Editor()->SelectLayer(0, g);
			}

			if(Abrupted)
			{
				SetOperation(OP_NONE);
			}

			if(m_Operation == OP_CLICK && absolute(Ui()->MouseY() - m_InitialMouseY) > MIN_DRAG_DISTANCE)
			{
				StartDragGroup = true;
				m_pDraggedButton = Map.m_vpGroups[g].get();
			}

			if(m_Operation == OP_CLICK && Clicked)
			{
				if(g != SelectedGroup)
					Editor()->SelectLayer(0, g);

				if(Input()->ShiftIsPressed() && SelectedGroup == g)
				{
					vSelectedLayers.clear();
					for(size_t i = 0; i < Map.m_vpGroups[g]->m_vpLayers.size(); i++)
					{
						Editor()->AddSelectedLayer(i);
					}
				}

				if(Result == 2)
				{
					static SPopupMenuId s_PopupGroupId;
					Ui()->DoPopupMenu(&s_PopupGroupId, Ui()->MouseX(), Ui()->MouseY(), 145, 256, Editor(), Editor()->PopupGroup);
				}

				if(!Map.m_vpGroups[g]->m_vpLayers.empty() && Input()->MouseDoubleClick())
					Map.m_vpGroups[g]->m_Collapse ^= 1;

				SetOperation(OP_NONE);
			}

			if(m_Operation == OP_GROUP_DRAG && Clicked)
				MoveGroup = true;
		}
		else if(m_pDraggedButton == Map.m_vpGroups[g].get())
		{
			SetOperation(OP_NONE);
		}
	}

	for(int i = 0; i < (int)Map.m_vpGroups[g]->m_vpLayers.size(); i++)
	{
		if(Map.m_vpGroups[g]->m_Collapse)
			continue;

		bool IsLayerSelected = false;
		if(SelectedGroup == g)
		{
			for(const auto &Selected : vSelectedLayers)
			{
				if(Selected == i)
				{
					IsLayerSelected = true;
					break;
				}
			}
		}

		if(m_Operation == OP_GROUP_DRAG && g == SelectedGroup)
		{
			UnscrolledLayersBox.HSplitTop(ROW_HEIGHT + 2.0f, &Slot, &UnscrolledLayersBox);
		}
		else if(m_Operation == OP_LAYER_DRAG)
		{
			if(IsLayerSelected)
			{
				UnscrolledLayersBox.HSplitTop(ROW_HEIGHT + 2.0f, &Slot, &UnscrolledLayersBox);
			}
			else
			{
				if(!DraggedPositionFound && Ui()->MouseY() < pRect->y + ROW_HEIGHT / 2)
				{
					DraggedPositionFound = true;
					GroupAfterDraggedLayer = g + 1;
					LayerAfterDraggedLayer = i;
					for(size_t j = 0; j < vSelectedLayers.size(); j++)
					{
						pRect->HSplitTop(ROW_HEIGHT + 2.0f, nullptr, pRect);
						m_ScrollRegion.AddRect(Slot);
					}
				}
				pRect->HSplitTop(ROW_HEIGHT + 2.0f, &Slot, pRect);
				if(!m_ScrollRegion.AddRect(Slot, ScrollToSelection && IsLayerSelected))
					continue;
			}
		}
		else
		{
			pRect->HSplitTop(ROW_HEIGHT + 2.0f, &Slot, pRect);
			if(!m_ScrollRegion.AddRect(Slot, ScrollToSelection && IsLayerSelected))
				continue;
		}

		Slot.HSplitTop(ROW_HEIGHT, &Slot, nullptr);

		CUIRect Button;
		Slot.VSplitLeft(INDENT, nullptr, &Slot);
		Slot.VSplitLeft(15.0f, &VisibleToggle, &Button);

		if(Editor()->DoButton_FontIcon(&Map.m_vpGroups[g]->m_vpLayers[i]->m_Visible, Map.m_vpGroups[g]->m_vpLayers[i]->m_Visible ? FONT_ICON_EYE : FONT_ICON_EYE_SLASH, 0, &VisibleToggle, 0, "Toggle layer visibility", IGraphics::CORNER_L, 8.0f))
			Map.m_vpGroups[g]->m_vpLayers[i]->m_Visible = !Map.m_vpGroups[g]->m_vpLayers[i]->m_Visible;

		if(Map.m_vpGroups[g]->m_vpLayers[i]->m_aName[0])
			str_copy(aBuf, Map.m_vpGroups[g]->m_vpLayers[i]->m_aName);
		else
		{
			if(Map.m_vpGroups[g]->m_vpLayers[i]->m_Type == LAYERTYPE_TILES)
			{
				std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(Map.m_vpGroups[g]->m_vpLayers[i]);
				str_copy(aBuf, pTiles->m_Image >= 0 ? Map.m_vpImages[pTiles->m_Image]->m_aName : "Tiles");
			}
			else if(Map.m_vpGroups[g]->m_vpLayers[i]->m_Type == LAYERTYPE_QUADS)
			{
				std::shared_ptr<CLayerQuads> pQuads = std::static_pointer_cast<CLayerQuads>(Map.m_vpGroups[g]->m_vpLayers[i]);
				str_copy(aBuf, pQuads->m_Image >= 0 ? Map.m_vpImages[pQuads->m_Image]->m_aName : "Quads");
			}
			else if(Map.m_vpGroups[g]->m_vpLayers[i]->m_Type == LAYERTYPE_SOUNDS)
			{
				std::shared_ptr<CLayerSounds> pSounds = std::static_pointer_cast<CLayerSounds>(Map.m_vpGroups[g]->m_vpLayers[i]);
				str_copy(aBuf, pSounds->m_Sound >= 0 ? Map.m_vpSounds[pSounds->m_Sound]->m_aName : "Sounds");
			}
		}

		int Checked = IsLayerSelected ? 1 : 0;
		if(Map.m_vpGroups[g]->m_vpLayers[i]->IsEntitiesLayer())
		{
			Checked += 6;
		}

		bool Clicked;
		bool Abrupted;
		if(int Result = Editor()->DoButton_DraggableEx(Map.m_vpGroups[g]->m_vpLayers[i].get(), aBuf, Checked, &Button, &Clicked, &Abrupted,
			   BUTTON_CONTEXT, "Select layer. Shift click to select multiple.", IGraphics::CORNER_R))
		{
			if(m_Operation == OP_NONE)
			{
				m_InitialMouseY = Ui()->MouseY();
				m_InitialCutHeight = m_InitialMouseY - UnscrolledLayersBox.y;

				SetOperation(OP_CLICK);

				if(!Input()->ShiftIsPressed() && !IsLayerSelected)
				{
					Editor()->SelectLayer(i, g);
				}
			}

			if(Abrupted)
			{
				SetOperation(OP_NONE);
			}

			if(m_Operation == OP_CLICK && absolute(Ui()->MouseY() - m_InitialMouseY) > MIN_DRAG_DISTANCE)
			{
				bool EntitiesLayerSelected = false;
				for(int k : vSelectedLayers)
				{
					if(Map.m_vpGroups[SelectedGroup]->m_vpLayers[k]->IsEntitiesLayer())
						EntitiesLayerSelected = true;
				}

				if(!EntitiesLayerSelected)
					StartDragLayer = true;

				m_pDraggedButton = Map.m_vpGroups[g]->m_vpLayers[i].get();
			}

			if(m_Operation == OP_CLICK && Clicked)
			{
				static CEditor::SLayerPopupContext s_LayerPopupContext = {};
				s_LayerPopupContext.m_pEditor = Editor();
				if(Result == 1)
				{
					if(Input()->ShiftIsPressed() && SelectedGroup == g)
					{
						auto Position = std::find(vSelectedLayers.begin(), vSelectedLayers.end(), i);
						if(Position != vSelectedLayers.end())
							vSelectedLayers.erase(Position);
						else
							Editor()->AddSelectedLayer(i);
					}
					else if(!Input()->ShiftIsPressed())
					{
						Editor()->SelectLayer(i, g);
					}
				}
				else if(Result == 2)
				{
					s_LayerPopupContext.m_vpLayers.clear();
					s_LayerPopupContext.m_vLayerIndices.clear();

					if(!IsLayerSelected)
					{
						Editor()->SelectLayer(i, g);
					}

					if(vSelectedLayers.size() > 1)
					{
						bool AllTile = true;
						for(size_t j = 0; AllTile && j < vSelectedLayers.size(); j++)
						{
							int LayerIndex = vSelectedLayers[j];
							if(Map.m_vpGroups[SelectedGroup]->m_vpLayers[LayerIndex]->m_Type == LAYERTYPE_TILES)
							{
								s_LayerPopupContext.m_vpLayers.push_back(std::static_pointer_cast<CLayerTiles>(Map.m_vpGroups[SelectedGroup]->m_vpLayers[vSelectedLayers[j]]));
								s_LayerPopupContext.m_vLayerIndices.push_back(LayerIndex);
							}
							else
								AllTile = false;
						}

						// Don't allow editing if all selected layers are not tile layers
						if(!AllTile)
						{
							s_LayerPopupContext.m_vpLayers.clear();
							s_LayerPopupContext.m_vLayerIndices.clear();
						}
					}

					Ui()->DoPopupMenu(&s_LayerPopupContext, Ui()->MouseX(), Ui()->MouseY(), 120, 270, &s_LayerPopupContext, Editor()->PopupLayer);
				}

				SetOperation(OP_NONE);
			}

			if(m_Operation == OP_LAYER_DRAG && Clicked)
			{
				MoveLayers = true;
			}
		}
		else if(m_pDraggedButton == Map.m_vpGroups[g]->m_vpLayers[i].get())
		{
			SetOperation(OP_NONE);
		}
	}

	if(m_Operation != OP_GROUP_DRAG || g != SelectedGroup)
	{
		pRect->HSplitTop(5.0f, &Slot, pRect);
		m_ScrollRegion.AddRect(Slot);
	}
}

void CLayersView::SetOperation(int Operation)
{
	if(Operation != m_Operation)
	{
		m_PreviousOperation = m_Operation;
		m_Operation = Operation;
		if(Operation == OP_NONE)
		{
			m_pDraggedButton = nullptr;
		}
	}
}

void CLayersView::ResetDragContext()
{
	m_DragContext.m_GroupAfterDraggedLayer = -1;
	m_DragContext.m_LayerAfterDraggedLayer = -1;
	m_DragContext.m_DraggedPositionFound = false;
	m_DragContext.m_MoveLayers = false;
	m_DragContext.m_MoveGroup = false;
	m_DragContext.m_StartDragLayer = false;
	m_DragContext.m_StartDragGroup = false;
}

void CLayersView::ResetRenderContext()
{
	m_RenderContext.m_vButtonsPerGroup.clear();
	m_RenderContext.m_ScrollToSelection = false;
}

int CLayersView::DoGroupButton(const CEditorGroupInfo *pGroupInfo, const char *pText, int Checked, const CUIRect *pRect, bool *pClicked, bool *pAbrupted, const char *pToolTip)
{
	CEditorMap &Map = Editor()->m_Map;
	std::shared_ptr<IGroup> pGroupBase = GetGroupBase(pGroupInfo);
	if(!pGroupBase)
		return -1;

	CUIRect Button, VisibleToggle;

	pRect->VSplitLeft(15.0f, &VisibleToggle, &Button);

	if(Editor()->DoButton_FontIcon(&pGroupBase->m_Visible, pGroupBase->m_Visible ? FONT_ICON_EYE : FONT_ICON_EYE_SLASH, 0, &VisibleToggle, 0, "Toggle group visibility", IGraphics::CORNER_L, 8.0f))
	{
		// TODO
		pGroupBase->m_Visible = !pGroupBase->m_Visible;
	}

	return Editor()->DoButton_DraggableEx(pGroupInfo, pText, Checked, &Button, pClicked, pAbrupted, BUTTON_CONTEXT, pToolTip, IGraphics::CORNER_R);
}

std::shared_ptr<IGroup> CLayersView::GetGroupBase(const CEditorGroupInfo *pGroupInfo)
{
	switch(pGroupInfo->m_Type)
	{
	case CEditorGroupInfo::TYPE_LAYER_GROUP:
		return std::static_pointer_cast<IGroup>(Editor()->m_Map.m_vpGroups.at(pGroupInfo->m_GroupIndex));
		return nullptr;
	case CEditorGroupInfo::TYPE_PARENT_GROUP:
		return std::static_pointer_cast<IGroup>(Editor()->m_Map.m_vpGroupParents.at(pGroupInfo->m_GroupIndex));
	default:
		return nullptr;
	}
}
