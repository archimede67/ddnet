#include "editor_tree_view.h"

#include <engine/font_icons.h>

#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>
#include <game/editor/component.h>
#include <game/editor/editor.h>
#include <game/editor/editor_object.h>
#include <game/editor/nodes/editor_tree_node.h>
#include <game/editor/nodes/editor_tree_nodes.h>

#include <memory>
#include <vector>

void CEditorTreeView::OnInit(CEditor *pEditor)
{
	CEditorComponent::OnInit(pEditor);
	m_pRoot = std::make_shared<CEditorTreeNode<SEditorTreeRootNodeData>>(SEditorTreeRootNodeData{});
	m_pRoot->SetTreeView(this);
}

void CEditorTreeView::OnReset()
{
	Clear();
}

void CEditorTreeView::OnMapLoad()
{
	Rebuild();
}

void CEditorTreeView::RenderView(CUIRect Rect)
{
	m_Context.m_Rect = Rect;

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarWidth = 10.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ScrollUnit = 20.0f * 5.0f;
	m_Context.m_ScrollRegion.Begin(&m_Context.m_Rect, &ScrollParams);

	auto Children = Root()->Children();
	for(const auto &pChild : Children)
	{
		RenderNode(pChild, 0);
	}

	m_Context.m_ScrollRegion.End();
}

void CEditorTreeView::Rebuild()
{
	Clear();

	TEditorTreeNodeId Id = 0;

	for(int g = 0; g < (int)Map()->m_vpGroups.size(); g++)
	{
		// Add editor group node
		auto groupNode = AddChild<CEditorTreeNodeGroup>(Root(), TreeNodeGroupData{Map()->m_vpGroups[g].get(), g});
		groupNode->SetCollapsed(Map()->m_vpGroups[g]->m_Collapse);

		// Add layer nodes
		for(int i = 0; i < (int)Map()->m_vpGroups[g]->m_vpLayers.size(); i++)
		{
			// Add layer node
			AddChild<CEditorTreeNodeLayer>(groupNode, TreeNodeLayerData{Map()->m_vpGroups[g]->m_vpLayers[i].get(), g, i});
		}
	}
}

std::shared_ptr<IEditorTreeNode> CEditorTreeView::FindNodeById(TEditorTreeNodeId Id) const
{
	auto It = m_NodeLookup.find(Id);

	if(It == m_NodeLookup.end())
		return nullptr;

	return It->second.lock();
}

std::vector<std::shared_ptr<IEditorTreeNode>> CEditorTreeView::GetSelectedNodes() const
{
	std::vector<std::shared_ptr<IEditorTreeNode>> Result;
	Result.reserve(m_vSelectedNodes.size());

	for(TEditorTreeNodeId Id : m_vSelectedNodes)
	{
		if(auto Node = FindNodeById(Id))
		{
			Result.push_back(Node);
		}
	}

	return Result;
}

void CEditorTreeView::RenderNode(const std::shared_ptr<IEditorTreeNode> &pNode, int Depth)
{
	if(!pNode)
		return;

	m_Context.m_Rect.HSplitTop(20.0f, &m_Context.m_NodeRect, &m_Context.m_Rect);
	m_Context.m_NodeRect.HSplitBottom(4.0f, &m_Context.m_NodeRect, nullptr);
	m_Context.m_NodeRect.VSplitLeft(Depth * 4.0f, nullptr, &m_Context.m_NodeRect);
	if(m_Context.m_ScrollRegion.AddRect(m_Context.m_NodeRect))
	{
		CUIRect CollapseRect;
		if(pNode->Children().size() > 0)
		{
			m_Context.m_NodeRect.VSplitLeft(20.0f, &CollapseRect, &m_Context.m_NodeRect);
			if(Editor()->DoButton_FontIcon(&pNode->m_Collapsed, pNode->m_Collapsed ? FontIcon::FOLDER : FontIcon::FOLDER_OPEN, 0, &CollapseRect, BUTTONFLAG_LEFT, "Toggle collapse state", 0))
			{
				pNode->SetCollapsed(!pNode->m_Collapsed);
			}
		}

		const void *pId = pNode.get();
		m_Context.m_NodeRect.Draw(Editor()->GetButtonColor(pId, IsSelected(pNode)), 0, 3.0f);
		auto Rect = m_Context.m_NodeRect;
		auto Button = Editor()->Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_ALL);
		pNode->Render(m_Context);

		if(Button == 1)
		{
			DoSelect(pNode);
			if(Editor()->Ui()->DoDoubleClickLogic(pId))
			{
				pNode->SetCollapsed(!pNode->m_Collapsed);
			}
		}
		else if(Button == 2)
		{
			auto popupRenderInfo = pNode->GetPopupRenderInfo(m_Context);
			if(popupRenderInfo.m_pfnPopupFunc)
			{
				Editor()->Ui()->DoPopupMenu(&Editor()->m_RenderLayersState.m_PopupGroupId, Editor()->Ui()->MouseX(), Editor()->Ui()->MouseY(), popupRenderInfo.m_Width, popupRenderInfo.m_Height, Editor(), popupRenderInfo.m_pfnPopupFunc);
			}
		}
	}

	if(!pNode->m_Collapsed)
	{
		auto Children = pNode->Children();
		for(const auto &child : Children)
		{
			RenderNode(child, Depth + 1);
		}
	}
}

void CEditorTreeView::DoSelect(const std::shared_ptr<IEditorTreeNode> &pNode)
{
	if(Input()->ShiftIsPressed())
	{
		if(m_vSelectedNodes.size() > 1 || !IsSelected(pNode))
			Toggle(pNode);
	}
	else
	{
		SelectOnly(pNode);
	}
}
