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

struct CMapItemRoot
{
	enum
	{
		CURRENT_VERSION = 1
	};

	int m_Version;
	int m_Objects; // Pointer to the list of root objects
};

// --- Object base ------------------------

struct IMapItemObject
{
	int m_Version;
};
// ----------------------------------------

// --- Objects ----------------------------

// Map item for a "pointer" to a layergroup
struct CMapItemLayerGroupObject : IMapItemObject
{
	MACRO_MAPITEM_OBJECT("Yahoo");

	enum
	{
		CURRENT_VERSION = 1
	};

	int m_GroupIndex; // Index to the LayerGroup
};

// Map item for a parent group
struct CMapItemParentGroupObject : IMapItemObject
{
	MACRO_MAPITEM_OBJECT("Test");

	enum
	{
		CURRENT_VERSION = 1
	};

	int m_Name; // Pointer to string data
	int m_Test1;
	int m_Test2;
};
// ----------------------------------------

void RegisterMapItemTypeUuids(class CUuidManager *pManager);
#endif // GAME_MAPITEMS_EX_H
