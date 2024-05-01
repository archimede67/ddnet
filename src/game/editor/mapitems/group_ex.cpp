#include "group_ex.h"
#include "layer_group.h"

#include <game/editor/editor.h>

void CEditorParentGroup::Render()
{
	for(auto &pChild : m_vpChildren)
		pChild->Render();
}

void CLayerGroupObject::Render()
{
	std::shared_ptr<CLayerGroup> pGroup = Map()->m_vpGroups[m_GroupIndex];

	if(!pGroup->m_Visible)
		return;

	pGroup->Render();
}
