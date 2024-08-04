#include "node.h"

#include <game/editor/editor.h>
#include <game/editor/mapitems/map_object.h>

// void ITreeParentNode::AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
//{
//	Object()->m_vpChildren.insert(Object()->m_vpChildren.begin() + Index.m_Index, pChild->Object());
// }
//
// void ITreeParentNode::RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
//{
//	Object()->m_vpChildren.erase(Object()->m_vpChildren.begin() + Index.m_Index);
// }

void ITreeNode::Clear()
{
	m_vpChildren.clear();
}

CDropTargetInfo ITreeNode::DropTargetInfo()
{
	return CDropTargetInfo::None();
}

void ITreeNode::AddChild(const std::shared_ptr<ITreeNode> &pNode)
{
	pNode->m_pNodeParent = shared_from_this();
	pNode->m_Path = m_Path / m_vpChildren.size();
	m_vpChildren.push_back(pNode);
}

CUi::EPopupMenuFunctionResult ITreeNode::Popup(CUIRect View, int &Height)
{
	std::shared_ptr<IEditorMapObject> pObject = Object();
	if(pObject)
		return pObject->Popup(View, Height);

	return CUi::EPopupMenuFunctionResult::POPUP_CLOSE_CURRENT;
};

bool ITreeNode::Hovered()
{
	return Editor()->Ui()->HotItem() == Id();
}

void ITreeNode::OnChildAdded(const size_t &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Object()->m_vpChildren.insert(Object()->m_vpChildren.begin() + Index, pChild->Object());
}

void ITreeNode::OnChildRemoved(const size_t &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Object()->m_vpChildren.erase(Object()->m_vpChildren.begin() + Index);
}

CUi::EPopupMenuFunctionResult ITreeNode::RenderPopup(void *pContext, CUIRect View, bool Active, int &Height)
{
	ITreeNode *pNode = (ITreeNode *)pContext;
	dbg_assert(pNode != nullptr, "Invalid tree node used to render popup");
	return pNode->Popup(View, Height);
}
