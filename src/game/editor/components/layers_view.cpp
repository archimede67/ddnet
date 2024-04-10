#include "layers_view.h"

#include <engine/keys.h>

#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>
#include <game/editor/popups.h>

#include <game/client/ui_treeview.h>

using namespace FontIcons;

void CLayersView::Init(CEditor *pEditor)
{
	CEditorComponent::Init(pEditor);

	m_ScrollToSelectionNext = false;
	m_ParentPopupContext.m_pEditor = pEditor;
}

void CLayersView::Render(CUIRect LayersBox)
{
	CEditor *pEditor = Editor();
	CEditorMap &Map = Editor()->m_Map;
	//auto &SelectedGroup = Editor()->m_SelectedGroup;
	//auto &vSelectedLayers = Editor()->m_vSelectedLayers;

	ResetRenderContext();

	CUIRect UnscrolledLayersBox = LayersBox;

	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarWidth = 10.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ScrollUnit = ROW_HEIGHT * 5.0f;
	m_ScrollRegion.Begin(&LayersBox, &ScrollOffset, &ScrollParams);
	LayersBox.y += ScrollOffset.y;

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

	static int s_InitialGroupIndex;
	static std::vector<int> s_vInitialLayerIndices;

	if(Input()->KeyPress(KEY_DOWN) && CanHandleInput())
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
	if(Input()->KeyPress(KEY_UP) && CanHandleInput())
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
			//SelectedGroup = Map.m_vpGroups.size() - 1;
			//Editor()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(Editor(), SelectedGroup, false));
		}
	}

	m_ScrollRegion.End();
}

