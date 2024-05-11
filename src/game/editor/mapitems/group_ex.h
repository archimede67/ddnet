#ifndef GAME_EDITOR_MAPITEMS_GROUP_EX_H
#define GAME_EDITOR_MAPITEMS_GROUP_EX_H

#include "map_object.h"

#include <memory>
#include <vector>

class CEditorParentGroup : public IEditorMapObject
{
public:
	void Render() override;

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

	void Render() override;

public:
	int m_GroupIndex;
};

#endif
