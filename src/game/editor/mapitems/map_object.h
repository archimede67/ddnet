#ifndef GAME_EDITOR_MAPITEMS_MAP_OBJECT_H
#define GAME_EDITOR_MAPITEMS_MAP_OBJECT_H

#include <base/system.h>
#include <game/mapitems_object.h>
#include <game/mapitems_object_types.h>

#include <memory>

class CEditorMap;

struct IEditorMapObject
{
public:
	virtual ~IEditorMapObject() = default;
	virtual void Render();

protected:
	CEditorMap *Map() { return m_pMap; }

protected:
	IEditorMapObject() = default;
	IEditorMapObject(const IEditorMapObject &) = default;
	IEditorMapObject &operator=(const IEditorMapObject &) = delete;

private:
	virtual IEditorMapObjectNode ToObjectNode()
	{
		dbg_assert(false, "Map object was not created from a mixin");
		return nullptr;
	}

	CEditorMap *m_pMap;

	friend class CEditorMap;
	friend class CMapItemTreeWriter;

public:
	std::vector<std::shared_ptr<IEditorMapObject>> m_vpChildren;
};

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
