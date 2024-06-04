#include "nodes.h"

#include <game/editor/components/layers_view/queries.h>
#include <game/editor/editor.h>

#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/sound.h>

void CEditorMapNode::AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	ITreeParentNode::AddChild(Index, pChild);

	// if(pChild->Type() == ITreeNode::TYPE_LAYER_GROUP)
	//{
	//	m_pMap->m_vpGroups.insert(m_pMap->m_vpGroups.begin() + Index.m_TypeIndex, std::static_pointer_cast<CLayerGroupNode>(pChild)->Group());
	// }
}

void CEditorMapNode::RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	ITreeParentNode::RemoveChild(Index, pChild);

	// if(pChild->Type() == ITreeNode::TYPE_LAYER_GROUP)
	//{
	//	m_pMap->m_vpGroups.erase(m_pMap->m_vpGroups.begin() + Index.m_TypeIndex);
	// }
}

std::shared_ptr<IEditorMapObject> CEditorMapNode::Object()
{
	return std::static_pointer_cast<IEditorMapObject>(m_pMap->m_pTreeRoot);
}

CLayerGroupNode::CLayerGroupNode(const int Index, const std::shared_ptr<CLayerGroupObject> &pGroupObject, const std::shared_ptr<CLayerGroup> &pGroup) :
	ITreeParentNode(TYPE_LAYER_GROUP), m_GroupIndex(Index), m_pGroupObject(pGroupObject), m_pGroup(pGroup), m_aName{}
{
}

void CLayerGroupNode::AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	// Maybe check if pChild can be a valid child of this node?
	Group()->m_vpLayers.insert(Group()->m_vpLayers.begin() + Index.m_Index, std::static_pointer_cast<CLayerNode>(pChild)->Layer());
}

void CLayerGroupNode::RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Group()->m_vpLayers.erase(Group()->m_vpLayers.begin() + Index.m_Index);
}

bool *CLayerGroupNode::Visible() { return &Group()->m_Visible; }
bool *CLayerGroupNode::Collapse() { return &Group()->m_Collapse; }

const char *CLayerGroupNode::Name()
{
	str_format(m_aName, sizeof(m_aName), "#%d %s", m_GroupIndex, Group()->m_aName);
	return m_aName;
}

ENodeSelectResult CLayerGroupNode::OnSelect()
{
	// TODO: FIND HOW TO MAKE THIS BETTER
	printf("CLayerGroupNode::OnSelect() [m_GroupIndex = %d]\n", m_GroupIndex);
	Editor()->m_SelectedGroup = m_GroupIndex;

	return ENodeSelectResult::ALLOW;
}

void CLayerGroupNode::OnDeselect()
{
	// Deselect all children
	printf("CLayerGroupNode::OnDeselect() [m_GroupIndex = %d]\n", m_GroupIndex);
	Layers()->Deselect(CLayerGroupNodeChildrenQuery(m_GroupIndex));
}

void CLayerGroupNode::Decorate(CUIRect View)
{
	if(m_GroupIndex == Editor()->m_SelectedGroup)
	{
		CUIRect Icon;
		View.VSplitRight(View.h, &View, &Icon);
		Layers()->DoIcon(FONT_ICON_PENCIL, &Icon, 8.0f);
	}
}

CUi::EPopupMenuFunctionResult CLayerGroupNode::Popup(CUIRect View, int &Height)
{
	Height = 256;
	return CEditor::PopupGroup(Editor(), View, true);
}

std::shared_ptr<IEditorMapObject> CLayerGroupNode::Object()
{
	return std::static_pointer_cast<IEditorMapObject>(m_pGroupObject);
}

bool *CLayerNode::Visible() { return &m_pLayer->m_Visible; }
bool *CLayerNode::Collapse() { return nullptr; }

const char *CLayerNode::Name()
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
		Layers()->ClearSelection();

	Editor()->m_vSelectedLayers.push_back(m_Index);
	if(IsDifferentGroup)
		Editor()->m_SelectedGroup = m_GroupIndex;

	printf("END CLayerNode::OnSelect() [m_GroupIndex = %d, m_Index = %d]\n", m_GroupIndex, m_Index);
	return ENodeSelectResult::ALLOW;
}

void CLayerNode::OnDeselect()
{
	printf("CLayerNode::OnDeselect() [m_GroupIndex = %d, m_Index = %d]\n", m_GroupIndex, m_Index);
	auto &vSelectedLayers = Editor()->m_vSelectedLayers;
	vSelectedLayers.erase(std::remove_if(vSelectedLayers.begin(), vSelectedLayers.end(), [&](const int Index) { return Index == m_Index; }), vSelectedLayers.end());
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
const char *CEditorFolderNode::Name() { return m_pFolder->m_aName; }

std::shared_ptr<IEditorMapObject> CEditorFolderNode::Object()
{
	return std::static_pointer_cast<IEditorMapObject>(Folder());
}
