#ifndef GAME_MAPITEMS_EX_H
#define GAME_MAPITEMS_EX_H
#include <game/generated/protocol.h>

#include "mapitems_object.h"

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

struct IMapItemTreeNode
{
	int m_Version;
};

struct CMapItemFolderNode : IMapItemTreeNode
{
	MACRO_MAPITEM_TREENODE(MAPITEMTYPE_FOLDER_NODE);

	enum
	{
		CURRENT_VERSION = 1
	};

	int m_Name;
	int m_Visible;
	int m_Collapse;
	int m_Test;
};

// ----------------------------------------

void RegisterMapItemTypeUuids(class CUuidManager *pManager);
#endif // GAME_MAPITEMS_EX_H
