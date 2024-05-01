#ifndef GAME_EDITOR_MAPITEMS_MAP_IO_H
#define GAME_EDITOR_MAPITEMS_MAP_IO_H

#include <base/factory.h>
#include <base/generic.h>
#include <base/system.h>

#include <game/mapitems_ex.h>
#include <game/mapitems_object_types.h>

#include <memory>

using CMapItemObject = CInterfaceImpl<IMapItemObject>;

class CMapReader
{
public:
	auto Load(const struct CMapItemParentGroupObject &Item);
	auto Load(const struct CMapItemLayerGroupObject &Item);
};

class CMapWriter
{
public:
	CMapWriter(class CDataFileWriter &Writer) :
		m_Writer(Writer) {}

	auto Write(const class CEditorParentGroup &Object);
	auto Write(const class CLayerGroupObject &Object);

private:
	class CDataFileWriter &m_Writer;
};

#endif
