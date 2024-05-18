#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_REFS_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_REFS_H

#include "nodes.h"

#include <memory>

struct CLayerNodeRef
{
	int m_GroupIndex;
	int m_LayerIndex;

	explicit CLayerNodeRef(const int GroupIndex, const int LayerIndex) :
		m_GroupIndex(GroupIndex), m_LayerIndex(LayerIndex) {}

	bool operator()(const std::shared_ptr<ITreeNode> &pNode) const
	{
		if(const auto pLayerNode = GetLayerNode(pNode))
			return pLayerNode->m_Index == m_LayerIndex && pLayerNode->m_GroupIndex == m_GroupIndex;

		return false;
	}

	static std::shared_ptr<CLayerNode> GetLayerNode(const std::shared_ptr<ITreeNode> &pNode)
	{
		if(pNode->Type() == ITreeNode::TYPE_LAYER || pNode->Type() == ITreeNode::TYPE_ENTITIES_LAYER)
			return std::static_pointer_cast<CLayerNode>(pNode);
		return nullptr;
	}
};

struct CLayerGroupNodeRef
{
	int m_GroupIndex;

	explicit CLayerGroupNodeRef(const int GroupIndex) :
		m_GroupIndex(GroupIndex) {}

	bool operator()(const std::shared_ptr<ITreeNode> &pNode) const
	{
		if(pNode->Type() != ITreeNode::TYPE_LAYER_GROUP)
			return false;

		const std::shared_ptr<CLayerGroupNode> &pLayerGroupNode = std::static_pointer_cast<CLayerGroupNode>(pNode);
		return pLayerGroupNode->m_GroupIndex == m_GroupIndex;
	}
};

struct CLayerGroupNodeChildrenRef
{
	int m_GroupIndex;

	explicit CLayerGroupNodeChildrenRef(const int GroupIndex) :
		m_GroupIndex(GroupIndex) {}

	bool operator()(const std::shared_ptr<ITreeNode> &pNode) const
	{
		if(const auto pLayerNode = CLayerNodeRef::GetLayerNode(pNode))
			return pLayerNode->m_GroupIndex == m_GroupIndex;

		return false;
	}
};

#endif
