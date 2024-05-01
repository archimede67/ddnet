#ifndef GAME_MAPITEMS_OBJECT_H
#define GAME_MAPITEMS_OBJECT_H

#include "mapitems_object_types.h"

#include <base/factory.h>
#include <base/generic.h>

#include <memory>

struct IEditorMapObject;
using CEditorMapObject = CInterfaceImpl<IEditorMapObject>;

class IMapItemObjectBase
{
	struct IConcept
	{
		virtual ~IConcept() = default;
		virtual CEditorMapObject Load(class CMapReader &Loader) = 0;
		virtual std::string Key() = 0;
	};

	template<typename T>
	struct CModel : IConcept
	{
		T m_Object;

		CModel(const T &Object) :
			m_Object(Object) {}

		CEditorMapObject Load(class CMapReader &Loader) override;
		std::string Key() override;
	};

public:
	template<typename T>
	IMapItemObjectBase(const T &Object) :
		m_pConcept(std::make_unique<CModel<T>>(Object))
	{
	}

	IMapItemObjectBase() :
		m_pConcept(nullptr) {}

	std::shared_ptr<IEditorMapObject> Load(class CMapReader &Loader)
	{
		return m_pConcept->Load(Loader).Convert();
	}

	std::string Key()
	{
		return m_pConcept->Key();
	}

private:
	std::unique_ptr<IConcept> m_pConcept;
};

using CMapItemObjectFactory = CFactoryRegistry<struct IMapItemObject, IMapItemObjectBase>;

#define MACRO_MAPITEM_OBJECT(id) MACRO_FACTORY_REGISTER_FULL(id, IMapItemObject, IMapItemObjectBase)

#endif
