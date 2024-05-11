#include "nodes.h"

#include <game/editor/editor.h>
#include <game/editor/mapitems/layer_group.h>

void CEditorMapNode::AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild)
{
	// TODO: check if node is valid child
	m_pMap->m_vpGroups.insert(m_pMap->m_vpGroups.begin() + Index, std::static_pointer_cast<CLayerGroupNode>(pChild)->Group());
}

void CEditorMapNode::RemoveChild(int Index)
{
	// TODO: how to handle other stuff like folders?
	m_pMap->m_vpGroups.erase(m_pMap->m_vpGroups.begin() + Index);
}

void CLayerGroupNode::AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild)
{
	// Maybe check if pChild can be a valid child of this node?
	m_pGroup->m_vpLayers.insert(m_pGroup->m_vpLayers.begin() + Index, std::static_pointer_cast<CLayerNode>(pChild)->Layer());
}

void CLayerGroupNode::RemoveChild(int Index)
{
	m_pGroup->m_vpLayers.erase(m_pGroup->m_vpLayers.begin() + Index);
}

bool *CLayerGroupNode::Visible() { return &m_pGroup->m_Visible; }
bool *CLayerGroupNode::Collapse() { return &m_pGroup->m_Collapse; }

bool *CLayerNode::Visible() { return &m_pLayer->m_Visible; }
bool *CLayerNode::Collapse() { return nullptr; }

// -----------------------------------

void CEditorFolderNode::AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Folder()->m_vpChildren.insert(Folder()->m_vpChildren.begin() + Index, std::static_pointer_cast<CEditorFolderNode>(pChild)->Folder());
}

void CEditorFolderNode::RemoveChild(int Index)
{
	Folder()->m_vpChildren.erase(Folder()->m_vpChildren.begin() + Index);
}

bool *CEditorFolderNode::Visible() { return &m_pFolder->m_Visible; }
bool *CEditorFolderNode::Collapse() { return &m_pFolder->m_Collapse; }
