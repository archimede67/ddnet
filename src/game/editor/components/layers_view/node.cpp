#include "node.h"

#include <game/editor/editor.h>
#include <game/editor/mapitems/map_object.h>

void ITreeParentNode::AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Object()->m_vpChildren.insert(Object()->m_vpChildren.begin() + Index.m_Index, pChild->Object());
}

void ITreeParentNode::RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Object()->m_vpChildren.erase(Object()->m_vpChildren.begin() + Index.m_Index);
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

CUi::EPopupMenuFunctionResult ITreeNode::RenderPopup(void *pContext, CUIRect View, bool Active, int &Height)
{
	ITreeNode *pNode = (ITreeNode *)pContext;
	dbg_assert(pNode != nullptr, "Invalid tree node used to render popup");
	return pNode->Popup(View, Height);
}
