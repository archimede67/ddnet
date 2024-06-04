#include "ui_treeview.h"

CTreeView::CTreeView()
{
	m_ItemHeight = 12.0f;
	m_Indent = 12.0f;
	m_AutoSpacing = 0.0f;
	m_pDragId = m_pHotId = m_pClickedId = nullptr;
	m_DraggedPanelX = m_DraggedPanelY = 0;
	m_State = EState::STATE_NONE;
	m_DragEndState = EDragEndState::STATE_NONE;
	m_DragStatus = EDragStatus::NONE;

	m_CurrentPath = m_CurrentTargetPath = m_SelectedPath = {};
	m_TargetPath.reset();
	m_HighlightPath.reset();

	m_InitialMouseY = 0;
	m_DraggedPanelHeight = 0;
	m_DraggedPanelOffset = 0;
	m_vCurrentDropTargetInfo = {};

	m_DraggedContext.m_vTypes.clear();
	m_DraggedContext.m_vOriginalPaths.clear();
}

void CTreeView::Start(CUIRect *pView, float Indent, float ItemHeight, const CDropTargetInfo &RootDropTargetInfo)
{
	m_Root = *pView;
	m_Indent = Indent;
	m_ItemHeight = ItemHeight;
	m_DragEndState = EDragEndState::STATE_NONE;
	m_vCurrentDropTargetInfo = {RootDropTargetInfo};
	m_DragStatus = EDragStatus::NONE;

	m_SelectedPath = {};
	m_TargetPath.reset();
	m_HighlightPath.reset();
	m_CurrentPath = {0};

	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarWidth = 10.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	ScrollParams.m_ScrollUnit = ItemHeight * 5.0f;
	m_ScrollRegion.Begin(&m_Root, &ScrollOffset, &ScrollParams);

	m_ViewContext.Begin(m_Root, Indent);
	m_DraggedContext.Begin(m_Root, Indent);

	m_ViewContext.m_View.y += ScrollOffset.y;

	if(Dragging())
	{
		m_DraggedContext.m_View.x = m_DraggedPanelX;
		m_DraggedContext.m_View.w = maximum(0.0f, m_Root.w - (m_DraggedPanelX - m_Root.x));
		m_DraggedContext.m_View.y = m_DraggedPanelY;
		m_DraggedContext.m_View.h = m_DraggedPanelHeight;

		m_DraggedPanelX = m_Root.x;
	}
}

CTreeChanges CTreeView::End()
{
	if(m_DragEndState == EDragEndState::STATE_DRAG_CANCEL)
		OnDragCancel();
	else if(m_DragEndState == EDragEndState::STATE_DRAG_CONFIRM)
		OnDragConfirm();

	if(Dragging())
	{
		// Add space at the bottom to be able to scroll there
		CUIRect Space;
		m_ViewContext.m_View.HSplitTop(m_AutoSpacing, nullptr, &m_ViewContext.m_View);
		m_ViewContext.m_View.HSplitTop(m_ItemHeight, &Space, nullptr);
		m_ScrollRegion.AddRect(Space);

		float MouseY = Ui()->MouseY() + m_DraggedPanelOffset + (ItemIndex() > 0 ? m_AutoSpacing : 0);
		if(MouseY >= Space.y)
		{
			if(IsValidDropTarget(m_vCurrentDropTargetInfo.back()))
			{
				m_CurrentTargetPath = {m_CurrentPath[0]}; // Insert at the end
			}
			else
			{
				m_CurrentTargetPath.Clear();
				m_DragStatus = EDragStatus::NOT_ALLOWED;
			}
		}
	}

	if(m_pDragId != nullptr && !Dragging())
		OnDragStart();
	else if(Dragging())
		OnDrag();

	m_ScrollRegion.End();

	// Compute changes
	if(m_TargetPath)
	{
		auto ToPath = *m_TargetPath;

		CTreeChanges Changes(m_DraggedContext.m_vOriginalPaths, std::move(ToPath));

		m_TargetPath.reset();
		m_CurrentTargetPath.Clear();
		Reset();

		return Changes;
	}

	if(m_State == EState::STATE_NONE)
		Reset();

	return {};
}

