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

	m_pTreeRoot = std::make_shared<CNode>(ITreeNode::TYPE_ROOT, 0, std::make_shared<CEditorMapNode>(&Editor()->m_Map));
	m_TreeNav = CTreeNavigator(m_pTreeRoot);

	BuildTree();
}

void CLayersView::OnMapLoad()
{
	BuildTree();
}

void CLayersView::Render(CUIRect LayersBox)
{
	CEditor *pEditor = Editor();
	CEditorMap &Map = Editor()->m_Map;
	// auto &SelectedGroup = Editor()->m_SelectedGroup;
	// auto &vSelectedLayers = Editor()->m_vSelectedLayers;

	CUIRect ButtonsBar;
	LayersBox.HSplitBottom(20.0f, &LayersBox, &ButtonsBar);

	static int s_NewGroupBtn = 0;
	if(Editor()->DoButton_Editor(&s_NewGroupBtn, "New group", 0, &ButtonsBar, 0, ""))
	{
	}

	{
		m_TreeView.Start(&LayersBox, 12.0f, 14.0f, CDropTargetInfo::Accept({ITreeNode::TYPE_LAYER_GROUP, ITreeNode::TYPE_FOLDER}));
		m_TreeView.DoAutoSpacing(4.0f);

		for(auto &pNode : m_pTreeRoot->m_vpChildren)
		{
			RenderTreeNode(pNode);
			m_TreeView.DoSpacing(4.0f);
		}

		auto Changes = m_TreeView.End();
		if(!Changes.Empty())
			ApplyTreeChanges(Changes);

		if(m_TreeView.DragStatus() == CTreeView::EDragStatus::NOT_ALLOWED)
			Editor()->m_CursorType = CEditor::CURSOR_NOT_ALLOWED;
		else if(m_TreeView.DragStatus() == CTreeView::EDragStatus::MOVE_HERE)
			Editor()->m_CursorType = CEditor::CURSOR_MOVE_HERE;
	}

	ResetRenderContext();

	m_RenderContext.m_ScrollToSelection = pEditor->LayerSelector()->SelectByTile() || m_ScrollToSelectionNext;
	m_ScrollToSelectionNext = false;

	static int s_InitialGroupIndex;
	static std::vector<int> s_vInitialLayerIndices;

	if(Input()->KeyPress(KEY_DOWN) && CanHandleInput())
	{
		if(Input()->ShiftIsPressed())
		{
			// if(vSelectedLayers[vSelectedLayers.size() - 1] < (int)Map.m_vpGroups[SelectedGroup]->m_vpLayers.size() - 1)
			//	Editor()->AddSelectedLayer(vSelectedLayers[vSelectedLayers.size() - 1] + 1);
		}
		else
		{
			// int CurrentLayer = 0;
			// for(const auto &Selected : vSelectedLayers)
			//	CurrentLayer = maximum(Selected, CurrentLayer);
			// Editor()->SelectLayer(CurrentLayer);

			// if(vSelectedLayers[0] < (int)Map.m_vpGroups[SelectedGroup]->m_vpLayers.size() - 1)
			//{
			//	Editor()->SelectLayer(vSelectedLayers[0] + 1);
			// }
			// else
			//{
			//	for(size_t Group = SelectedGroup + 1; Group < Map.m_vpGroups.size(); Group++)
			//	{
			//		if(!Map.m_vpGroups[Group]->m_vpLayers.empty())
			//		{
			//			Editor()->SelectLayer(0, Group);
			//			break;
			//		}
			//	}
			// }
		}
		m_ScrollToSelectionNext = true;
	}
	if(Input()->KeyPress(KEY_UP) && CanHandleInput())
	{
		if(Input()->ShiftIsPressed())
		{
			// if(vSelectedLayers[vSelectedLayers.size() - 1] > 0)
			//	Editor()->AddSelectedLayer(vSelectedLayers[vSelectedLayers.size() - 1] - 1);
		}
		else
		{
			// int CurrentLayer = std::numeric_limits<int>::max();
			// for(const auto &Selected : vSelectedLayers)
			//	CurrentLayer = minimum(Selected, CurrentLayer);
			// Editor()->SelectLayer(CurrentLayer);

			// if(vSelectedLayers[0] > 0)
			//{
			//	Editor()->SelectLayer(vSelectedLayers[0] - 1);
			// }
			// else
			//{
			//	for(int Group = SelectedGroup - 1; Group >= 0; Group--)
			//	{
			//		if(!Map.m_vpGroups[Group]->m_vpLayers.empty())
			//		{
			//			Editor()->SelectLayer(Map.m_vpGroups[Group]->m_vpLayers.size() - 1, Group);
			//			break;
			//		}
			//	}
			// }
		}
		m_ScrollToSelectionNext = true;
	}

	CUIRect AddGroupButton;
	LayersBox.HSplitTop(ROW_HEIGHT + 1.0f, &AddGroupButton, &LayersBox);
	// if(m_ScrollRegion.AddRect(AddGroupButton))
	//{
	//	AddGroupButton.HSplitTop(ROW_HEIGHT, &AddGroupButton, 0);
	//	static int s_AddGroupButton = 0;
	//	if(Editor()->DoButton_Editor(&s_AddGroupButton, "Add group", 0, &AddGroupButton, IGraphics::CORNER_R, "Adds a new group"))
	//	{
	//		Map.NewGroup();
	//		//SelectedGroup = Map.m_vpGroups.size() - 1;
	//		//Editor()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(Editor(), SelectedGroup, false));
	//	}
	// }

	// m_ScrollRegion.End();
}

