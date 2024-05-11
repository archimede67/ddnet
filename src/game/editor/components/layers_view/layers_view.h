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

class CToggleFlags
{
public:
	enum EToggle
	{
		TOGGLE_VISIBILE,
		TOGGLE_COLLAPSE,
		NUM_TOGGLES
	};

	CToggleFlags() :
		m_vpFlags(NUM_TOGGLES)
	{
		std::fill(m_vpFlags.begin(), m_vpFlags.end(), nullptr);
	}

	bool *&operator[](EToggle Toggle)
	{
		dbg_assert(Toggle != NUM_TOGGLES, "Invalid toggle flag");
		return m_vpFlags[Toggle];
	}

	bool *operator[](EToggle Toggle) const
	{
		dbg_assert(Toggle != NUM_TOGGLES, "Invalid toggle flag");
		return m_vpFlags.at(Toggle);
	}

private:
	std::vector<bool *> m_vpFlags;
};

struct SEditorSelectableInfo
{
	enum EType
	{
		TYPE_GROUP_INFO,
		TYPE_LAYER
	};

	int m_Type;
	int m_Index;
};

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

template<class TNodeData, typename EType>
class CTreeNavigator
{
public:
	CTreeNavigator() :
		CTreeNavigator(nullptr) {}
	CTreeNavigator(const std::shared_ptr<CTreeNode<TNodeData, EType>> &Root) :
		m_pRoot(Root) {}

	std::shared_ptr<CTreeNode<TNodeData, EType>> operator[](const CTreeNodePath &Path)
	{
		if(!m_pRoot)
			return nullptr;
		auto pNode = m_pRoot;
		for(auto p : Path)
			pNode = pNode->m_vpChildren.at(p);
		return pNode;
	}

private:
	std::shared_ptr<CTreeNode<TNodeData, EType>> m_pRoot;
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
	using CTreeNavigator = CTreeNavigator<ITreeNode, ITreeNode::EType>;

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

	struct STreeNodePathHasher
	{
		size_t operator()(const CTreeNodePath &v) const
		{
			std::hash<int> hasher;
			size_t seed = 0;
			for(int i : v)
			{
				seed ^= hasher(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			}
			return seed;
		}
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
	// void RenderLayersGroup(CUIRect *pRect, const CEditorGroupInfo &Info);
	// void RenderParentGroup(CUIRect *pRect, int Index, const CEditorGroupInfo &Info);

	void RenderTreeNode(const std::shared_ptr<CNode> &pNode);

	void RenderLayerNodeItem(const std::shared_ptr<CNode> &pNode);
	void RenderGroupNodeItem(const std::shared_ptr<CNode> &pNode);
	void RenderFolderNodeItem(const std::shared_ptr<CNode> &pNode);

	void RenderTreeNodeItem(const char *pName, const std::shared_ptr<CNode> &pNode);

	inline void ResetRenderContext();

	int DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra = {});
	// std::shared_ptr<IGroup> GetGroupBase(const CEditorGroupInfo &pGroupInfo);

	int DoToggleIconButton(const void *pButtonId, const void *pParentId, const char *pIcon, bool Checked, const CUIRect *pRect, const char *pToolTip);
	void DoIcon(const char *pIcon, const CUIRect *pRect, float FontSize);

	bool CanHandleInput() const;
	const char *LayerName(const std::shared_ptr<CLayer> &pLayer);

	void BuildTree();
	void ApplyTreeChanges(const CTreeChanges &Changes);

	// template<typename T, typename Iter>
	// void InsertNodes(Iter NodesBegin, Iter NodesEnd, std::vector<std::shared_ptr<T>> &vpTarget, int Position)
	//{
	//	size_t Size = NodesEnd - NodesBegin;
	//	for(int Offset = 0; Offset < Size; Offset++)
	//	{
	//		vpTarget.insert(vpTarget.begin() + Position + Offset, std::static_pointer_cast<T>(*(NodesBegin + Offset)));
	//	}
	// }

private:
	static constexpr float ROW_HEIGHT = 12.0f;
};

#endif
