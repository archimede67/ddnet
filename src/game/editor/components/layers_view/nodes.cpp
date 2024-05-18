#include "nodes.h"

#include <game/editor/components/layers_view/node_refs.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/sound.h>

void CEditorMapNode::AddChild(const int Index, const std::shared_ptr<ITreeNode> &pChild)
{
	// TODO: check if node is valid child
	m_pMap->m_vpGroups.insert(m_pMap->m_vpGroups.begin() + Index,
		std::static_pointer_cast<CLayerGroupNode>(pChild)->Group());
}

void CEditorMapNode::RemoveChild(const int Index)
{
	// TODO: how to handle other stuff like folders?
	m_pMap->m_vpGroups.erase(m_pMap->m_vpGroups.begin() + Index);
}

CLayerGroupNode::CLayerGroupNode(const int Index, const std::shared_ptr<CLayerGroup> &pGroup) :
	ITreeParentNode(TYPE_LAYER_GROUP, FLAG_NO_MULTI_SELECTION), m_GroupIndex(Index), m_pGroup(pGroup)
{
	str_format(m_aName, sizeof(m_aName), "#%d %s", Index, pGroup->m_aName);
}

void CLayerGroupNode::AddChild(const int Index, const std::shared_ptr<ITreeNode> &pChild)
{
	// Maybe check if pChild can be a valid child of this node?
	m_pGroup->m_vpLayers.insert(m_pGroup->m_vpLayers.begin() + Index,
		std::static_pointer_cast<CLayerNode>(pChild)->Layer());
}

void CLayerGroupNode::RemoveChild(const int Index)
{
	m_pGroup->m_vpLayers.erase(m_pGroup->m_vpLayers.begin() + Index);
}

bool *CLayerGroupNode::Visible() { return &m_pGroup->m_Visible; }
bool *CLayerGroupNode::Collapse() { return &m_pGroup->m_Collapse; }

ENodeSelectResult CLayerGroupNode::OnSelect()
{
	// TODO: FIND HOW TO MAKE THIS BETTER
	printf("CLayerGroupNode::OnSelect() [m_GroupIndex = %d]\n", m_GroupIndex);
	Editor()->m_SelectedGroup = m_GroupIndex;

	if(/*Editor()->m_vSelectedLayers.empty() &&*/ !Group()->m_vpLayers.empty())
		Layers()->Select(CLayerNodeRef(m_GroupIndex, 0));

	return ENodeSelectResult::ALLOW;
}

void CLayerGroupNode::OnDeselect()
{
	// Deselect all children
	printf("CLayerGroupNode::OnDeselect() [m_GroupIndex = %d]\n", m_GroupIndex);
	Layers()->Deselect(CLayerGroupNodeChildrenRef(m_GroupIndex));
}

CUi::EPopupMenuFunctionResult CLayerGroupNode::Popup(CUIRect View, int &Height)
{
	Height = 256;
	return CEditor::PopupGroup(Editor(), View, true);
}

bool *CLayerNode::Visible() { return &m_pLayer->m_Visible; }
bool *CLayerNode::Collapse() { return nullptr; }

const char *CLayerNode::Name() const
{
	if(m_pLayer->m_aName[0])
		return m_pLayer->m_aName;

	if(m_pLayer->m_Type == LAYERTYPE_TILES)
	{
		std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(m_pLayer);
		return pTiles->m_Image >= 0 ? m_pLayer->m_pEditor->m_Map.m_vpImages[pTiles->m_Image]->m_aName : "Tiles";
	}

	if(m_pLayer->m_Type == LAYERTYPE_QUADS)
	{
		std::shared_ptr<CLayerQuads> pQuads = std::static_pointer_cast<CLayerQuads>(m_pLayer);
		return pQuads->m_Image >= 0 ? m_pLayer->m_pEditor->m_Map.m_vpImages[pQuads->m_Image]->m_aName : "Quads";
	}

	if(m_pLayer->m_Type == LAYERTYPE_SOUNDS)
	{
		std::shared_ptr<CLayerSounds> pSounds = std::static_pointer_cast<CLayerSounds>(m_pLayer);
		return pSounds->m_Sound >= 0 ? m_pLayer->m_pEditor->m_Map.m_vpSounds[pSounds->m_Sound]->m_aName : "Sounds";
	}

	return "Unknown";
}

ENodeSelectResult CLayerNode::OnSelect()
{
	printf("BEGIN CLayerNode::OnSelect() [m_GroupIndex = %d, m_Index = %d]\n", m_GroupIndex, m_Index);

	const bool IsDifferentGroup = Editor()->m_SelectedGroup != m_GroupIndex;
	if(IsDifferentGroup)
		Editor()->m_vSelectedLayers.clear();

	Editor()->m_vSelectedLayers.push_back(m_Index);
	Layers()->Select(CLayerGroupNodeRef(m_GroupIndex));

	printf("END CLayerNode::OnSelect() [m_GroupIndex = %d, m_Index = %d]\n", m_GroupIndex, m_Index);
	return ENodeSelectResult::ALLOW;
}

void CLayerNode::OnDeselect()
{
	printf("CLayerNode::OnDeselect() [m_GroupIndex = %d, m_Index = %d]\n", m_GroupIndex, m_Index);
	if(m_Index < (int)m_pLayer->m_pEditor->m_vSelectedLayers.size())
		Editor()->m_vSelectedLayers.erase(m_pLayer->m_pEditor->m_vSelectedLayers.begin() + m_Index);
}

CUi::EPopupMenuFunctionResult CLayerNode::Popup(const CUIRect View, int &Height)
{
	static CEditor::SLayerPopupContext s_Context;
	s_Context.m_pEditor = m_pLayer->m_pEditor;

	Height = 270;
	return CEditor::PopupLayer(&s_Context, View, true);
}

// -----------------------------------

bool *CEditorFolderNode::Visible() { return &m_pFolder->m_Visible; }
bool *CEditorFolderNode::Collapse() { return &m_pFolder->m_Collapse; }
const char *CEditorFolderNode::Name() const { return m_pFolder->m_aName; }

std::shared_ptr<IEditorMapObject> CEditorFolderNode::Object()
{
	return std::static_pointer_cast<IEditorMapObject>(Folder());
}
