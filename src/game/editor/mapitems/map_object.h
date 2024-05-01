#ifndef GAME_EDITOR_MAPITEMS_MAP_OBJECT_H
#define GAME_EDITOR_MAPITEMS_MAP_OBJECT_H

#include "map_io.h"

#include <base/system.h>
#include <game/mapitems_object_types.h>

#include <memory>

class CEditorMap;

class IEditorMapObject
{
public:
	virtual ~IEditorMapObject() = default;
	virtual void Render() = 0;

protected:
	CEditorMap *Map() { return m_pMap; }

protected:
	IEditorMapObject() = default;
	IEditorMapObject(const IEditorMapObject &) = default;
	IEditorMapObject &operator=(const IEditorMapObject &) = delete;

private:
	CEditorMap *m_pMap;

	friend class CEditorMap;
};

struct IEditorMapObjectMixin : public virtual IEditorMapObject
{
	virtual IMapItemObjectBase Save(CMapWriter &Writer) = 0;
};

template<typename Object, typename std::enable_if_t<std::is_base_of_v<IEditorMapObject, Object>, bool> = true>
struct CEditorMapObjectMixin : public virtual IEditorMapObjectMixin, public Object
{
	explicit CEditorMapObjectMixin(const Object &Obj) :
		Object(Obj) {}

private:
	IMapItemObjectBase Save(CMapWriter &Writer) override final
	{
		return Writer.Write(*static_cast<Object *>(this));
	}
};

#endif
