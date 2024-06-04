#include "group_ex.h"
#include "layer_group.h"

#include <game/editor/editor.h>

void CLayerGroupObject::Render()
{
	std::shared_ptr<CLayerGroup> pGroup = Map()->m_vpGroups[m_GroupIndex];

	if(!pGroup->m_Visible)
		return;

	pGroup->Render();
}

std::shared_ptr<ITreeNode> CLayerGroupObject::ToTreeNode(const std::shared_ptr<IEditorMapObject> &Self)
{
	return std::make_shared<CLayerGroupNode>(m_GroupIndex, std::static_pointer_cast<CLayerGroupObject>(Self), Map()->m_vpGroups[std::static_pointer_cast<CLayerGroupObject>(Self)->m_GroupIndex]);
}

CUi::EPopupMenuFunctionResult CLayerGroupObject::Popup(CUIRect View, int &Height)
{
	return CUi::POPUP_KEEP_OPEN;
}

std::shared_ptr<ITreeNode> CEditorParentGroup::ToTreeNode(const std::shared_ptr<IEditorMapObject> &Self)
{
	return std::make_shared<CEditorFolderNode>(std::static_pointer_cast<CEditorParentGroup>(Self));
}

CUi::EPopupMenuFunctionResult CEditorParentGroup::Popup(CUIRect View, int &Height)
{
	Height = 100;
	return CUi::POPUP_KEEP_OPEN;
}
