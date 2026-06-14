#ifndef GAME_EDITOR_TREE_NODES_H
#define GAME_EDITOR_TREE_NODES_H

#include <game/editor/nodes/editor_tree_node.h>

struct TreeNodeGroupData : SEditorNodeData
{
	int m_GroupIndex;
};

class CEditorTreeNodeGroup : public CEditorTreeNode<TreeNodeGroupData>
{
public:
	using CEditorTreeNode<TreeNodeGroupData>::CEditorTreeNode;

	void Render(SEditorTreeViewRenderContext &Context) override;
	SEditorPopupRenderInfo GetPopupRenderInfo(SEditorTreeViewRenderContext &Context) override;
	bool OnSelection(ENodeSelection Selection) override;

protected:
	void OnCollapseChanged() override;
};

struct TreeNodeLayerData : SEditorNodeData
{
	int m_GroupIndex;
	int m_LayerIndex;
};

class CEditorTreeNodeLayer : public CEditorTreeNode<TreeNodeLayerData>
{
public:
	using CEditorTreeNode<TreeNodeLayerData>::CEditorTreeNode;

	void Render(SEditorTreeViewRenderContext &Context) override;
	bool OnSelection(ENodeSelection Selection) override;
};

#endif