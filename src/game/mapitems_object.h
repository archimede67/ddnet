#ifndef GAME_MAPITEMS_OBJECT_H
#define GAME_MAPITEMS_OBJECT_H

#include <base/factory.h>

#include <memory>

// Required forward declarations
struct IEditorMapObject;
class CMapObjectReader;

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
	public:
		virtual ~IConcept() = default;

		virtual std::shared_ptr<IEditorMapObject> Load(CMapObjectReader &Reader, const void *pData) = 0;
		virtual CPackedItem Pack() = 0;
		virtual CMapItemTreeNodeFactory::KeyType Type() = 0;
	};

	template<typename T>
	struct CModel final : IConcept
	{
		explicit CModel(const T &Object) :
			m_Object(Object) {}

		T m_Object;

		std::shared_ptr<IEditorMapObject> Load(CMapObjectReader &Reader, const void *pData) override;
		CPackedItem Pack() override;
		CMapItemTreeNodeFactory::KeyType Type() override;
	};

public:
	template<typename T>
	IMapItemTreeNodeBase(const T &Object) :
		m_pConcept(std::make_unique<CModel<T>>(Object))
	{
	}

	std::shared_ptr<IEditorMapObject> Load(CMapObjectReader &Reader, const void *pData) const { return m_pConcept->Load(Reader, pData); }
	CPackedItem Pack() const { return m_pConcept->Pack(); }
	CMapItemTreeNodeFactory::KeyType Type() const { return m_pConcept->Type(); }

private:
	std::unique_ptr<IConcept> m_pConcept;
};

class IEditorMapObjectNode
{
private:
	struct IConcept
	{
	public:
		virtual ~IConcept() = default;

		virtual IMapItemTreeNodeBase Write(class CMapObjectWriter &Writer) = 0;
	};

	template<typename T>
	struct CModel final : IConcept
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

	IEditorMapObjectNode(std::nullptr_t) :
		m_pConcept(nullptr)
	{
	}

	IMapItemTreeNodeBase Write(class CMapObjectWriter &Writer) const
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

#define MACRO_MAPITEM_TREENODE(id) MACRO_FACTORY_REGISTER((int)(id), CMapItemTreeNodeFactory)

#endif