//void CLayersView::RenderParentGroup(CUIRect *pRect, int Index, const CEditorGroupInfo &Info)
//{
//	char aBuf[64];
//	CEditorMap &Map = Editor()->m_Map;
//
//	int g = Info.m_GroupIndex;
//	std::shared_ptr<CEditorParentGroup> &pGroupParent = Map.m_vpGroupParents.at(g);
//
//	std::vector<int> vChildren;
//	for(size_t k = 0; k < Map.m_vGroupInfos.size(); k++)
//	{
//		if(Map.m_vGroupInfos[k].m_ParentIndex == Index)
//			vChildren.push_back(k);
//	}
//
//	CUIRect Slot;
//	pRect->HSplitTop(ROW_HEIGHT, &Slot, pRect);
//
//	CUIRect TmpRect;
//	pRect->HSplitTop(2.0f, &TmpRect, pRect);
//	m_ScrollRegion.AddRect(TmpRect);
//
//	if(m_ScrollRegion.AddRect(Slot))
//	{
//		str_format(aBuf, sizeof(aBuf), "%s", pGroupParent->m_aName);
//
//		bool Clicked = false;
//		bool Abrupted = false;
//
//		if(int Result = DoSelectable(&Info, aBuf, GroupFlags(Info), 0, &Slot, "Something I guess"))
//		{
//			if(Clicked)
//			{
//				if(Result == 2)
//				{
//					m_ParentPopupContext.m_GroupInfoIndex = Index;
//					Ui()->DoPopupMenu(&m_ParentPopupContext, Ui()->MouseX(), Ui()->MouseY(), 145, 120, &m_ParentPopupContext, Editor()->PopupParentGroup);
//				}
//
//				if(!vChildren.empty() && Input()->MouseDoubleClick())
//					pGroupParent->m_Collapse ^= 1;
//			}
//		}
//	}
//
//	if(!pGroupParent->m_Collapse)
//	{
//		int StartY = pRect->y;
//
//		// Render childrens
//		CUIRect ChildrenBox = *pRect;
//		ChildrenBox.VSplitLeft(INDENT, nullptr, &ChildrenBox);
//		for(int Child : vChildren)
//		{
//			CEditorGroupInfo &ChildInfo = Map.m_vGroupInfos.at(Child);
//			if(ChildInfo.m_Type == CEditorGroupInfo::TYPE_LAYER_GROUP)
//				RenderLayersGroup(&ChildrenBox, ChildInfo);
//			else if(ChildInfo.m_Type == CEditorGroupInfo::TYPE_PARENT_GROUP)
//				RenderParentGroup(&ChildrenBox, Child, ChildInfo);
//		}
//		int EndY = ChildrenBox.y;
//
//		CUIRect IndentIndicator;
//		pRect->VSplitLeft(4.0f, nullptr, &IndentIndicator);
//		IndentIndicator.VSplitLeft(1.0f, &IndentIndicator, nullptr);
//		IndentIndicator.h = EndY - StartY - 4.0f;
//		IndentIndicator.Draw(ColorRGBA(0.6f, 0.6f, 0.6f, 0.4f), IGraphics::CORNER_NONE, 0.0f);
//
//		pRect->y = EndY;
//	}
//	else
//	{
//		pRect->HSplitTop(5.0f, &Slot, pRect);
//		m_ScrollRegion.AddRect(Slot);
//	}
//}
//
//void CLayersView::RenderLayersGroup(CUIRect *pRect, const CEditorGroupInfo &Info)
//{
//	char aBuf[64];
//	CEditorMap &Map = Editor()->m_Map;
//	auto &SelectedGroup = Editor()->m_SelectedGroup;
//	auto &vSelectedLayers = Editor()->m_vSelectedLayers;
//
//	auto &[ScrollToSelection] = m_RenderContext;
//
//	int g = Info.m_GroupIndex;
//
//	CUIRect Slot, VisibleToggle;
//	if(g != SelectedGroup)
//	{
//		pRect->HSplitTop(ROW_HEIGHT, &Slot, pRect);
//
//		CUIRect TmpRect;
//		pRect->HSplitTop(2.0f, &TmpRect, pRect);
//		m_ScrollRegion.AddRect(TmpRect);
//	}
//
//	if(m_ScrollRegion.AddRect(Slot))
//	{
//		//Slot.VSplitLeft(15.0f, &VisibleToggle, &Slot);
//		//if(Editor()->DoButton_FontIcon(&Map.m_vpGroups[g]->m_Visible, Map.m_vpGroups[g]->m_Visible ? FONT_ICON_EYE : FONT_ICON_EYE_SLASH, Map.m_vpGroups[g]->m_Collapse ? 1 : 0, &VisibleToggle, 0, "Toggle group visibility", IGraphics::CORNER_L, 8.0f))
//		//	Map.m_vpGroups[g]->m_Visible = !Map.m_vpGroups[g]->m_Visible;
//
//		str_format(aBuf, sizeof(aBuf), "#%d %s", g, Map.m_vpGroups[g]->m_aName);
//
//		bool Clicked = false;
//		bool Abrupted = false;
//		SExtraRenderInfo Extra;
//		Extra.m_pIcon = FONT_ICON_LAYER_GROUP;
//		if(int Result = DoSelectable(&Map.m_vpGroups[g], aBuf, GroupFlags(Info), g == SelectedGroup, &Slot, Map.m_vpGroups[g]->m_Collapse ? "Select group. Shift click to select all layers. Double click to expand." : "Select group. Shift click to select all layers. Double click to collapse.", Extra))
//		{
//			if(Clicked && Result == 1)
//			{
//				if(g != SelectedGroup)
//					Editor()->SelectLayer(0, g);
//
//				if(Input()->ShiftIsPressed() && SelectedGroup == g)
//				{
//					vSelectedLayers.clear();
//					for(size_t i = 0; i < Map.m_vpGroups[g]->m_vpLayers.size(); i++)
//					{
//						Editor()->AddSelectedLayer(i);
//					}
//				}
//
//				if(Result == 2)
//				{
//					static SPopupMenuId s_PopupGroupId;
//					Ui()->DoPopupMenu(&s_PopupGroupId, Ui()->MouseX(), Ui()->MouseY(), 145, 256, Editor(), Editor()->PopupGroup);
//				}
//
//				if(!Map.m_vpGroups[g]->m_vpLayers.empty() && Input()->MouseDoubleClick())
//					Map.m_vpGroups[g]->m_Collapse ^= 1;
//			}
//		}
//	}
//
//	for(int i = 0; i < (int)Map.m_vpGroups[g]->m_vpLayers.size(); i++)
//	{
//		if(Map.m_vpGroups[g]->m_Collapse)
//			continue;
//
//		std::shared_ptr<CLayer> &pLayer = Map.m_vpGroups[g]->m_vpLayers.at(i);
//
//		if(pLayer->m_aName[0])
//			str_copy(aBuf, pLayer->m_aName);
//		else
//		{
//			if(pLayer->m_Type == LAYERTYPE_TILES)
//			{
//				std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
//				str_copy(aBuf, pTiles->m_Image >= 0 ? Map.m_vpImages[pTiles->m_Image]->m_aName : "Tiles");
//			}
//			else if(pLayer->m_Type == LAYERTYPE_QUADS)
//			{
//				std::shared_ptr<CLayerQuads> pQuads = std::static_pointer_cast<CLayerQuads>(pLayer);
//				str_copy(aBuf, pQuads->m_Image >= 0 ? Map.m_vpImages[pQuads->m_Image]->m_aName : "Quads");
//			}
//			else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
//			{
//				std::shared_ptr<CLayerSounds> pSounds = std::static_pointer_cast<CLayerSounds>(pLayer);
//				str_copy(aBuf, pSounds->m_Sound >= 0 ? Map.m_vpSounds[pSounds->m_Sound]->m_aName : "Sounds");
//			}
//		}
//
//		int Checked = false ? 1 : 0;
//		if(pLayer->IsEntitiesLayer())
//		{
//			Checked += 6;
//		}
//
//		SExtraRenderInfo Extra;
//		Extra.m_pIcon = pLayer->Icon();
//		if(int Result = DoSelectable(&pLayer, aBuf, LayerFlags(pLayer), Checked, &Button, "Select layer. Shift click to select multiple.", Extra))
//		{
//			if(!Input()->ShiftIsPressed() /* && !IsLayerSelected*/)
//			{
//				Editor()->SelectLayer(i, g);
//			}
//
//			if(Clicked)
//			{
//				static CEditor::SLayerPopupContext s_LayerPopupContext = {};
//				s_LayerPopupContext.m_pEditor = Editor();
//				if(Result == 1)
//				{
//					if(Input()->ShiftIsPressed() && SelectedGroup == g)
//					{
//						auto Position = std::find(vSelectedLayers.begin(), vSelectedLayers.end(), i);
//						if(Position != vSelectedLayers.end())
//							vSelectedLayers.erase(Position);
//						else
//							Editor()->AddSelectedLayer(i);
//					}
//					else if(!Input()->ShiftIsPressed())
//					{
//						Editor()->SelectLayer(i, g);
//					}
//				}
//				else if(Result == 2)
//				{
//					s_LayerPopupContext.m_vpLayers.clear();
//					s_LayerPopupContext.m_vLayerIndices.clear();
//
//					if(!IsLayerSelected)
//					{
//						Editor()->SelectLayer(i, g);
//					}
//
//					if(vSelectedLayers.size() > 1)
//					{
//						bool AllTile = true;
//						for(size_t j = 0; AllTile && j < vSelectedLayers.size(); j++)
//						{
//							int LayerIndex = vSelectedLayers[j];
//							if(Map.m_vpGroups[SelectedGroup]->m_vpLayers[LayerIndex]->m_Type == LAYERTYPE_TILES)
//							{
//								s_LayerPopupContext.m_vpLayers.push_back(std::static_pointer_cast<CLayerTiles>(Map.m_vpGroups[SelectedGroup]->m_vpLayers[vSelectedLayers[j]]));
//								s_LayerPopupContext.m_vLayerIndices.push_back(LayerIndex);
//							}
//							else
//								AllTile = false;
//						}
//
//						// Don't allow editing if all selected layers are not tile layers
//						if(!AllTile)
//						{
//							s_LayerPopupContext.m_vpLayers.clear();
//							s_LayerPopupContext.m_vLayerIndices.clear();
//						}
//					}
//
//					Ui()->DoPopupMenu(&s_LayerPopupContext, Ui()->MouseX(), Ui()->MouseY(), 120, 270, &s_LayerPopupContext, Editor()->PopupLayer);
//				}
//			}
//		}
//	}
//}