void CLayersView::RenderTreeNode(const std::shared_ptr<CNode> &pNode)
{
	RenderTreeNodeItem(pNode->m_pData->Name(), pNode);
}

void CLayersView::RenderTreeNodeItem(const char *pName, const std::shared_ptr<CNode> &pNode)
{
	static auto &&DoBtn = [&](const void *pId, const char *pText, int Checked, const CUIRect *pRect, bool HasChildren, bool *pClicked) {
		auto Color = Editor()->GetButtonColor(pId, Checked);
		pRect->Draw(Color, !HasChildren ? IGraphics::CORNER_ALL : IGraphics::CORNER_R, 3.0f);
		CUIRect Label;
		pRect->VMargin(5.0f, &Label);
		Editor()->Ui()->DoLabel(&Label, pText, 10.0f, TEXTALIGN_ML);
		return Editor()->Ui()->DoDraggableButtonLogic(pId, Checked, pRect, pClicked, nullptr);
	};

	bool IsSelected = m_vpSelectedNodes.find(pNode) != m_vpSelectedNodes.end();

	bool Dragging = m_TreeView.Dragging();

	bool *pCollapse = pNode->m_pData->Collapse();
	bool *pVisible = pNode->m_pData->Visible();

	CDropTargetInfo DropTargetInfo = CDropTargetInfo::None();
	if(pNode->m_Type == ITreeNode::TYPE_FOLDER)
		DropTargetInfo = CDropTargetInfo::Accept({ITreeNode::TYPE_FOLDER, ITreeNode::TYPE_LAYER_GROUP});
	else if(pNode->m_Type == ITreeNode::TYPE_LAYER_GROUP)
	{
		std::shared_ptr<CLayerGroupNode> pGroupNode = std::static_pointer_cast<CLayerGroupNode>(pNode->m_pData);
		if(pGroupNode->Group() == Editor()->m_Map.m_pGameGroup)
			DropTargetInfo = CDropTargetInfo::Accept({ITreeNode::TYPE_LAYER, ITreeNode::TYPE_ENTITIES_LAYER});
		else // Do not accept entities layers on non game groups
			DropTargetInfo = CDropTargetInfo::Accept({ITreeNode::TYPE_LAYER});
	}

	auto Item = m_TreeView.DoNode(pNode->m_pData.get(), IsSelected, pNode->m_Type, DropTargetInfo);
	const bool Shift = Editor()->Input()->ShiftIsPressed();

	if(Item.m_IsTargetParent)
	{
		// Draw highlighted background
		Item.m_Rect.Draw(ColorRGBA(0.6f, 0.1f, 0.9f, 0.6f), IGraphics::CORNER_ALL, 3.0f);
	}

	const bool ChildrenExists = (!pNode->m_vpChildren.empty() || pNode->m_Type != 1) && pCollapse;
	if(ChildrenExists)
	{
		CUIRect ColBtn;
		Item.m_Rect.VSplitLeft(14.0f, &ColBtn, &Item.m_Rect);
		if(Editor()->DoButton_FontIcon(pCollapse, *pCollapse ? "+" : "-", *pCollapse, &ColBtn, 0, nullptr, IGraphics::CORNER_L, 8.0f))
			*pCollapse = !*pCollapse;
	}

	bool Clicked;
	int Checked = IsSelected ? 1 : 0;
	if(pNode->m_Type == ITreeNode::TYPE_ENTITIES_LAYER)
		Checked += 6;

	int Res = DoBtn(pNode->m_pData.get(), pName, Checked, &Item.m_Rect, ChildrenExists, &Clicked);

	if(Res)
	{
		if(!Dragging)
		{
			if(!Shift && !IsSelected)
			{
				m_vpSelectedNodes.clear();
				m_vpSelectedNodes.insert(pNode);
			}

			if(Clicked)
			{
				if(Res == 1)
				{
					if(!Shift)
					{
						m_vpSelectedNodes.clear();
						m_vpSelectedNodes.insert(pNode);
					}
					else
					{
						if(!IsSelected)
							m_vpSelectedNodes.insert(pNode);
						else
							m_vpSelectedNodes.erase(pNode);
					}
				}
				else if(Res == 2)
				{
					// TODO: open popup
					Ui()->DoPopupMenu(pNode->m_pData.get(), Ui()->MouseX(), Ui()->MouseY(), 120, pNode->m_pData.get(), ITreeNode::RenderPopup);
				}
			}
		}
	}

	if(ChildrenExists && !*pCollapse)
	{
		m_TreeView.PushTree();
		for(auto &Child : pNode->m_vpChildren)
			RenderTreeNode(Child);
		m_TreeView.PopTree();
	}
}

