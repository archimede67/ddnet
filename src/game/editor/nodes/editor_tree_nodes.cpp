#include "editor_tree_nodes.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/textrender.h>

#include <game/editor/editor.h>
#include <game/editor/editor_object.h>
#include <game/editor/editor_ui.h>
#include <game/editor/nodes/editor_tree_node.h>

void CEditorTreeNodeGroup::Render(SEditorTreeViewRenderContext &Context)
{
	const auto &data = Data();
	auto &pGroup = TreeView()->Editor()->Map()->m_vpGroups[data.m_GroupIndex];

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "#%d %s", data.m_GroupIndex, pGroup->m_aName);
	TreeView()->Editor()->DoLabel_Editor(aBuf, &Context.m_NodeRect, EditorFontSizes::MENU, TEXTALIGN_ML);
}

SEditorPopupRenderInfo CEditorTreeNodeGroup::GetPopupRenderInfo(SEditorTreeViewRenderContext &Context)
{
	//auto &State = TreeView()->Editor()->m_RenderLayersState;
	//TreeView()->Editor()->Ui()->DoPopupMenu(&State.m_PopupGroupId, TreeView()->Editor()->Ui()->MouseX(), TreeView()->Editor()->Ui()->MouseY(), 145, 256, TreeView()->Editor(), TreeView()->Editor()->PopupGroup);
	return SEditorPopupRenderInfo(145, 256, TreeView()->Editor()->PopupGroup);
}

bool CEditorTreeNodeGroup::OnSelection(ENodeSelection Selection)
{
	switch(Selection)
	{
	case ENodeSelection::ADDED:
		// Handle the case when the node is added to the selection
		TreeView()->Editor()->Map()->m_SelectedGroup = Data().m_GroupIndex;
		return true;
	case ENodeSelection::SELECTED:
		// Handle the case when the node is the only one selected
		TreeView()->Editor()->Map()->m_SelectedGroup = Data().m_GroupIndex;
		return true;
	case ENodeSelection::REMOVED:
		return false;
	}
}

void CEditorTreeNodeGroup::OnCollapseChanged()
{
	const auto &data = Data();
	auto &pGroup = TreeView()->Editor()->Map()->m_vpGroups[data.m_GroupIndex];
	pGroup->m_Collapse = IsCollapsed();
}

void CEditorTreeNodeLayer::Render(SEditorTreeViewRenderContext &Context)
{
	const auto &data = Data();
	auto &pGroup = TreeView()->Editor()->Map()->m_vpGroups[data.m_GroupIndex];
	auto &pLayer = pGroup->m_vpLayers[data.m_LayerIndex];

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "#%d %s", data.m_LayerIndex, pLayer->m_aName);
	TreeView()->Editor()->DoLabel_Editor(aBuf, &Context.m_NodeRect, EditorFontSizes::MENU, TEXTALIGN_ML);
}

bool CEditorTreeNodeLayer::OnSelection(ENodeSelection Selection)
{
	auto CurrentGroup = TreeView()->Editor()->Map()->m_SelectedGroup;
	if(Data().m_GroupIndex != CurrentGroup && Selection != ENodeSelection::SELECTED)
		return false;

	switch(Selection)
	{
	case ENodeSelection::ADDED:
		TreeView()->Editor()->Map()->AddSelectedLayer(Data().m_LayerIndex);
		break;
	case ENodeSelection::SELECTED:
		TreeView()->Editor()->Map()->SelectLayer(Data().m_LayerIndex, Data().m_GroupIndex);
		break;
	case ENodeSelection::REMOVED:
		TreeView()->Editor()->Map()->RemoveSelectedLayer(Data().m_LayerIndex);
		break;
	}

	return true;
}