void CLayersView::ResetRenderContext()
{
	m_RenderContext.m_ScrollToSelection = false;
}

int CLayersView::DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra)
{
	CEditorMap &Map = Editor()->m_Map;

	CUIRect Button = *pRect;
	CUIRect VisibleToggle;

	auto pCollapse = Flags[CToggleFlags::TOGGLE_COLLAPSE];
	auto pVisible = Flags[CToggleFlags::TOGGLE_VISIBILE];

	bool RenderIcon = Extra.m_pIcon != nullptr;

	if(pCollapse)
	{
		CUIRect CollapseToggle;
		Button.VSplitLeft(15.0f, &CollapseToggle, &Button);

		if(Editor()->DoButton_FontIcon(pCollapse, RenderIcon ? Extra.m_pIcon : (*pCollapse ? FONT_ICON_FOLDER : FONT_ICON_FOLDER_OPEN), RenderIcon ? *pCollapse : 0, &CollapseToggle, 0, "Collapse", IGraphics::CORNER_L, 8.0f))
			*pCollapse = !*pCollapse;

		RenderIcon = false;
	}

	auto Color = Editor()->GetButtonColor(pId, Checked, Ui()->MouseInside(&Button));
	if(pVisible && !*pVisible)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f));
		Color.r *= 0.8f;
		Color.g *= 0.7f;
		Color.b *= 0.7f;
	}
	Button.Draw(Color, pCollapse ? IGraphics::CORNER_R : IGraphics::CORNER_ALL, 3.0f);

	if(RenderIcon)
	{
		CUIRect Icon;
		Button.VSplitLeft(12.0f, &Icon, &Button);
		DoIcon(Extra.m_pIcon, &Icon, 8.0f);
	}

	static bool s_Drag = false;

	CUIRect Rect;
	Button.VMargin(1.0f, &Rect);

	SLabelProperties Props;
	Props.m_MaxWidth = Rect.w - 15.0f;
	Props.m_EllipsisAtEnd = true;
	Props.m_EnableWidthCheck = false;
	Ui()->DoLabel(&Rect, pText, 10.0f, TEXTALIGN_ML, Props);

	if(Ui()->MouseInside(&Button))
		Editor()->ms_pUiGotContext = pId;

	Editor()->UpdateTooltip(pId, &Button, pToolTip);
	bool Clicked, Abrupted;
	int Result = Ui()->DoDraggableButtonLogic(pId, Checked, &Button, &Clicked, &Abrupted);

	//if(Result)
	//{
	//	if(Clicked)
	//}

	CUIRect ButtonBar;
	Button.VMargin(1.0f, &ButtonBar);

	if(pVisible)
	{
		ButtonBar.VSplitRight(15.0f, &ButtonBar, &VisibleToggle);

		if(DoToggleIconButton(pVisible, pId, *pVisible ? FONT_ICON_EYE : FONT_ICON_EYE_SLASH, !*pVisible, &VisibleToggle, "Toggle visibility"))
			*pVisible = !*pVisible;
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());

	return Result;
}
//
//std::shared_ptr<IGroup> CLayersView::GetGroupBase(const CEditorGroupInfo &GroupInfo)
//{
//	switch(GroupInfo.m_Type)
//	{
//	case CEditorGroupInfo::TYPE_LAYER_GROUP:
//		return std::static_pointer_cast<IGroup>(Editor()->m_Map.m_vpGroups.at(GroupInfo.m_GroupIndex));
//		return nullptr;
//	case CEditorGroupInfo::TYPE_PARENT_GROUP:
//		return std::static_pointer_cast<IGroup>(Editor()->m_Map.m_vpGroupParents.at(GroupInfo.m_GroupIndex));
//	default:
//		return nullptr;
//	}
//}

