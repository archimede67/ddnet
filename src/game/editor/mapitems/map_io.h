#ifndef GAME_EDITOR_MAPITEMS_MAP_IO_H
#define GAME_EDITOR_MAPITEMS_MAP_IO_H

#include <game/mapitems_ex.h>

class IMapItemReader
{
protected:
	class CDataFileReader &DataFile() { return *m_pReader; }

private:
	class CDataFileReader *m_pReader;

	friend class CEditorMap;
};

class IMapItemWriter
{
protected:
	class CDataFileWriter &Writer() { return *m_pWriter; }

private:
	class CDataFileWriter *m_pWriter;

	friend class CEditorMap;
};

class CMapObjectReader : public IMapItemReader
{
public:
	auto Load(const struct CMapItemParentGroupObject &Item);
	auto Load(const struct CMapItemLayerGroupObject &Item);
};

class CMapObjectWriter : public IMapItemWriter
{
public:
	auto Write(const class CEditorParentGroup &Object);
	auto Write(const class CLayerGroupObject &Object);
};

#endif