void CLayersView::ResetRenderContext()
{
	m_RenderContext.m_ScrollToSelection = false;
}

// int CLayersView::DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra)
//{
//	CEditorMap &Map = Editor()->m_Map;
//
//	CUIRect Button = *pRect;
//	CUIRect VisibleToggle;
//
//	auto pCollapse = Flags[CToggleFlags::TOGGLE_COLLAPSE];
//	auto pVisible = Flags[CToggleFlags::TOGGLE_VISIBILE];
//
//	bool RenderIcon = Extra.m_pIcon != nullptr;
//
//	if(pCollapse)
//	{
//		CUIRect CollapseToggle;
//		Button.VSplitLeft(15.0f, &CollapseToggle, &Button);
//
//		if(Editor()->DoButton_FontIcon(pCollapse, RenderIcon ? Extra.m_pIcon : (*pCollapse ? FONT_ICON_FOLDER : FONT_ICON_FOLDER_OPEN), RenderIcon ? *pCollapse : 0, &CollapseToggle, 0, "Collapse", IGraphics::CORNER_L, 8.0f))
//			*pCollapse = !*pCollapse;
//
//		RenderIcon = false;
//	}
//
//	auto Color = Editor()->GetButtonColor(pId, Checked, Ui()->MouseInside(&Button));
//	if(pVisible && !*pVisible)
//	{
//		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f));
//		Color.r *= 0.8f;
//		Color.g *= 0.7f;
//		Color.b *= 0.7f;
//	}
//	Button.Draw(Color, pCollapse ? IGraphics::CORNER_R : IGraphics::CORNER_ALL, 3.0f);
//
//	if(RenderIcon)
//	{
//		CUIRect Icon;
//		Button.VSplitLeft(12.0f, &Icon, &Button);
//		DoIcon(Extra.m_pIcon, &Icon, 8.0f);
//	}
//
//	static bool s_Drag = false;
//
//	CUIRect Rect;
//	Button.VMargin(1.0f, &Rect);
//
//	SLabelProperties Props;
//	Props.m_MaxWidth = Rect.w - 15.0f;
//	Props.m_EllipsisAtEnd = true;
//	Props.m_EnableWidthCheck = false;
//	Ui()->DoLabel(&Rect, pText, 10.0f, TEXTALIGN_ML, Props);
//
//	if(Ui()->MouseInside(&Button))
//		Editor()->ms_pUiGotContext = pId;
//
//	Editor()->UpdateTooltip(pId, &Button, pToolTip);
//	bool Clicked, Abrupted;
//	int Result = Ui()->DoDraggableButtonLogic(pId, Checked, &Button, &Clicked, &Abrupted);
//
//	// if(Result)
//	//{
//	//	if(Clicked)
//	// }
//
//	CUIRect ButtonBar;
//	Button.VMargin(1.0f, &ButtonBar);
//
//	if(pVisible)
//	{
//		ButtonBar.VSplitRight(15.0f, &ButtonBar, &VisibleToggle);
//
//		if(DoToggleIconButton(pVisible, pId, *pVisible ? FONT_ICON_EYE : FONT_ICON_EYE_SLASH, !*pVisible, &VisibleToggle, "Toggle visibility"))
//			*pVisible = !*pVisible;
//	}
//
//	TextRender()->TextColor(TextRender()->DefaultTextColor());
//
//	return Result;
// }

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

