#ifndef GAME_CLIENT_UI_TREEVIEW_H
#define GAME_CLIENT_UI_TREEVIEW_H

#include "ui.h"
#include "ui_rect.h"
#include "ui_scrollregion.h"

#include <iterator>
#include <unordered_set>

// A path is simply a list of numbers, using a convenient name here.
// It is mainly used to identify a node (the path to a node) within the tree.
using CTreeNodePath = std::vector<unsigned int>;

// Creates a child path
inline CTreeNodePath operator/(const CTreeNodePath &Path, const unsigned Rhs)
{
	auto NewPath = Path;
	NewPath.push_back(Rhs);
	return NewPath;
}

inline CTreeNodePath operator/=(const CTreeNodePath &Path, const unsigned Rhs)
{
	return Path / Rhs;
}

// Go up one level, effectively returns parent path
inline CTreeNodePath operator--(const CTreeNodePath &Path, int)
{
	auto ParentPath = Path;
	ParentPath.pop_back();
	return ParentPath;
}

// Go to next sibling
inline CTreeNodePath operator++(const CTreeNodePath &Path)
{
	auto SiblingPath = Path;
	SiblingPath.back()++;
	return SiblingPath;
}

// Go to previous sibling
inline CTreeNodePath operator--(const CTreeNodePath &Path)
{
	auto SiblingPath = Path;
	if(SiblingPath.back() > 0)
		SiblingPath.back()--;
	return SiblingPath;
}

struct CTreeViewItem
{
	CUIRect m_Rect; // The rect of the element in the tree
	bool m_IsTargetParent; // Whether this item is a drag target parent
	CTreeNodePath m_Path; // The path to this node/item
};

// A class describing a drop target
class CDropTargetInfo
{
public:
	// Creates a drop target accepting nothing
	static CDropTargetInfo None()
	{
		CDropTargetInfo Info;
		Info.m_IsDropTarget = false;
		return Info;
	}

	// Creates a drop target accepting specific types
	static CDropTargetInfo Accept(const std::unordered_set<int> &vTypes)
	{
		CDropTargetInfo Info;
		Info.m_IsDropTarget = true;
		Info.m_vAcceptTypes = vTypes;
		return Info;
	}

	// Creates a drop target accepting everything
	static CDropTargetInfo AcceptAll()
	{
		CDropTargetInfo Info;
		Info.m_IsDropTarget = true;
		Info.m_vAcceptTypes.clear();
		return Info;
	}

public:
	// Checks if a specific type is accepted in this drop target
	bool IsAccepting(int Type) const
	{
		return m_IsDropTarget && (m_vAcceptTypes.empty() || m_vAcceptTypes.find(Type) != m_vAcceptTypes.end());
	}

private:
	CDropTargetInfo() :
		m_IsDropTarget(false)
	{
	}

private:
	bool m_IsDropTarget; // Indicates if this is a drop target that is accepting something
	std::unordered_set<int> m_vAcceptTypes; // Stores the accepted types

	friend class CTreeView;
};

// A class describing changes within the tree
class CTreeChanges
{
private:
	CTreeChanges()
	{
	}

	CTreeChanges(const std::vector<CTreeNodePath> &vFrom, CTreeNodePath To) :
		m_vFrom(vFrom), m_To(std::move(To))
	{
	}

public:
	// Gets the list of initial paths (parent paths that were selected)
	const std::vector<CTreeNodePath> &From() const { return m_vFrom; }

	// Gets the destination path
	const CTreeNodePath &To() const { return m_To; }

	// Checks if there are any changes
	bool Empty() const { return m_vFrom.empty(); }

private:
	std::vector<CTreeNodePath> m_vFrom;
	CTreeNodePath m_To;

	friend class CTreeView;
};

// The main class to handle the logic of a treeview, with drag-and-drop support.
// Notes:
// - The changes within the tree must be handled outside, after calling End().
//   This is because the logic only computes the changes, and has no idea of the
//   underlying data structure used to store the tree.
// - Instances of this class must be either static or a member of a class
class CTreeView : private CUIElementBase
{
private:
	class CContext
	{
	public:
		CUIRect m_View;
		std::vector<CUIRect> m_vSubTrees;
		float m_Indent;
		int m_ItemIndex;

