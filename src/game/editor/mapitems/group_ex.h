#ifndef GAME_EDITOR_MAPITEMS_GROUP_EX_H
#define GAME_EDITOR_MAPITEMS_GROUP_EX_H

#include "map_object.h"

#include <memory>
#include <vector>

class CEditorParentGroup : public IEditorMapObject
{
public:
	CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height) override;

	std::shared_ptr<ITreeNode> ToTreeNode(const std::shared_ptr<IEditorMapObject> &Self) override;

public:
	char m_aName[16];

	bool m_Visible;
	int m_Test;
	bool m_Collapse;
};

class CLayerGroupObject : public IEditorMapObject
{
public:
	CLayerGroupObject(int GroupIndex) :
		m_GroupIndex(GroupIndex) {}

	virtual void Render() override;
	CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height) override;

	std::shared_ptr<ITreeNode> ToTreeNode(const std::shared_ptr<IEditorMapObject> &Self) override;

public:
	int m_GroupIndex;
};

#endif