bool CLayersView::CanHandleInput() const
{
	return Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr;
}

void CLayersView::BuildTree()
{
	auto Start = std::chrono::high_resolution_clock::now();

	m_pTreeRoot->Clear();
	BuildTreeNodeChildren(m_pTreeRoot, Editor()->m_Map.m_vpRootObjects);

	auto End = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float> Duration = End - Start;
	printf("BuildTree() took %.12fs (%" PRId64 "ms)\n", Duration.count(), std::chrono::duration_cast<std::chrono::milliseconds>(Duration).count());
}

void CLayersView::BuildTreeNodeChildren(const std::shared_ptr<CNode> &pNode, const std::vector<std::shared_ptr<IEditorMapObject>> &vpObject)
{
	for(int i = 0; i < (int)vpObject.size(); i++)
	{
		auto &pObject = vpObject.at(i);
		auto pTreeNode = pObject->ToTreeNode(pObject);
		if(!pTreeNode->IsLeaf())
		{
			auto &pChild = pNode->AddChild(pTreeNode->Type(), i, pTreeNode);
			if(pTreeNode->Type() == ITreeNode::TYPE_LAYER_GROUP)
			{
				std::shared_ptr<CLayerGroupObject> pLayerGroup = std::static_pointer_cast<CLayerGroupObject>(pObject);
				printf("Adding child to tree for layer group %d\n", pLayerGroup->m_GroupIndex);

				auto &vpLayers = Editor()->m_Map.m_vpGroups[pLayerGroup->m_GroupIndex]->m_vpLayers;
				for(int i = 0; i < (int)vpLayers.size(); i++)
				{
					auto &pLayer = vpLayers.at(i);
					pChild->AddChild(pLayer->IsEntitiesLayer() ? ITreeNode::TYPE_ENTITIES_LAYER : ITreeNode::TYPE_LAYER, i, std::make_shared<CLayerNode>(pLayer));
				}
			}
			else
			{
				BuildTreeNodeChildren(pChild, pObject->m_vpChildren);
			}
		}
	}
}

void CLayersView::ApplyTreeChanges(const CTreeChanges &Changes)
{
	auto &To = Changes.To();
	CTreeNodePath ToParent = To;
	ToParent.pop_back();
	int Position = To.back();

	std::shared_ptr<CNode> pToNode = m_TreeNav[ToParent];

	std::vector<std::shared_ptr<ITreeNode>> vpSubTree;
	for(auto Iterator = Changes.From().rbegin(); Iterator != Changes.From().rend(); Iterator++)
	{
		const CTreeNodePath &Path = *Iterator;

		CTreeNodePath ParentPath = Path;
		ParentPath.pop_back();

		if(ParentPath == ToParent && Path.back() < Position)
			Position--;

		std::shared_ptr<CNode> pFromNodeParent = m_TreeNav[ParentPath];
		std::shared_ptr<CNode> pFromNode = pFromNodeParent->m_vpChildren[Path.back()];

		vpSubTree.push_back(pFromNode->m_pData);
		if(!pFromNodeParent->m_pData->IsLeaf())
		{
			std::static_pointer_cast<ITreeParentNode>(pFromNodeParent->m_pData)->RemoveChild(Path.back());
		}
	}

	if(!pToNode->m_pData->IsLeaf())
	{
		std::shared_ptr<ITreeParentNode> pNode = std::static_pointer_cast<ITreeParentNode>(pToNode->m_pData);
		for(int Offset = 0; Offset < vpSubTree.size(); Offset++)
		{
			pNode->AddChild(Position + Offset, *(vpSubTree.rbegin() + Offset));
		}
	}

	// Update tree
	BuildTree();
}