		void Begin(const CUIRect &View, const float Indent)
		{
			m_vSubTrees.clear();
			m_View = View;
			m_Indent = Indent;
			m_vSubTrees.push_back(View);
			m_ItemIndex = 0;
		}

		void NextSlot(const float Cut, CUIRect *pSlot)
		{
			m_View.HSplitTop(Cut, pSlot, &m_View);
		}

		void Push()
		{
			m_View.VSplitLeft(m_Indent, nullptr, &m_View);
			m_vSubTrees.push_back(m_View);
		}

		void Pop()
		{
			m_View.VSplitLeft(-m_Indent, nullptr, &m_View);
			m_vSubTrees.pop_back();
		}

		CUIRect CurrentSubTree() const
		{
			CUIRect SubTree = m_vSubTrees.back();
			SubTree.h = m_View.y - SubTree.y;
			return SubTree;
		}
	};

	struct CDraggedContext : CContext
	{
		std::unordered_set<int> m_vTypes;
		std::vector<CTreeNodePath> m_vOriginalPaths;
	};

public:
	enum class EDragStatus
	{
		NONE,
		NORMAL,
		NOT_ALLOWED,
		MOVE_HERE,
	};

public:
	CTreeView();

	void Start(CUIRect *pView, float Indent, float ItemHeight,
		const CDropTargetInfo &RootDropTargetInfo = CDropTargetInfo::AcceptAll());
	CTreeViewItem DoNode(const void *pId, bool Selected, int Type, const CDropTargetInfo &DropTargetInfo);
	void PushTree();
	void PopTree();
	CTreeChanges End();

	void DoAutoSpacing(float Spacing);
	void DoSpacing(float Spacing);

	bool Dragging() const;
	EDragStatus DragStatus() const { return m_DragStatus; }
	const CTreeNodePath &CurrentPath() const { return m_CurrentPath; }

private:
	enum class EDragResult
	{
		DRAG_RESULT_NONE,
		DRAG_RESULT_DRAG,
		DRAG_RESULT_CANCEL,
	};

	EDragResult DoDrag(const void *pId, CUIRect *pRect);
	bool UpdateNode(const void *pId, bool Selected, int Type);
	void NextSlot(float Cut, CUIRect *pSlot);
	bool IsValidDropTarget(const CDropTargetInfo &Info);
	void Place(bool ValidTarget, CUIRect *pRect);

	inline bool ShouldClearSelected();
	inline bool ShouldBeDragged() const;

	void OnDragStop();
	void OnDragConfirm();
	void OnDragCancel();
	void OnDragStart();
	void OnDrag();
	void OnTarget(const CTreeNodePath &Path);

	inline CContext *Context();

	void Reset();
	inline int ItemIndex();

private:
	CUIRect m_Root;

	float m_Indent;
	float m_AutoSpacing;
	float m_ItemHeight;

	const void *m_pDragId;
	const void *m_pHotId;
	const void *m_pClickedId;
	CScrollRegion m_ScrollRegion;

	float m_InitialMouseY;
	float m_DraggedPanelY;
	float m_DraggedPanelX;
	float m_DraggedPanelHeight;
	float m_DraggedPanelOffset;
	CTreeNodePath m_SelectedPath;

	CContext m_ViewContext;
	CDraggedContext m_DraggedContext;

	std::optional<CTreeNodePath> m_HighlightPath;

	enum class EState
	{
		STATE_NONE,
		STATE_DRAG_START,
		STATE_DRAGGING,
	};

	EState m_State;

	enum class EDragEndState
	{
		STATE_NONE,
		STATE_DRAG_CONFIRM,
		STATE_DRAG_CANCEL,
	};

	EDragEndState m_DragEndState;

	CTreeNodePath m_CurrentPath;
	// int m_PathCounter;
	CTreeNodePath m_CurrentTargetPath;
	std::optional<CTreeNodePath> m_TargetPath;

	std::vector<CDropTargetInfo> m_vCurrentDropTargetInfo;
	CDropTargetInfo m_LastDropTargetInfo;

	EDragStatus m_DragStatus;

public:
	static void Print(const CTreeNodePath &Path)
	{
		printf("/");
		for(const unsigned p : Path)
			printf("%d/", p);
	}
};

#endif