CTreeViewItem CTreeView::DoNode(const void *pId, bool Selected, int Type, const CDropTargetInfo &DropTargetInfo)
{
	m_LastDropTargetInfo = DropTargetInfo;
	CDropTargetInfo &CurrentDropTargetInfo = m_vCurrentDropTargetInfo.back();
	const bool ValidCurrentTarget = IsValidDropTarget(CurrentDropTargetInfo);

	if(m_pDragId == pId)
	{
		m_DraggedPanelY = maximum(m_Root.y - (ItemIndex() > 0 ? m_AutoSpacing : 0), Ui()->MouseY() + m_DraggedPanelOffset);
	}

	bool Dragged = UpdateNode(pId, Selected, Type);

	CTreeViewItem Item{};

	if(m_AutoSpacing > 0.0f && ItemIndex() > 0)
		DoSpacing(m_AutoSpacing);

	NextSlot(m_ItemHeight, &Item.m_Rect);

	if(Context() == &m_ViewContext)
	{
		float MouseY = Ui()->MouseY() + m_DraggedPanelOffset;
		MouseY = maximum(m_Root.y + 0.01f, MouseY);

		if(Dragging())
		{
			const float Area = m_ItemHeight * 0.25f;

			CUIRect SubTree = Context()->CurrentSubTree();
			if(ValidCurrentTarget)
			{
				if(MouseY >= (SubTree.y - Area) && MouseY <= SubTree.y + SubTree.h)
				{
					if(SubTree.x > m_DraggedPanelX && !m_HighlightPath)
					{
						m_DraggedPanelX = SubTree.x;
					}
				}
			}
		}

		if(m_State == EState::STATE_DRAGGING)
		{
			CUIRect Top, Bottom;
			Item.m_Rect.HSplitMid(&Top, &Bottom);
			Top.y -= m_AutoSpacing;
			Top.h += m_AutoSpacing;

			if(MouseY >= Top.y && MouseY <= Top.y + Top.h)
			{
				Place(ValidCurrentTarget, &Item.m_Rect);
			}
			else if(MouseY >= Bottom.y && MouseY <= Bottom.y + Bottom.h)
			{
				if(!m_DraggedContext.m_vTypes.empty())
				{
					const bool ValidTarget = IsValidDropTarget(DropTargetInfo);

					if(ValidTarget)
					{
						m_DraggedPanelX = Bottom.x;
						m_HighlightPath = m_CurrentPath;

						// The target insertion will be at the beginning of the children list
						auto Path = m_CurrentPath.Child(0);
						OnTarget(Path);

						m_DragStatus = EDragStatus::MOVE_HERE;
					}
					else
					{
						Place(ValidCurrentTarget, &Item.m_Rect);
					}
				}
			}
		}
	}

	if(!Dragged)
		m_ScrollRegion.AddRect(Item.m_Rect);

	EDragResult Result = DoDrag(pId, &Item.m_Rect);
	if(Result == EDragResult::DRAG_RESULT_DRAG)
	{
		if(m_pClickedId == nullptr)
		{
			m_pClickedId = pId;
			m_InitialMouseY = Ui()->MouseY();
		}

		if(!m_pDragId && m_pClickedId == pId && absolute(Ui()->MouseY() - m_InitialMouseY) > 5.0f)
		{
			m_pDragId = pId;
			m_DraggedPanelOffset = (m_ViewContext.m_View.y - 1 * (m_ItemHeight)-m_InitialMouseY);
		}
	}
	else if(Result == EDragResult::DRAG_RESULT_CANCEL)
	{
		m_pClickedId = nullptr;
		m_DragEndState = EDragEndState::STATE_DRAG_CANCEL;
	}
	else if(m_pClickedId == pId)
	{
		if(m_pDragId == pId)
			m_DragEndState = EDragEndState::STATE_DRAG_CONFIRM;

		m_pClickedId = nullptr;
	}

	Context()->m_ItemIndex++;

	if(m_CurrentPath == m_HighlightPath)
		Item.m_IsTargetParent = true;

	Item.m_Path = m_CurrentPath;
	m_CurrentPath++;

	return Item;
}

void CTreeView::DoAutoSpacing(float Spacing)
{
	m_AutoSpacing = Spacing;
}

void CTreeView::DoSpacing(float Spacing)
{
	NextSlot(Spacing, nullptr);
}

void CTreeView::PushTree()
{
	m_CurrentPath--;
	m_CurrentPath.Push();
	m_vCurrentDropTargetInfo.push_back(m_LastDropTargetInfo);
	Context()->Push();
}

void CTreeView::PopTree()
{
	if(ShouldClearSelected())
		m_SelectedPath.Clear();

	if(Dragging() && Context() == &m_ViewContext)
	{
		float MouseY = Ui()->MouseY() + m_DraggedPanelOffset;
		MouseY = maximum(m_Root.y + 0.01f, MouseY);

		const bool ValidTarget = IsValidDropTarget(m_vCurrentDropTargetInfo.back());
		auto SubTree = Context()->CurrentSubTree();

		if(MouseY > SubTree.y + SubTree.h && MouseY <= SubTree.y + SubTree.h + 0.25f * m_ItemHeight && ValidTarget)
		{
			CUIRect FakeItem;
			m_ViewContext.m_View.HSplitTop(m_ItemHeight, &FakeItem, nullptr);

			Place(ValidTarget, &FakeItem);
			m_DraggedPanelX = SubTree.x;
		}
	}

	if(m_CurrentPath == m_SelectedPath)
		m_SelectedPath.Clear();

	m_CurrentPath.Pop();
	m_CurrentPath++;
	m_vCurrentDropTargetInfo.pop_back();

	Context()->Pop();
}

