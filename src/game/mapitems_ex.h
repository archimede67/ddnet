#ifndef GAME_MAPITEMS_EX_H
#define GAME_MAPITEMS_EX_H
#include <game/generated/protocol.h>

enum
{
	__MAPITEMTYPE_UUID_HELPER = OFFSET_MAPITEMTYPE_UUID - 1,
#define UUID(id, name) id,
#include "mapitems_ex_types.h"
#undef UUID
	END_MAPITEMTYPES_UUID,
};

struct CMapItemAutoMapperConfig
{
	enum
	{
		CURRENT_VERSION = 1
	};
	enum
	{
		FLAG_AUTOMATIC = 1
	};

	int m_Version;
	int m_GroupId;
	int m_LayerId;
	int m_AutomapperConfig;
	int m_AutomapperSeed;
	int m_Flags;
};

// Map item to store infos about a group
struct CMapItemGroupInfo
{
	enum
	{
		CURRENT_VERSION = 1
	};

	int m_Version; // Version of the map item
	int m_Type; // Type of the group
	int m_Index; // Index of the group
	//int m_Children; // List of child group indices of this parent group
	int m_Parent; // Parent of this group
};

struct CMapItemParentGroup
{
	enum
	{
		CURRENT_VERSION = 1
	};

	int m_Version; // Version of the map item
	int m_Name; // Name of the group
	// Extra info here (maybe)
	int m_Visible;
	int m_Collapse;
};

void RegisterMapItemTypeUuids(class CUuidManager *pManager);
#endif // GAME_MAPITEMS_EX_H
