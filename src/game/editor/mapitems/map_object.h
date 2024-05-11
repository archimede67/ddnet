#ifndef GAME_EDITOR_MAPITEMS_MAP_OBJECT_H
#define GAME_EDITOR_MAPITEMS_MAP_OBJECT_H

#include "map_io.h"

#include <base/system.h>
#include <game/mapitems_object_types.h>

#include <memory>

class CEditorMap;

struct IEditorMapObject
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
	virtual IEditorMapObjectNode ToObjectNode() = 0;

	CEditorMap *m_pMap;

	friend class CEditorMap;
};

struct IEditorMapObjectMixin : virtual IEditorMapObject
{
	virtual IMapItemObjectBase Save(CMapObjectWriter &Writer) = 0;
};

template<typename Object, typename std::enable_if_t<std::is_base_of_v<IEditorMapObject, Object>, bool> = true>
struct CEditorMapObjectMixin : virtual IEditorMapObjectMixin, public virtual Object
{
	explicit CEditorMapObjectMixin(const Object &Obj) :
		Object(Obj) {}

private:
	IMapItemObjectBase Save(CMapObjectWriter &Writer) override final
	{
		return Writer.Write(*static_cast<Object *>(this));
	}
};

// ----------------------------------------------

template<typename Object, typename std::enable_if_t<std::is_base_of_v<IEditorMapObject, Object>, bool> = true>
struct CEditorMapTreeNodeMixin : public Object
{
	template<typename... Args>
	CEditorMapTreeNodeMixin(Args &&...Arguments) :
		Object(std::forward<Args>(Arguments)...)
	{
	}

private:
	IEditorMapObjectNode ToObjectNode() override final
	{
		return static_cast<Object *>(this);
	}
};

#endif
