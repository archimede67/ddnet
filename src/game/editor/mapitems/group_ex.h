#ifndef GAME_EDITOR_MAPITEMS_GROUP_EX_H
#define GAME_EDITOR_MAPITEMS_GROUP_EX_H

#include "group.h"

#include <vector>

// Group extensions

// Group info, used to link parent groups to other groups (parent or normal)
class CEditorGroupInfo
{
public:
	enum EType
	{
		TYPE_LAYER_GROUP,
		TYPE_PARENT_GROUP,
	};

	enum
	{
		PARENT_NONE = -1
	};

	int m_GroupIndex; // Index pointing to a CEditorParentGroup or a CLayerGroup depending on m_Type
	EType m_Type; // Type of the pointed group
	//std::vector<int> m_Children; // List of children where each index points to another CEditorGroupInfo
	int m_ParentIndex; // Index pointing to the parent CEditorGroupInfo if any, PARENT_NONE otherwise
};

// Data for a parent group
class CEditorParentGroup : public IGroup
{
public:
	char m_aName[128];
	int m_Color;
};

#endif
