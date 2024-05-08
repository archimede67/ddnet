#ifndef GAME_MAPITEMS_OBJECT_H
#define GAME_MAPITEMS_OBJECT_H

#include "mapitems_object_types.h"

#include <base/factory.h>

#include <memory>

// Required forward declarations
struct IEditorMapObject;
struct IMapItemObject;
class IMapItemObjectBase;

// Main factory used in the save/load process of map objects
using CMapItemObjectFactory = CFactoryRegistry<CMapItemObjectType, IMapItemObject, IMapItemObjectBase>;

// Struct holding the metadata of
struct CMapObjectMetadata
{
	CMapItemObjectType m_Type;
};

template<typename T>
struct CMapObjectPacked
{
	CMapObjectMetadata m_Metadata;
	T m_Object;

	CMapObjectPacked(CMapItemObjectFactory::KeyType Type, const T &Object) :
		m_Metadata({Type}), m_Object(Object) {}
};

class IMapItemObjectBase
{
public:
	struct CPackedItem
	{
		void *m_pData;
		size_t m_Size;

		CPackedItem(void *pData, size_t Size) :
			m_pData(pData), m_Size(Size) {}
	};

private:
	struct IConcept
	{
		virtual ~IConcept() = default;
		virtual std::shared_ptr<IEditorMapObject> Load(class CMapObjectReader &Reader, const void *pItem) = 0;
		virtual CPackedItem PackedItem() = 0;
	};

	template<typename T>
	struct CModel : IConcept
	{
		CMapObjectPacked<T> m_PackedObject;

		explicit CModel(const T &Object);

		std::shared_ptr<IEditorMapObject> Load(class CMapObjectReader &Reader, const void *pItem) override;
		CPackedItem PackedItem() override;
		CMapItemObjectFactory::KeyType Key();
	};

public:
	template<typename T>
	IMapItemObjectBase(const T &Object) :
		m_pConcept(std::make_unique<CModel<T>>(Object))
	{
	}

	std::shared_ptr<IEditorMapObject> Load(class CMapObjectReader &Reader, const void *pItem)
	{
		return m_pConcept->Load(Reader, pItem);
	}

	CPackedItem PackedItem()
	{
		return m_pConcept->PackedItem();
	}

private:
	std::unique_ptr<IConcept> m_pConcept;
};

// Helper macro the mark a class as a map item object.
// This will automatically register the class into the factory CMapItemObjectFactory
// from its identifier.
// This macro will add some data to identify and register that class, such as traits
// and an init method. However, this won't add any extra bytes to the class, which is
// very important.
#define MACRO_MAPITEM_OBJECT(id) MACRO_FACTORY_REGISTER((CMapItemObjectType)id, CMapItemObjectFactory)

#endif
