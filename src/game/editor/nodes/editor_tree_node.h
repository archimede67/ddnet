#ifndef GAME_EDITOR_TREE_NODE_H
#define GAME_EDITOR_TREE_NODE_H

#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using TEditorTreeNodeId = const void *;

enum class ENodeSelection
{
	ADDED,
	SELECTED,
	REMOVED,
};

struct SEditorTreeViewRenderContext
{
	CUIRect m_Rect;
	CUIRect m_NodeRect;
	CScrollRegion m_ScrollRegion;
};

using FnPopupFunction = CUi::EPopupMenuFunctionResult (*)(void *, CUIRect, bool);

struct SEditorPopupRenderInfo
{
	FnPopupFunction m_pfnPopupFunc;
	int m_Width;
	int m_Height;

	SEditorPopupRenderInfo(int Width, int Height, const FnPopupFunction &pfnPopupFunc) :
		m_Width(Width), m_Height(Height), m_pfnPopupFunc(pfnPopupFunc)
	{
	}
};

class CEditorTreeNodePath
{
public:
	explicit CEditorTreeNodePath(std::size_t Index)
	{
		m_vIndices.push_back(Index);
	}

	CEditorTreeNodePath &operator/(std::size_t Index)
	{
		m_vIndices.push_back(Index);
		return *this;
	}

	const std::vector<std::size_t> &Indices() const
	{
		return m_vIndices;
	}

private:
	std::vector<std::size_t> m_vIndices;
};

inline CEditorTreeNodePath NodePath(std::size_t Index)
{
	return CEditorTreeNodePath(Index);
}

struct SEditorNodeData
{
	TEditorTreeNodeId m_pId;
};

class CEditorTreeView;

class IEditorTreeNode : public std::enable_shared_from_this<IEditorTreeNode>
{
public:
	IEditorTreeNode() :
		m_vpChildren(), m_pTreeView(nullptr) {}
	virtual ~IEditorTreeNode() = default;

	virtual void Render(SEditorTreeViewRenderContext &Context) = 0;
	virtual SEditorPopupRenderInfo GetPopupRenderInfo(SEditorTreeViewRenderContext &Context) = 0;

	void AddChild(std::shared_ptr<IEditorTreeNode> pChild)
	{
		pChild->m_pParent = shared_from_this();
		pChild->m_pTreeView = m_pTreeView;
		m_vpChildren.push_back(std::move(pChild));
	}

	inline std::shared_ptr<IEditorTreeNode> Parent() const { return m_pParent.lock(); }
	inline CEditorTreeView *TreeView() const { return m_pTreeView; }
	inline void SetCollapsed(bool Collapsed)
	{
		m_Collapsed = Collapsed;
		OnCollapseChanged();
	}

	inline void Clear()
	{
		if(!m_vpChildren.empty())
			m_vpChildren.clear();
	}
	inline const std::vector<std::shared_ptr<IEditorTreeNode>> &Children() const { return m_vpChildren; }
	inline virtual TEditorTreeNodeId Id() const = 0;
	inline bool IsCollapsed() const { return m_Collapsed; }
	virtual bool OnSelection(ENodeSelection Selection) { return true; }

protected:
	void SetTreeView(CEditorTreeView *pTreeView)
	{
		m_pTreeView = pTreeView;
	}

	bool Selected() const;
	virtual void OnCollapseChanged() {}

private:
	std::weak_ptr<IEditorTreeNode> m_pParent;
	CEditorTreeView *m_pTreeView;
	std::vector<std::shared_ptr<IEditorTreeNode>> m_vpChildren;
	bool m_Collapsed = false;

	friend class CEditorTreeView;
};

template<typename T>
class CEditorTreeNode : public IEditorTreeNode
{
	static_assert(std::is_base_of_v<SEditorNodeData, T>, "T must be derived from SEditorNodeData");

public:
	explicit CEditorTreeNode(T Data) :
		IEditorTreeNode(), m_Data(std::move(Data)) {}

	inline T &Data() { return m_Data; }
	inline const T &Data() const { return m_Data; }

	void Render(SEditorTreeViewRenderContext &Context) override
	{
		// Default render behavior
	}

	SEditorPopupRenderInfo GetPopupRenderInfo(SEditorTreeViewRenderContext &Context) override
	{
		return SEditorPopupRenderInfo(0, 0, nullptr); // Default implementation returns no popup
	}

	TEditorTreeNodeId Id() const override
	{
		return m_Data.m_pId;
	}

private:
	T m_Data;
};

#endif