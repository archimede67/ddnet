#ifndef GAME_EDITOR_MAPITEMS_MAP_IO_H
#define GAME_EDITOR_MAPITEMS_MAP_IO_H

#include <game/mapitems_ex.h>

#include <memory>
#include <vector>

class IMapItemReader
{
protected:
	class CDataFileReader *DataFile() { return m_pReader; }

	void Error(const char *pErrorMessage) { (*m_pfnErrorHandler)(pErrorMessage); }

private:
	class CDataFileReader *m_pReader;
	const std::function<void(const char *pErrorMessage)> *m_pfnErrorHandler;

	friend class CEditorMap;
};

class IMapItemWriter
{
protected:
	class CDataFileWriter *Writer() { return m_pWriter; }

private:
	class CDataFileWriter *m_pWriter;

	friend class CEditorMap;
};

class CMapObjectReader : public IMapItemReader
{
public:
	auto Load(const struct CMapItemFolderNode &Item);
	// auto Load(const struct CMapItemLayerGroupObject &Item);
};

class CMapObjectWriter : public IMapItemWriter
{
public:
	auto Write(const class CEditorParentGroup &Object);
	// auto Write(const class CLayerGroupObject &Object);
};

class CMapItemTreeWriter : public IMapItemWriter
{
	friend class CEditorMap;

public:
	CMapItemTreeWriter();
	void WriteRoot(const std::vector<std::shared_ptr<class IEditorMapObject>> &vpRootObjects);

private:
	int NextTypeIndex(int Type);
	int NextIndex();

	void WriteObjects(const std::vector<std::shared_ptr<class IEditorMapObject>> &vpObjects, int *pFirstChild);

private:
	CMapObjectWriter m_MapObjectWriter;
	int m_Index;
	std::unordered_map<int, int> m_IndexByType;
};

class CMapItemTreeReader : public IMapItemReader
{
public:
	CMapItemTreeReader(const CMapObjectReader &ObjectReader);
	std::vector<std::shared_ptr<IEditorMapObject>> ReatRoot();

private:
	std::vector<std::shared_ptr<IEditorMapObject>> ReadObjects(int Index);

private:
	CMapObjectReader m_ObjectReader;
};

#endif