int CLayersView::DoToggleIconButton(const void *pButtonId, const void *pParentId, const char *pIcon, bool Checked, const CUIRect *pRect, const char *pToolTip)
{
	if(Checked || (pParentId != nullptr && Ui()->HotItem() == pParentId) || Ui()->HotItem() == pButtonId)
		DoIcon(pIcon, pRect, 8.0f);

	Editor()->UpdateTooltip(pButtonId, pRect, pToolTip);
	return Ui()->DoButtonLogic(pButtonId, 0, pRect);
}

void CLayersView::DoIcon(const char *pIcon, const CUIRect *pRect, float FontSize)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	Ui()->DoLabel(pRect, pIcon, FontSize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

//CToggleFlags CLayersView::GroupFlags(const CEditorGroupInfo &GroupInfo)
//{
//	std::shared_ptr<IGroup> pGroupBase = GetGroupBase(GroupInfo);
//
//	CToggleFlags Flags;
//	Flags[CToggleFlags::TOGGLE_COLLAPSE] = &pGroupBase->m_Collapse;
//	Flags[CToggleFlags::TOGGLE_VISIBILE] = &pGroupBase->m_Visible;
//
//	return Flags;
//}
//
//CToggleFlags CLayersView::LayerFlags(const std::shared_ptr<CLayer> &pLayer)
//{
//	CToggleFlags Flags;
//	Flags[CToggleFlags::TOGGLE_VISIBILE] = &pLayer->m_Visible;
//	return Flags;
//}

bool CLayersView::CanHandleInput() const
{
	return Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr;
}

// ------------------------------------------------------------

//void CLayersView::InternalRender(CTreeView &TreeView, const std::shared_ptr<TreeNode> &pNode)
//{
//	static auto &&DoBtn = [&](const void *pId, const char *pText, bool Checked, const CUIRect *pRect, bool HasChildren, bool *pClicked) {
//		auto Color = Editor()->GetButtonColor(pId, Checked);
//		pRect->Draw(Color, !HasChildren ? IGraphics::CORNER_ALL : IGraphics::CORNER_R, 3.0f);
//		CUIRect Label;
//		pRect->VMargin(5.0f, &Label);
//		Editor()->Ui()->DoLabel(&Label, pText, 10.0f, TEXTALIGN_ML);
//		return Editor()->Ui()->DoDraggableButtonLogic(pId, Checked, pRect, pClicked, nullptr);
//	};
//
//	static std::unordered_set<std::shared_ptr<TreeNode>> s_Selection;
//	bool IsSelected = s_Selection.find(pNode) != s_Selection.end();
//
//	bool Dragging = TreeView.Dragging();
//
//	CDropTargetInfo DropTargetInfo = CDropTargetInfo::None();
//	if(pNode->m_Type == TreeNode::TYPE_FOLDER)
//		DropTargetInfo = CDropTargetInfo::Accept({TreeNode::TYPE_FOLDER, TreeNode::TYPE_LAYER_GROUP});
//	else if(pNode->m_Type == TreeNode::TYPE_LAYER_GROUP)
//		DropTargetInfo = CDropTargetInfo::Accept({TreeNode::TYPE_LAYER});
//
//	auto Item = TreeView.DoNode(pNode.get(), IsSelected, pNode->m_Type, DropTargetInfo);
//	const bool Shift = Editor()->Input()->ShiftIsPressed();
//
//	if(Item.m_IsTargetParent)
//	{
//		// Draw highlighted background
//		Item.m_Rect.Draw(ColorRGBA(0.6f, 0.1f, 0.9f, 0.4f), IGraphics::CORNER_ALL, 3.0f);
//	}
//
//	if(!pNode->m_vpChildren.empty() || pNode->m_Type != 1)
//	{
//		CUIRect ColBtn;
//		Item.m_Rect.VSplitLeft(15.0f, &ColBtn, &Item.m_Rect);
//		if(Editor()->DoButton_FontIcon(&pNode->m_Collapse, pNode->m_Collapse ? "+" : "-", pNode->m_Collapse, &ColBtn, 0, nullptr, IGraphics::CORNER_L, 8.0f))
//			pNode->m_Collapse = !pNode->m_Collapse;
//	}
//
//	bool Clicked;
//	int Res = DoBtn(&pNode->m_vpChildren, pNode->m_aName, IsSelected, &Item.m_Rect, !pNode->m_vpChildren.empty() || pNode->m_Type != 1, &Clicked);
//
//	if(Res)
//	{
//		if(!Dragging)
//		{
//			if(!Shift && !IsSelected)
//			{
//				s_Selection.clear();
//				s_Selection.insert(pNode);
//			}
//
//			if(Clicked)
//			{
//				if(Res == 1)
//				{
//					if(!Shift)
//					{
//						s_Selection.clear();
//						s_Selection.insert(pNode);
//					}
//					else
//					{
//						if(!IsSelected)
//							s_Selection.insert(pNode);
//						else
//							s_Selection.erase(pNode);
//					}
//				}
//			}
//		}
//	}
//
//	if(!pNode->m_Collapse && !pNode->m_vpChildren.empty())
//	{
//		TreeView.PushTree();
//		for(auto &Child : pNode->m_vpChildren)
//			InternalRender(TreeView, Child);
//		TreeView.PopTree();
//	}
//}
//
//static std::shared_ptr<TreeNode> GenerateTestTree(const char *pDesc)
//{
//	std::shared_ptr<TreeNode> Root = std::make_shared<TreeNode>(TreeNode{
//		"ROOT",
//		false,
//		TreeNode::TYPE_NONE,
//		{},
//	});
//
//	std::shared_ptr<TreeNode> pNode = Root;
//	std::vector<std::shared_ptr<TreeNode>> m_vpNodes;
//
//	const char *ptr = pDesc;
//	int p = 0;
//	char LastType = 0;
//
//	int aCounts[3] = {0, 0, 0};
//
//	while(ptr && *ptr)
//	{
//		if(str_isspace(*ptr))
//		{
//			ptr++;
//			continue;
//		}
//
//		if(*ptr == '(' && LastType != 'L')
//		{
//			TreeNode Node;
//			Node.m_vpChildren.emplace_back();
//			Node.m_Collapse = false;
//			if(LastType == 'G')
//			{
//				Node.m_Type = TreeNode::TYPE_LAYER_GROUP;
//				str_format(Node.m_aName, sizeof(Node.m_aName), "Group %d", aCounts[Node.m_Type - 1]++);
//			}
//			else if(LastType == 'F')
//			{
//				Node.m_Type = TreeNode::TYPE_FOLDER;
//				str_format(Node.m_aName, sizeof(Node.m_aName), "Folder %d", aCounts[Node.m_Type - 1]++);
//			}
//			Node.m_vpChildren.clear();
//
//			pNode->m_vpChildren.push_back(std::make_shared<TreeNode>(Node));
//			m_vpNodes.push_back(pNode);
//			pNode = pNode->m_vpChildren.back();
//			p++;
//		}
//		if(*ptr == ')' && !m_vpNodes.empty())
//		{
//			pNode = m_vpNodes.back();
//			m_vpNodes.pop_back();
//			LastType = 0;
//			p--;
//		}
//
//		if(*ptr == 'G' || *ptr == 'F')
//		{
//			LastType = *ptr;
//		}
//
//		if(*ptr == 'L')
//		{
//			TreeNode Node;
//			Node.m_Type = TreeNode::TYPE_LAYER;
//			str_format(Node.m_aName, sizeof(Node.m_aName), "Layer %d", aCounts[pNode->m_Type - 1]++);
//			Node.m_Collapse = false;
//			pNode->m_vpChildren.push_back(std::make_shared<TreeNode>(Node));
//		}
//
//		ptr++;
//	}
//
//	if(p != 0)
//	{
//		printf("Invalid tree\n");
//		return {};
//	}
//
//	return Root;
//}
//
//void CLayersView::OnRender(CUIRect View)
//{
//	static CTreeView s_TreeView;
//
//	CUIRect Test;
//	View.VSplitLeft(200.0f, nullptr, &Test);
//	Test.VSplitLeft(200.0f, &Test, nullptr);
//
//	static auto s_Root = GenerateTestTree({R"(
//		F(
//			F(
//				G(LLLLLLL)
//				G(LLLLL)
//				F(
//					F(
//						G(LLLLL)
//						F()
//					)
//				)
//			)
//			G()
//			F()
//			F(
//				G(L)
//				F(G(LLLL))
//				G()
//			)
//			G(LLL)
//			F(
//				G()
//				G(LL)
//				G(LLLLLLL)
//				F(
//					G(LL)
//					G()
//				)
//			)
//		)
//		G(LLLLLLLL)
//		F(
//			G(LLL)
//			G(LLLLL)
//			F()
//		)
//	)"});
//
//	//static std::shared_ptr<TreeNode> s_Root = GenerateTestTree({R"(
//	//	F(G()G(LLL)G())F()F(G()G())
//	//)"});
//
//	//static std::shared_ptr<TreeNode> s_Root = GenerateTestTree({R"(
//	//	F(G(L)G())F()
//	//)"});
//
//	//static auto s_Root = GenerateTestTree({R"(
//	//	F(G()G())
//	//)"});
//
//	//static auto s_Root = GenerateTestTree({R"(
//	//	F(
//	//		F()
//	//		F(
//	//			F()
//	//			F()
//	//		)
//	//		F(
//	//			F()
//	//		)
//	//	)
//	//	F(
//	//		F(
//	//			F(F()F())
//	//		)
//	//	)
//	//	F(F()F())
//	//)"});
//
//	s_TreeView.Start(&Test, 12.0f, 20.0f, CDropTargetInfo::Accept({TreeNode::TYPE_LAYER_GROUP, TreeNode::TYPE_FOLDER}));
//	s_TreeView.DoAutoSpacing(4.0f);
//
//	for(auto &Node : s_Root->m_vpChildren)
//		InternalRender(s_TreeView, Node);
//
//	auto Changes = s_TreeView.End();
//
//	if(s_TreeView.DragStatus() == CTreeView::EDragStatus::NOT_ALLOWED)
//		Editor()->m_CursorType = CEditor::CURSOR_NOT_ALLOWED;
//	else if(s_TreeView.DragStatus() == CTreeView::EDragStatus::MOVE_HERE)
//		Editor()->m_CursorType = CEditor::CURSOR_MOVE_HERE;
//
//	auto &&GetNode = [](const std::shared_ptr<TreeNode> &pRoot, const CTreeChanges::TPath &Path) {
//		std::shared_ptr<TreeNode> pNode = pRoot;
//		for(auto p : Path)
//			pNode = pNode->m_vpChildren[p];
//		return pNode;
//	};
//
//	if(!Changes.Empty())
//	{
//		auto To = Changes.To();
//		auto ToParent = To;
//		ToParent.pop_back();
//		int Position = To.back();
//
//		std::vector<std::shared_ptr<TreeNode>> vSubTree;
//		std::vector<std::pair<std::shared_ptr<TreeNode>, int>> vDeleteBefore, vDeleteAfter;
//
//		for(auto &Path : Changes.From())
//		{
//			auto FromParentPath = Path;
//			auto FromPosition = FromParentPath.back();
//			FromParentPath.pop_back();
//
//			auto pFromNodeParent = GetNode(s_Root, FromParentPath);
//			auto pFromNode = pFromNodeParent->m_vpChildren[FromPosition];
//
//			if(Path >= To)
//				vDeleteBefore.emplace_back(pFromNodeParent, FromPosition);
//			else if(Path < To)
//				vDeleteAfter.emplace_back(pFromNodeParent, FromPosition);
//
//			vSubTree.push_back(pFromNode);
//		}
//
//		To.pop_back();
//
//		std::sort(vDeleteBefore.begin(), vDeleteBefore.end(), [](auto &Pair1, auto &Pair2) { return Pair2.second < Pair1.second; });
//		std::sort(vDeleteAfter.begin(), vDeleteAfter.end(), [](auto &Pair1, auto &Pair2) { return Pair2.second < Pair1.second; });
//
//		for(auto &Item : vDeleteBefore)
//			Item.first->m_vpChildren.erase(Item.first->m_vpChildren.begin() + Item.second);
//
//		std::shared_ptr<TreeNode> pDest = GetNode(s_Root, To);
//		pDest->m_vpChildren.insert(pDest->m_vpChildren.begin() + minimum(Position, (int)pDest->m_vpChildren.size()), vSubTree.begin(), vSubTree.end());
//
//		for(auto &Item : vDeleteAfter)
//			Item.first->m_vpChildren.erase(Item.first->m_vpChildren.begin() + Item.second);
//	}
//}
