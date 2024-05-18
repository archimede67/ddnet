#ifndef GAME_EDITOR_MAPITEMS_MAP_IO_H
#define GAME_EDITOR_MAPITEMS_MAP_IO_H

#include <game/mapitems_ex.h>

#include <memory>
#include <vector>

struct IEditorMapObject;

class IMapItemReader
{
protected:
	class CDataFileReader *DataFile() { return m_pReader; }
	class CEditorMap *Map() { return m_pMap; }

	void Error(const char *pErrorMessage) { (*m_pfnErrorHandler)(pErrorMessage); }

private:
	class CDataFileReader *m_pReader;
	class CEditorMap *m_pMap;
	const std::function<void(const char *pErrorMessage)> *m_pfnErrorHandler;

	friend class CEditorMap;
};

class IMapItemWriter
{
protected:
	class CDataFileWriter *Writer() { return m_pWriter; }
	class CEditorMap *Map() { return m_pMap; }

private:
	class CDataFileWriter *m_pWriter;
	class CEditorMap *m_pMap;

	friend class CEditorMap;
};

class CMapObjectReader : public IMapItemReader
{
public:
	auto Load(const struct CMapItemFolderNode &Item);
	auto Load(const struct CMapItemLayerGroupNode &Item);
};

class CMapObjectWriter : public IMapItemWriter
{
public:
	auto Write(const class CEditorParentGroup &Object);
	auto Write(const class CLayerGroupObject &Object);
};

class CMapItemTreeWriter : public IMapItemWriter
{
	friend class CEditorMap;

public:
	CMapItemTreeWriter(const CMapObjectWriter &ObjectWriter);
	void WriteRoot(const std::vector<std::shared_ptr<IEditorMapObject>> &vpRootObjects);

private:
	int NextTypeIndex(int Type);
	int NextIndex();

	void WriteObjects(const std::vector<std::shared_ptr<IEditorMapObject>> &vpObjects, int *pFirstChild);

private:
	CMapObjectWriter m_MapObjectWriter;
	int m_Index;
	std::unordered_map<int, int> m_IndexByType;
};

class CMapItemTreeReader : public IMapItemReader
{
public:
	CMapItemTreeReader(const CMapObjectReader &ObjectReader);
	std::vector<std::shared_ptr<IEditorMapObject>> ReadRoot();

private:
	std::vector<std::shared_ptr<IEditorMapObject>> ReadObjects(int FirstIndex);

private:
	CMapObjectReader m_ObjectReader;
};

#endif
