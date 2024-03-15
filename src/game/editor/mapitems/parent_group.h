#ifndef GAME_EDITOR_MAPITEMS_PARENT_GROUP_H
#define GAME_EDITOR_MAPITEMS_PARENT_GROUP_H

#include <vector>

class CEditorGroupInfo
{
public:
	enum EType
	{
		LAYER_GROUP,
		PARENT_GROUP,
	};

	int m_GroupIndex; // Index pointing to a CEditorParentGroup or a CLayerGroup depending on m_Type
	EType m_Type;
	std::vector<int> m_Children; // List of children where each index points to another CEditorGroupInfo
};

class CEditorParentGroup
{
public:
	char m_aName[128];
	int m_Color;
};

#endif
