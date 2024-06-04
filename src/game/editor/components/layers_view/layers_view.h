#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_LAYERS_VIEW_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_LAYERS_VIEW_H

#include "nodes.h"

#include <game/client/ui_treeview.h>

#include <game/editor/component.h>
#include <game/editor/components/layers_view/queries.h>
#include <game/editor/components/layers_view/traits.h>
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
		m_Type(-1), m_pData(nullptr), m_vpChildren(), m_pNodeParent(nullptr) {}
	CTreeNode(EType Type, const CTreeNodePath &Path, std::shared_ptr<TNodeData> pData) :
		m_Type(static_cast<int>(Type)), m_Path(Path), m_pData(std::move(pData)), m_vpChildren(), m_pNodeParent(nullptr) {}

public:
	template<typename T>
	std::shared_ptr<CTreeNode> &AddChild(EType Type, const std::shared_ptr<T> &Child)
	{
		m_vpChildren.push_back(std::make_shared<CTreeNode>(Type, m_Path / static_cast<int>(m_vpChildren.size()), std::static_pointer_cast<TNodeData>(Child)));
		return m_vpChildren.back();
	}

	void Clear()
	{
		m_vpChildren.clear();
	}

public:
	int m_Type; // Type of the node
	CTreeNodePath m_Path; // Path to the node
	std::shared_ptr<TNodeData> m_pData;
	std::vector<std::shared_ptr<CTreeNode>> m_vpChildren;
	std::shared_ptr<CTreeNode> m_pNodeParent;
};

template<class TNode>
class CTreeNavigator
{
public:
	class CIterator
	{
		using Self = CIterator;
		friend class CBetween;
		friend CTreeNavigator;

	private:
		CIterator(CTreeNavigator *pNavigator, const CTreeNodePath &Path)
		{
			m_CurrentPath = Path;
			m_pCurrentNode = (*pNavigator)[Path];
		}

		std::pair<CTreeNodePath, std::shared_ptr<TNode>> Next(const CTreeNodePath &Path, const std::shared_ptr<TNode> &pNode, const int Dir)
		{
			if(Path.Empty())
				return std::make_pair(CTreeNodePath{}, nullptr);

			dbg_assert(pNode != nullptr, "Node cannot be null");

			if(!pNode->m_vpChildren.empty())
			{
				auto NextPath = Path / 0;
				if(Dir > 0 ? NextPath > m_CurrentPath : NextPath < m_CurrentPath)
					return std::make_pair(NextPath, pNode->m_vpChildren[0]);
			}

			const unsigned Index = Path.Index();
			auto &pNodeParent = pNode->m_pNodeParent;

			// No next node, have to go up by one at least
			if(Index + (Dir > 0 ? 1 : -1) >= pNodeParent->m_vpChildren.size())
				return Next(Path.Parent(), pNodeParent, Dir);

			return std::make_pair(Dir > 0 ? Path.NextSibling() : Path.PreviousSibling(), pNodeParent->m_vpChildren[Index + 1]);
		}

	public:
		std::shared_ptr<TNode> &operator->() { return m_pCurrentNode; }
		std::shared_ptr<TNode> &operator*() { return m_pCurrentNode; }
		const CTreeNodePath &Path() const { return m_CurrentPath; }

		Self &operator++(int)
		{
			auto [Path, pNode] = Next(m_CurrentPath, m_pCurrentNode, 1);
			m_CurrentPath = Path;
			m_pCurrentNode = pNode;

			return *this;
		}

		Self &operator--(int)
		{
			auto [Path, pNode] = Next(m_CurrentPath, m_pCurrentNode, -1);
			m_CurrentPath = Path;
			m_pCurrentNode = pNode;

			return *this;
		}

		bool operator==(const Self &Other) { return Other.m_pCurrentNode == m_pCurrentNode; }
		bool operator!=(const Self &Other) { return !operator==(Other); }

	private:
		std::shared_ptr<TNode> m_pCurrentNode;
		CTreeNodePath m_CurrentPath;
	};

	class CRange
	{
	public:
		CRange(CTreeNavigator *pNavigator, CTreeNodePath From, CTreeNodePath To) :
			m_pNavigator(pNavigator), m_From(std::move(From)), m_To(std::move(To))
		{
		}

		CIterator begin() { return CIterator(m_pNavigator, m_From); }
		CIterator end() { return CIterator(m_pNavigator, m_To); }

	private:
		CTreeNavigator *m_pNavigator;
		CTreeNodePath m_From;
		CTreeNodePath m_To;
	};

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

	CRange Range(const CTreeNodePath &From, const CTreeNodePath &To)
	{
		if(To < From)
			return CRange(this, To, From);
		return CRange(this, From, To);
	}

	CIterator At(const CTreeNodePath &Path) { return CIterator(this, Path); }

private:
	std::shared_ptr<TNode> m_pRoot;
};

class CTreeNodeInfo
{
public:
	ITreeNode::EType m_Type;
	std::shared_ptr<CTreeNode<ITreeNode, ITreeNode::EType>> m_pNode;
	const void *m_pNodeId;
	size_t m_SelectionIndex;

	CTreeNodeInfo(const size_t Index, const std::shared_ptr<CTreeNode<ITreeNode, ITreeNode::EType>> &pNode) :
		m_Type(static_cast<ITreeNode::EType>(pNode->m_Type)), m_pNode(pNode), m_pNodeId(pNode->m_pData->Id()), m_SelectionIndex(Index)
	{
	}

	CTreeNodeInfo(const std::shared_ptr<CTreeNode<ITreeNode, ITreeNode::EType>> &pNode) :
		CTreeNodeInfo(-1, pNode) {}

