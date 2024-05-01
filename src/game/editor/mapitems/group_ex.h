#ifndef GAME_EDITOR_MAPITEMS_GROUP_EX_H
#define GAME_EDITOR_MAPITEMS_GROUP_EX_H

#include "map_object.h"

#include <memory>
#include <vector>

class CEditorParentGroup : public virtual IEditorMapObject
{
public:
	void Render() override;

public:
	char m_aName[16];

	bool m_Visible;
	bool m_Collapse;

	std::vector<std::shared_ptr<IEditorMapObject>> m_vpChildren;
};

class CLayerGroupObject : public virtual IEditorMapObject
{
public:
	CLayerGroupObject(int GroupIndex) :
		m_GroupIndex(GroupIndex) {}

	void Render() override;

public:
	int m_GroupIndex;
};

#endif
