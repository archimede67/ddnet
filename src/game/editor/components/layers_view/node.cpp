#include "node.h"

#include <game/editor/mapitems/map_object.h>

void ITreeParentNode::AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild)
{
	Object()->m_vpChildren.insert(Object()->m_vpChildren.begin() + Index, pChild->Object());
}

void ITreeParentNode::RemoveChild(int Index)
{
	Object()->m_vpChildren.erase(Object()->m_vpChildren.begin() + Index);
}

CUi::EPopupMenuFunctionResult ITreeNode::Popup(CUIRect View, int &Height)
{
	std::shared_ptr<IEditorMapObject> pObject = Object();
	if(pObject)
		return pObject->Popup(View, Height);

	return CUi::EPopupMenuFunctionResult::POPUP_CLOSE_CURRENT;
};

CUi::EPopupMenuFunctionResult ITreeNode::RenderPopup(void *pContext, CUIRect View, bool Active, int &Height)
{
	ITreeNode *pNode = (ITreeNode *)pContext;
	dbg_assert(pNode != nullptr, "Invalid tree node used to render popup");
	return pNode->Popup(View, Height);
}