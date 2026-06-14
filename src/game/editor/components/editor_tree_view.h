#ifndef GAME_EDITOR_COMPONENTS_EDITOR_TREE_VIEW_H
#define GAME_EDITOR_COMPONENTS_EDITOR_TREE_VIEW_H

#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/editor/component.h>
#include <game/editor/nodes/editor_tree_node.h>

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CEditor;

struct SEditorTreeRootNodeData : SEditorNodeData
{
};

class CEditorTreeView : public CEditorComponent
{
public:
	CEditorTreeView() = default;

	inline const std::shared_ptr<CEditorTreeNode<SEditorTreeRootNodeData>> &Root() { return m_pRoot; }
	inline void Clear()
	{
		if(m_pRoot)
			m_pRoot->Clear();
	}

	template<typename TNode, typename... TArgs>
	std::shared_ptr<TNode> AddChild(
		const std::shared_ptr<IEditorTreeNode> &pNodeParent,
		TArgs &&...Args)
	{
		static_assert(std::is_base_of_v<IEditorTreeNode, TNode>);

		auto Child = std::make_shared<TNode>(std::forward<TArgs>(Args)...);
		RegisterNode(Child);

		pNodeParent->AddChild(Child);
		return Child;
	}

	void OnInit(CEditor *pEditor) override;
	void OnReset() override;
	void OnMapLoad() override;
	void RenderView(CUIRect Rect);
	void Rebuild();

	inline void ClearSelection() { m_vSelectedNodes.clear(); }
	inline bool IsSelected(const IEditorTreeNode *pNode) const { return m_vSelectedNodes.contains(pNode->Id()); }
	inline bool IsSelected(const std::shared_ptr<IEditorTreeNode> &pNode) const { return IsSelected(pNode.get()); }
	std::shared_ptr<IEditorTreeNode> FindNodeById(TEditorTreeNodeId Id) const;
	std::vector<std::shared_ptr<IEditorTreeNode>> GetSelectedNodes() const;

private:
	void RenderNode(const std::shared_ptr<IEditorTreeNode> &pNode, int Depth);
	void DoSelect(const std::shared_ptr<IEditorTreeNode> &pNode);

	inline void Select(const std::shared_ptr<IEditorTreeNode> &pNode)
	{
		if(pNode)
			m_vSelectedNodes.insert(pNode->Id());
	}
	inline void Deselect(const std::shared_ptr<IEditorTreeNode> &pNode) { m_vSelectedNodes.erase(pNode->Id()); }
	inline void Toggle(const std::shared_ptr<IEditorTreeNode> &pNode)
	{
		if(!pNode)
			return;

		if(IsSelected(pNode))
		{
			if(pNode->OnSelection(ENodeSelection::REMOVED))
				Deselect(pNode);
		}
		else
		{
			if(pNode->OnSelection(ENodeSelection::ADDED))
				Select(pNode);
		}
	}
	void SelectOnly(const std::shared_ptr<IEditorTreeNode> &pNode)
	{
		if(pNode->OnSelection(ENodeSelection::SELECTED))
		{
			ClearSelection();
			Select(pNode);
		}
	}

	inline void RegisterNode(const std::shared_ptr<IEditorTreeNode> &pNode)
	{
		m_NodeLookup[pNode->Id()] = pNode;
	}

	inline void UnregisterNode(TEditorTreeNodeId Id)
	{
		m_NodeLookup.erase(Id);
	}

private:
	std::shared_ptr<CEditorTreeNode<SEditorTreeRootNodeData>> m_pRoot;
	SEditorTreeViewRenderContext m_Context;
	std::unordered_set<TEditorTreeNodeId> m_vSelectedNodes;
	TEditorTreeNodeId m_NextNodeId = 0;
	std::unordered_map<TEditorTreeNodeId, std::weak_ptr<IEditorTreeNode>> m_NodeLookup;
};

#endif