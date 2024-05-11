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

// ----------------------------------------------

using CMapItemTreeNodeFactory = CFactoryRegistry<int, struct IMapItemTreeNode, class IMapItemTreeNodeBase>;

class IMapItemTreeNodeBase
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
		virtual void Load(const void *pData) = 0;
		virtual CPackedItem Pack() = 0;
		virtual CMapItemTreeNodeFactory::KeyType Type() = 0;
	};

	template<typename T>
	struct CModel : IConcept
	{
		explicit CModel(const T &Object) :
			m_Object(Object) {}

		T m_Object;

		void Load(const void *pData) override final
		{
			T *pConcrete = (T *)pData;
			printf("We should load this concrete type somehow! %p omg %s %d\n", pConcrete, typeid(T).name(), pConcrete->m_SomeData);
		}

		CPackedItem Pack() override final
		{
			return CPackedItem(&m_Object, sizeof(m_Object));
		}

		CMapItemTreeNodeFactory::KeyType Type() override final;
	};

public:
	template<typename T>
	IMapItemTreeNodeBase(const T &Object) :
		m_pConcept(std::make_unique<CModel<T>>(Object))
	{
	}

	void Load(const void *pData)
	{
		m_pConcept->Load(pData);
	}

	CPackedItem Pack()
	{
		return m_pConcept->Pack();
	}

	CMapItemTreeNodeFactory::KeyType Type()
	{
		return m_pConcept->Type();
	}

private:
	std::unique_ptr<IConcept> m_pConcept;
};

class IEditorMapObjectNode
{
private:
	struct IConcept
	{
		virtual IMapItemTreeNodeBase Write(class CMapObjectWriter &Writer) = 0;
	};

	template<typename T>
	struct CModel : IConcept
	{
		explicit CModel(T *pObject) :
			m_pObject(pObject) {}

		T *m_pObject;

		IMapItemTreeNodeBase Write(class CMapObjectWriter &Writer) override;
	};

public:
	template<typename T>
	IEditorMapObjectNode(T *pObject) :
		m_pConcept(std::make_unique<CModel<T>>(pObject))
	{
	}

	IMapItemTreeNodeBase Write(class CMapObjectWriter &Writer)
	{
		return m_pConcept->Write(Writer);
	}

private:
	std::unique_ptr<IConcept> m_pConcept;
};

struct CMapItemRawTreeNode
{
	// Type of the item
	int m_ItemType;
	// Index of the item
	int m_ItemIndex;

	// Next sibling in the tree, -1 if none
	int m_NextSiblingIndex;
	// First child in the tree, -1 if none
	int m_FirstChildIndex;
};

#define MACRO_MAPITEM_TREENODE(id) MACRO_FACTORY_REGISTER((int)id, CMapItemTreeNodeFactory);

#endif
