#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_LAYERS_VIEW_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_LAYERS_VIEW_H

#include "nodes.h"

#include <game/client/ui_treeview.h>

#include <game/editor/component.h>
#include <game/editor/popups.h>

#include <game/editor/mapitems/group_ex.h>

using namespace EditorPopups;

class CEditorMap;
class CLayer;

template<class TNodeData, typename EType>
class CTreeNode
{
public:
	using DataType = TNodeData;
	using NodeType = EType;

	CTreeNode() :
		m_Type(-1), m_pData(nullptr), m_Index(-1), m_vpChildren() {}
	CTreeNode(EType Type, int Index, std::shared_ptr<TNodeData> pData) :
		m_Type(static_cast<int>(Type)), m_Index(Index), m_pData(pData), m_vpChildren() {}

public:
	template<typename T>
	std::shared_ptr<CTreeNode<TNodeData, EType>> &AddChild(EType Type, int Index, const std::shared_ptr<T> &Child)
	{
		m_vpChildren.push_back(std::make_shared<CTreeNode<TNodeData, EType>>(Type, Index, std::static_pointer_cast<TNodeData>(Child)));
		return m_vpChildren.back();
	}

	void Clear()
	{
		m_vpChildren.clear();
	}

public:
	int m_Type;
	int m_Index; // Index for that type
	std::shared_ptr<TNodeData> m_pData;
	std::vector<std::shared_ptr<CTreeNode<TNodeData, EType>>> m_vpChildren;

private:
};

template<class TNode>
class CTreeNavigator
{
public:
	CTreeNavigator() :
		CTreeNavigator(nullptr) {}
	CTreeNavigator(const std::shared_ptr<TNode> &Root) :
		m_pRoot(Root) {}

	std::shared_ptr<TNode> operator[](const CTreeNodePath &Path)
	{
		if(!m_pRoot)
			return nullptr;
		auto pNode = m_pRoot;
		for(auto p : Path)
			pNode = pNode->m_vpChildren.at(p);
		return pNode;
	}

private:
	std::shared_ptr<TNode> m_pRoot;
};

class CTreeNodeInfo
{
public:
	ITreeNode::EType m_Type;
	const void *m_pNode;

	CTreeNodeInfo(const std::shared_ptr<CTreeNode<ITreeNode, ITreeNode::EType>> &pNode) :
		m_pNode(pNode->m_pData->Id()), m_Type(static_cast<ITreeNode::EType>(pNode->m_Type)) {}

	bool operator==(const CTreeNodeInfo &Other) const { return Other.m_pNode == m_pNode; }
};

class CTreeView;

class CLayersView : public CEditorComponent
{
	using CNode = CTreeNode<ITreeNode, ITreeNode::EType>;
	using CTreeNavigator = CTreeNavigator<CNode>;

public:
	void Init(CEditor *pEditor) override;
	void Render(CUIRect LayersBox);
	void OnMapLoad() override;

private:
	struct SRenderContext
	{
		bool m_ScrollToSelection;
	};

	struct SExtraRenderInfo
	{
		const char *m_pIcon;

		SExtraRenderInfo() :
			m_pIcon(nullptr) {}
	};

	struct STreeNodeInfoHasher
	{
		size_t operator()(const CTreeNodeInfo &Info) const
		{
			return std::hash<const void *>()(Info.m_pNode);
		}
	};

private:
	CTreeView m_TreeView;
	std::shared_ptr<CNode> m_pTreeRoot;
	CTreeNavigator m_TreeNav;

	bool m_ScrollToSelectionNext;

	SRenderContext m_RenderContext;
	SParentGroupPopupContext m_ParentPopupContext;

	std::unordered_set<CTreeNodeInfo, STreeNodeInfoHasher> m_vpSelectedNodes;

private:
	void RenderTreeNode(const std::shared_ptr<CNode> &pNode);
	void RenderTreeNodeItem(const char *pName, const std::shared_ptr<CNode> &pNode);

	inline void ResetRenderContext();

	// int DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra = {});

	int DoToggleIconButton(const void *pButtonId, const void *pParentId, const char *pIcon, bool Checked, const CUIRect *pRect, const char *pToolTip);
	void DoIcon(const char *pIcon, const CUIRect *pRect, float FontSize);

	bool CanHandleInput() const;

	void BuildTree();
	void BuildTreeNodeChildren(const std::shared_ptr<CNode> &pNode, const std::vector<std::shared_ptr<IEditorMapObject>> &vpObject);
	void ApplyTreeChanges(const CTreeChanges &Changes);

private:
	static constexpr float ROW_HEIGHT = 12.0f;
};

#endif