bool CTreeView::Dragging() const
{
	return m_State == EState::STATE_DRAGGING || m_State == EState::STATE_DRAG_START;
}

CTreeView::EDragResult CTreeView::DoDrag(const void *pId, CUIRect *pRect)
{
	if(m_pClickedId && m_pClickedId != pId)
		return EDragResult::DRAG_RESULT_NONE;

	const bool Pressed = Ui()->MouseButton(0);
	const bool Canceled = Ui()->MouseButton(1);
	const bool Inside = Ui()->MouseInside(pRect);

	if(!Pressed && Inside)
		m_pHotId = pId;
	else if(m_pHotId == pId && !Inside)
		m_pHotId = nullptr;

	bool Result = (m_pHotId == pId || m_pClickedId == pId) && Pressed && !Canceled;

	if(m_pDragId == pId && Canceled)
		return EDragResult::DRAG_RESULT_CANCEL;

	return Result ? EDragResult::DRAG_RESULT_DRAG : EDragResult::DRAG_RESULT_NONE;
}

bool CTreeView::UpdateNode(const void *pId, bool Selected, int Type)
{
	if(Selected && (m_SelectedPath.Empty() || m_CurrentPath.Length() <= m_SelectedPath.Length()))
	{
		m_SelectedPath = m_CurrentPath;
		if(m_State == EState::STATE_DRAG_START)
		{
			m_DraggedContext.m_vOriginalPaths.push_back(m_CurrentPath);
			m_DraggedContext.m_vTypes.insert(Type);
		}
	}
	else if(ShouldClearSelected() && !Selected)
	{
		m_SelectedPath.Clear();
	}

	return Dragging() && !m_SelectedPath.Empty();
}

bool CTreeView::ShouldClearSelected()
{
	return !m_SelectedPath.Empty() && m_CurrentPath.Length() <= m_SelectedPath.Length();
}

void CTreeView::NextSlot(float Cut, CUIRect *pSlot)
{
	if(m_State == EState::STATE_DRAG_START && ShouldBeDragged())
		m_DraggedPanelHeight += Cut;
	Context()->NextSlot(Cut, pSlot);
}

void CTreeView::OnDragStop()
{
	m_pDragId = nullptr;
	m_State = EState::STATE_NONE;
}

void CTreeView::OnDragConfirm()
{
	OnDragStop();
	if(!m_CurrentTargetPath.Empty())
		m_TargetPath = m_CurrentTargetPath;
}

void CTreeView::OnDragCancel()
{
	OnDragStop();
	m_TargetPath.reset();
}

void CTreeView::OnDragStart()
{
	m_DraggedPanelHeight = 0;
	m_State = EState::STATE_DRAG_START;
}

void CTreeView::OnDrag()
{
	m_State = EState::STATE_DRAGGING;
}

void CTreeView::OnTarget(const CTreeNodePath &Path)
{
	m_CurrentTargetPath = Path;
}

bool CTreeView::ShouldBeDragged() const
{
	return Dragging() && !m_SelectedPath.Empty();
}

CTreeView::CContext *CTreeView::Context()
{
	return (m_State == EState::STATE_DRAGGING && ShouldBeDragged()) ? &m_DraggedContext : &m_ViewContext;
}

void CTreeView::Reset()
{
	m_DraggedContext.m_vTypes.clear();
	m_DraggedContext.m_vOriginalPaths.clear();
}

bool CTreeView::IsValidDropTarget(const CDropTargetInfo &Info)
{
	return std::all_of(m_DraggedContext.m_vTypes.begin(), m_DraggedContext.m_vTypes.end(), [&Info](int Type) { return Info.IsAccepting(Type); });
}

int CTreeView::ItemIndex() { return Context()->m_ItemIndex; }

void CTreeView::Place(bool ValidTarget, CUIRect *pRect)
{
	m_HighlightPath.reset();

	if(ValidTarget)
	{
		CUIRect Space;
		NextSlot(m_DraggedPanelHeight - m_ItemHeight, &Space);

		if(pRect)
			m_ScrollRegion.AddRect(*pRect);
		m_ScrollRegion.AddRect(Space);

		NextSlot(m_ItemHeight, pRect);

		OnTarget(m_CurrentPath);
		m_DragStatus = EDragStatus::NORMAL;
	}
	else
	{
		m_CurrentTargetPath.Clear();
		m_DragStatus = EDragStatus::NOT_ALLOWED;
	}
};
