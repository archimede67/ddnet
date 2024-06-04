#include "layers_view.h"

#include <engine/keys.h>
#include <iterator>

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

	m_pTreeRoot = std::make_shared<CNode>(ITreeNode::TYPE_ROOT, CTreeNodePath(), std::make_shared<CEditorMapNode>(&Editor()->m_Map));
	m_TreeNav = CTreeNavigator(m_pTreeRoot);

	m_LastSelectedNodePath = {};

	BuildTree();
}

void CLayersView::OnMapLoad()
{
	BuildTree();
}

void CLayersView::Render(CUIRect LayersBox)
{
	m_Selecting.clear();

	CEditor *pEditor = Editor();
	CEditorMap &Map = Editor()->m_Map;

	CUIRect ButtonsBar;
	LayersBox.HSplitBottom(20.0f, &LayersBox, &ButtonsBar);

	static int s_NewGroupBtn = 0;
	if(Editor()->DoButton_Editor(&s_NewGroupBtn, "New group", 0, &ButtonsBar, 0, ""))
	{
	}

	{
		m_TreeView.Start(&LayersBox, 12.0f, 14.0f, CDropTargetInfo::Accept({ITreeNode::TYPE_LAYER_GROUP, ITreeNode::TYPE_FOLDER}));
		m_TreeView.DoAutoSpacing(4.0f);

		for(unsigned int i = 0; i < (unsigned int)m_pTreeRoot->m_vpChildren.size(); i++)
		{
			const auto &pNode = m_pTreeRoot->m_vpChildren[i];
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
			auto It = m_TreeNav.At(m_LastSelectedNodePath)++;
			m_LastSelectedNodePath = It.Path();
			SelectTreeNode(*It);
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

void CLayersView::ClearSelection()
{
	m_Selecting.clear();
	while(!m_SelectedNodes.empty())
	{
		const auto pSelected = m_SelectedNodes.begin();
		pSelected->m_pNode->m_pData->OnDeselect();
		m_SelectedNodes.erase(pSelected);
	}
}

void CLayersView::DeselectType(int Type)
{
	const std::vector<std::shared_ptr<CNode>> vpNodes = FindIf(m_pTreeRoot, [Type](const auto &pNode) { return pNode->m_Type == Type; });
	for(auto &pNode : vpNodes)
	{
		if(m_SelectedNodes.count(pNode))
		{
			pNode->m_pData->OnDeselect();
			m_SelectedNodes.erase(pNode);
		}
	}
}

void CLayersView::RenderTreeNode(const std::shared_ptr<CNode> &pNode)
{
	RenderTreeNodeItem(pNode->m_pData->Name(), pNode);
}

void CLayersView::RenderTreeNodeItem(const char *pName, const std::shared_ptr<CNode> &pNode)
{
	static auto &&DoBtn = [&](const void *pId, const char *pText, const int Checked, const CUIRect *pRect, bool HasChildren, bool *pClicked, bool *pDown) {
		const auto Color = Editor()->GetButtonColor(pId, Checked);
		pRect->Draw(Color, !HasChildren ? IGraphics::CORNER_ALL : IGraphics::CORNER_R, 3.0f);
		CUIRect Label;
		pRect->VMargin(5.0f, &Label);
		Editor()->Ui()->DoLabel(&Label, pText, 10.0f, TEXTALIGN_ML);
		return Editor()->Ui()->DoDraggableButtonLogic(pId, Checked, pRect, pClicked, nullptr, pDown);
	};

	const static auto &&SelectSingleNode = [&](const std::shared_ptr<CNode> &pSelected) {
		ClearSelection();
		SelectTreeNode(pSelected);
	};

	const static auto &&ToggleSelected = [&](const std::shared_ptr<CNode> &pSelected, const bool Selected) {
		if(Selected)
		{
			pSelected->m_pData->OnDeselect();
			m_SelectedNodes.erase(pSelected);
		}
		else
			SelectTreeNode(pSelected);
	};

	const bool IsSelected = m_SelectedNodes.find(pNode) != m_SelectedNodes.end();
	const bool Dragging = m_TreeView.Dragging();

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

	CUIRect Rect = Item.m_Rect;

	// Shift is used to add multiple nodes to the selection, starting from the last selected node to the clicked node
	const bool SelectMultipleAdditiveModifier = Editor()->Input()->ShiftIsPressed();
	// Ctrl is used to add a single node to the selection
	const bool ToggleSelectionModifier = Editor()->Input()->ModifierIsPressed();

	const bool AnyModifier = SelectMultipleAdditiveModifier || ToggleSelectionModifier;

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

	bool MouseWasDown;
	const int Res = DoBtn(pNode->m_pData.get(), pName, Checked, &Item.m_Rect, ChildrenExists, &Clicked, &MouseWasDown);

	pNode->m_pData->Decorate(Rect);

	if(Res)
	{
		if(!Dragging)
		{
			if(MouseWasDown)
			{
				// Simple click: set the selected node
				if(!AnyModifier && !IsSelected)
					SelectSingleNode(pNode);
				// Ctrl click: toggle node selection
				else if(ToggleSelectionModifier)
					ToggleSelected(pNode, IsSelected);
				// Shift click: select nodes from last to current
				else if(SelectMultipleAdditiveModifier)
				{
					if(m_LastSelectedNodePath.Empty())
						SelectSingleNode(pNode);
					else
					{
						auto Range = m_TreeNav.Range(m_LastSelectedNodePath, pNode->m_Path);

						for(CTreeNavigator::CIterator It = Range.begin(); It != Range.end(); It++)
						{
							SelectTreeNode(*It);
						}

						SelectTreeNode(pNode);
					}
				}
			}

			if(Clicked)
			{
				if(Res == 1)
				{
					if(!AnyModifier)
						SelectSingleNode(pNode);
				}
				else if(Res == 2)
				{
					// Only select that node to not confuse the user
					// ClearSelection();
					// SelectTreeNode(pNode);

					Ui()->DoPopupMenu(pNode->m_pData.get(), Ui()->MouseX(), Ui()->MouseY(), 120, pNode->m_pData.get(), ITreeNode::RenderPopup);
				}
			}
		}
	}

	if(ChildrenExists && !*pCollapse)
	{
		m_TreeView.PushTree();
		for(unsigned int k = 0; k < (unsigned int)pNode->m_vpChildren.size(); k++)
		{
			RenderTreeNode(pNode->m_vpChildren[k]);
		}
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

void CLayersView::GroupSelection()
{
	std::vector<CTreeNodePath> vSelectedPaths{};
	std::transform(m_SelectedNodes.begin(), m_SelectedNodes.end(), std::back_inserter(vSelectedPaths), [](const auto &NodeInfo) { return NodeInfo.m_pNode->m_Path; });

	CTreeNodePath TargetPath = std::min_element(m_SelectedNodes.begin(), m_SelectedNodes.end(), [](const auto &a, const auto &b) { return a.m_SelectionIndex < b.m_SelectionIndex; })->m_pNode->m_Path;
	Group(vSelectedPaths, TargetPath);
}

void CLayersView::UngroupSelection()
{
}

void CLayersView::Group(const std::vector<CTreeNodePath> &vOriginalPaths, const CTreeNodePath &TargetPath)
{
	CEditorMap &Map = Editor()->m_Map;
	std::vector<CTreeNodePath> vPaths = vOriginalPaths;
	std::sort(vPaths.begin(), vPaths.end());

	unsigned TargetIndex = TargetPath.Index();

	const auto pFolder = Map.CreateObject<CEditorParentGroup>();
	str_copy(pFolder->m_aName, "Folder");

	for(const auto &Path : vPaths)
	{
		const auto pNodeParent = m_TreeNav[Path.Parent()];
		const auto pObjectParent = pNodeParent->m_pData->Object();

		const unsigned Index = Path.Index();
		const auto pNode = pNodeParent->m_vpChildren[Index];
		SetParent(pNode, pFolder, pFolder->m_vpChildren.size());
	}

	const auto pTargetParent = m_TreeNav[TargetPath.Parent()]->m_pData->Object();
	TargetIndex = minimum((size_t)TargetIndex, pTargetParent->m_vpChildren.size());
	pTargetParent->m_vpChildren.insert(pTargetParent->m_vpChildren.begin() + TargetIndex, pFolder);

	// Update tree
	BuildTree();

	// Update selection
	ClearSelection();
	SelectTreeNode(m_TreeNav[TargetPath.Parent() / TargetIndex]);
}

bool CLayersView::CanHandleInput() const
{
	return Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr;
}

void CLayersView::BuildTree()
{
	const auto Start = std::chrono::high_resolution_clock::now();

	m_pTreeRoot->Clear();
	BuildTreeNodeChildren(m_pTreeRoot, Editor()->m_Map.m_pTreeRoot->m_vpChildren);

	const auto End = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float> Duration = End - Start;
	printf("BuildTree() took %.12fs (%" PRId64 "ms)\n", Duration.count(), std::chrono::duration_cast<std::chrono::milliseconds>(Duration).count());

	// Sort groups
	const auto vGroupNodes = Query(CNodeTypeQuery(ITreeNode::TYPE_LAYER_GROUP));
	if(!vGroupNodes.empty())
	{
		std::unordered_map<const void *, std::shared_ptr<CNode>> GroupNodesMap{};
		std::transform(vGroupNodes.begin(), vGroupNodes.end(), std::inserter(GroupNodesMap, GroupNodesMap.end()), [](const auto &pNode) {
			return std::make_pair(pNode->m_pData->Id(), pNode);
		});
		auto &vpGroups = Editor()->m_Map.m_vpGroups;
		std::sort(vpGroups.begin(), vpGroups.end(), [&](const auto &a, const auto &b) {
			return GroupNodesMap[a.get()]->m_Path < GroupNodesMap[b.get()]->m_Path;
		});

		for(size_t i = 0; i < vpGroups.size(); i++)
		{
			const auto &pGroup = vpGroups.at(i);
			const auto &pNode = GroupNodesMap.at(pGroup.get());
			const auto &pGroupObj = std::static_pointer_cast<CLayerGroupObject>(pNode->m_pData->Object());

			char aBuf[64];
			pNode->m_Path.ToString(aBuf);

			printf("%s: #%d %s (%d -> %d)\n", aBuf, (int)i, pGroup->m_aName, pGroupObj->m_GroupIndex, (int)i);

			pGroupObj->m_GroupIndex = i;
			// TODO: remove all complexity, use only 1 layer of nodes :)
		}
	}
}

void CLayersView::BuildTreeNodeChildren(const std::shared_ptr<CNode> &pNode, const std::vector<std::shared_ptr<IEditorMapObject>> &vpObject)
{
	// Update selection
	static const auto &&UpdateSelection = [&](const std::shared_ptr<CNode> &pTargetNode) {
		const auto SelectedNodeIt = m_SelectedNodes.find(pTargetNode);
		if(SelectedNodeIt != m_SelectedNodes.end())
		{
			CTreeNodeInfo Info = *SelectedNodeIt;
			m_SelectedNodes.erase(SelectedNodeIt);
			m_SelectedNodes.insert({Info.m_SelectionIndex, pTargetNode});
		}
	};

	UpdateSelection(pNode);

	for(int i = 0; i < (int)vpObject.size(); i++)
	{
		auto &pObject = vpObject.at(i);
		auto pTreeNode = pObject->ToTreeNode(pObject);
		pTreeNode->m_pLayers = this;
		pTreeNode->m_pEditor = Editor();

		if(!pTreeNode->IsLeaf())
		{
			auto &pChild = pNode->AddChild(pTreeNode->Type(), pTreeNode);
			pChild->m_pNodeParent = pNode;

			if(pTreeNode->Type() == ITreeNode::TYPE_LAYER_GROUP)
			{
				// Update selection
				UpdateSelection(pChild);

				std::shared_ptr<CLayerGroupObject> pLayerGroup = std::static_pointer_cast<CLayerGroupObject>(pObject);

				auto &vpLayers = Editor()->m_Map.m_vpGroups[pLayerGroup->m_GroupIndex]->m_vpLayers;
				for(int k = 0; k < (int)vpLayers.size(); k++)
				{
					auto &pLayer = vpLayers.at(k);
					auto pLayerNode = std::make_shared<CLayerNode>(pLayerGroup->m_GroupIndex, k, pLayer);
					pLayerNode->m_pLayers = this;
					pLayerNode->m_pEditor = Editor();
					const auto &pLayerTreeNode = pChild->AddChild(pLayer->IsEntitiesLayer() ? ITreeNode::TYPE_ENTITIES_LAYER : ITreeNode::TYPE_LAYER, pLayerNode);
					pLayerTreeNode->m_pNodeParent = pChild;
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
	CTreeNodePath ToParent = To.Parent();
	int Position = To.Index();

	std::shared_ptr<CNode> pToNode = m_TreeNav[ToParent];

	std::vector<std::shared_ptr<ITreeNode>> vpSubTree;
	for(auto Iterator = Changes.From().rbegin(); Iterator != Changes.From().rend(); ++Iterator)
	{
		const CTreeNodePath &Path = *Iterator;

		CTreeNodePath ParentPath = Path.Parent();

		if(ParentPath == ToParent && Path.Index() < Position)
			Position--;

		const std::shared_ptr<CNode> pFromNodeParent = m_TreeNav[ParentPath];
		const std::shared_ptr<CNode> pFromNode = pFromNodeParent->m_vpChildren[Path.Index()];

		vpSubTree.push_back(pFromNode->m_pData);
		if(!pFromNodeParent->m_pData->IsLeaf())
		{
			std::static_pointer_cast<ITreeParentNode>(pFromNodeParent->m_pData)->RemoveChild({Path.Index()}, pFromNode->m_pData);
		}
	}

	if(!pToNode->m_pData->IsLeaf())
	{
		const std::shared_ptr<ITreeParentNode> pNode = std::static_pointer_cast<ITreeParentNode>(pToNode->m_pData);
		for(size_t Offset = 0; Offset < vpSubTree.size(); Offset++)
		{
			auto Path = ToParent / (Position + Offset);
			auto ChildIt = (vpSubTree.rbegin() + Offset);
			pNode->AddChild({Path.Index()}, *ChildIt);
		}
	}

	// Update tree
	BuildTree();
}

void CLayersView::SelectTreeNode(const std::shared_ptr<CNode> &pNode)
{
	if(m_SelectedNodes.count(pNode) || m_Selecting.count(pNode))
		return;

	m_Selecting.insert(pNode);

	switch(pNode->m_pData->OnSelect())
	{
	case ENodeSelectResult::ALLOW:
		break;
	case ENodeSelectResult::OVERRIDE:
		ClearSelection();
		break;
	case ENodeSelectResult::OVERRIDE_TYPE:
		DeselectType(pNode->m_Type);
		break;
	default:
	case ENodeSelectResult::NONE:
		return;
	}

	m_SelectedNodes.insert({m_SelectedNodes.size(), pNode});
	m_LastSelectedNodePath = pNode->m_Path;
}

void CLayersView::SetParent(const std::shared_ptr<CNode> &pNode, const std::shared_ptr<IEditorMapObject> &pParent, const int TargetPosition)
{
	const auto pCurrentParent = pNode->m_pNodeParent;
	if(pCurrentParent->m_pData->IsLeaf())
		return;

	// Remove the node from the current node's parent
	const auto pObjectParent = pCurrentParent->m_pData->Object();
	const auto NodeIt = std::find(pObjectParent->m_vpChildren.begin(), pObjectParent->m_vpChildren.end(), pNode->m_pData->Object());

	if(NodeIt != pObjectParent->m_vpChildren.end())
	{
		pObjectParent->m_vpChildren.erase(NodeIt);

		// Add the node to the new parent
		dbg_assert(TargetPosition >= 0 && TargetPosition <= (int)pParent->m_vpChildren.size(), "Invalid target position");
		pParent->m_vpChildren.insert(pParent->m_vpChildren.begin() + TargetPosition, pNode->m_pData->Object());
	}
}

std::vector<std::shared_ptr<CLayersView::CNode>> CLayersView::FindIf(const std::shared_ptr<CNode> &pRoot,
	const std::function<bool(const std::shared_ptr<CNode> &)> &fnPredicate)
{
	std::vector<std::shared_ptr<CNode>> vpNodes{};

	if(fnPredicate(pRoot))
		vpNodes.push_back(pRoot);

	for(auto &pChild : pRoot->m_vpChildren)
	{
		auto vFound = FindIf(pChild, fnPredicate);
		if(!vFound.empty())
			vpNodes.insert(vpNodes.end(), vFound.begin(), vFound.end());
	}

	return vpNodes;
}