	CTreeNodeInfo() :
		CTreeNodeInfo(nullptr) {}
	CTreeNodeInfo(std::nullptr_t) :
		m_Type(ITreeNode::TYPE_NONE), m_pNode(nullptr), m_pNodeId(nullptr), m_SelectionIndex(-1)
	{
	}

	bool operator==(const CTreeNodeInfo &Other) const { return Other.m_pNodeId == m_pNodeId; }
	bool operator==(std::nullptr_t) const { return m_pNodeId == nullptr; }
	bool operator!=(const CTreeNodeInfo &Other) const { return !operator==(Other); }
	bool operator!=(std::nullptr_t) const { return !operator==(nullptr); }
};

class CTreeView;

class CLayersView final : public CEditorComponent
{
	struct STreeNodeInfoHasher
	{
		size_t operator()(const CTreeNodeInfo &Info) const
		{
			return std::hash<const void *>()(Info.m_pNodeId);
		}
	};

	using CNode = CTreeNode<ITreeNode, ITreeNode::EType>;
	using CTreeNavigator = CTreeNavigator<CNode>;
	using CNodeSet = std::unordered_set<CTreeNodeInfo, STreeNodeInfoHasher>;

public:
	CLayersView() = default;

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

private:
	CTreeView m_TreeView;
	std::shared_ptr<CNode> m_pTreeRoot;
	CTreeNavigator m_TreeNav;

	bool m_ScrollToSelectionNext;

	SRenderContext m_RenderContext;
	SParentGroupPopupContext m_ParentPopupContext;

	CNodeSet m_SelectedNodes;
	CTreeNodePath m_LastSelectedNodePath;
	CNodeSet m_Selecting;

	std::unordered_map<CNodeQueryHash, std::vector<CTreeNodeInfo>, CNodeQueryHash::Hasher> m_SelectionQueryCache;

public:
	// Select node(s) based on a node reference
	template<typename Query>
	void Select(const Query &SearchQuery)
	{
		auto vpNodes = FindIf(m_pTreeRoot, [SearchQuery](const auto &pNode) { return SearchQuery(pNode->m_pData); });
		for(auto &pNode : vpNodes)
			SelectTreeNode(pNode);
	}

	// Deselect node(s) based on a node reference
	template<typename Query>
	void Deselect(const Query &SearchQuery)
	{
		auto vpNodes = FindIf(m_pTreeRoot, [SearchQuery](const auto &pNode) { return SearchQuery(pNode->m_pData); });
		for(auto &pNode : vpNodes)
		{
			pNode->m_pData->OnDeselect();
			m_SelectedNodes.erase(pNode);
		}
	}

	template<typename Query>
	std::vector<std::shared_ptr<CNode>> Query(const Query &SearchQuery)
	{
		return FindIf(m_pTreeRoot, [SearchQuery](const auto &pNode) { return SearchQuery(pNode->m_Path, pNode->m_pData); });
	}

	template<typename Query, std::enable_if_t<QueryTraits::IsHashed<Query>::value, bool> = true>
	std::vector<CTreeNodeInfo> SelectedNodes(const Query &SearchQuery)
	{
		const CNodeQueryHash QueryHash = CNodeQueryHash::Combine(typeid(Query).hash_code(), SearchQuery.Hash());
		const auto It = m_SelectionQueryCache.find(QueryHash);
		if(It == m_SelectionQueryCache.end())
		{
			auto &Entry = m_SelectionQueryCache[QueryHash];
			Entry = {};
			for(auto &Selected : m_SelectedNodes)
			{
				if(SearchQuery(Selected.m_pNode->m_pData))
					Entry.push_back(Selected);
			}
			return Entry;
		}
		return It->second;
	}

	const CNodeSet &Selection() { return m_SelectedNodes; }

	void ClearSelection();
	void DeselectType(int Type);

	void DoIcon(const char *pIcon, const CUIRect *pRect, float FontSize);

	void GroupSelection();
	void UngroupSelection();

	void Group(const std::vector<CTreeNodePath> &vOriginalPaths, const CTreeNodePath &TargetPath);

private:
	void RenderTreeNode(const std::shared_ptr<CNode> &pNode);
	void RenderTreeNodeItem(const char *pName, const std::shared_ptr<CNode> &pNode);

	inline void ResetRenderContext();

	// int DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra = {});

	int DoToggleIconButton(const void *pButtonId, const void *pParentId, const char *pIcon, bool Checked, const CUIRect *pRect, const char *pToolTip);
	bool CanHandleInput() const;

	void BuildTree();
	void BuildTreeNodeChildren(const std::shared_ptr<CNode> &pNode, const std::vector<std::shared_ptr<IEditorMapObject>> &vpObject);
	void ApplyTreeChanges(const CTreeChanges &Changes);

	void SelectTreeNode(const std::shared_ptr<CNode> &pNode);
	static void SetParent(const std::shared_ptr<CNode> &pNode, const std::shared_ptr<IEditorMapObject> &pParent, int TargetPosition);

	template<typename Ref>
	static std::shared_ptr<CNode> Find(const std::shared_ptr<CNode> &pRoot, const Ref &NodeRef)
	{
		if(NodeRef(pRoot))
			return pRoot;

		for(auto &pChild : pRoot->m_vpChildren)
		{
			if(const auto pFound = Find(pChild, NodeRef))
				return pFound;
		}

		return nullptr;
	}
	static std::vector<std::shared_ptr<CNode>> FindIf(const std::shared_ptr<CNode> &pRoot, const std::function<bool(const std::shared_ptr<CNode> &)> &fnPredicate);

private:
	static constexpr float ROW_HEIGHT = 12.0f;
};

#endif
