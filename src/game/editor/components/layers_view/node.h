#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H

#include <game/client/ui.h>

#include <memory>
#include <unordered_set>
#include <vector>

struct IEditorMapObject;

enum class ENodeSelectResult
{
	NONE = 0,
	ALLOW,
	OVERRIDE,
	OVERRIDE_TYPE,
};

class ITreeNode : SPopupMenuId
{
	friend class CLayersView;

public:
	enum EType
	{
		TYPE_NONE = -2,
		TYPE_ROOT = -1,
		TYPE_LAYER,
		TYPE_ENTITIES_LAYER,
		TYPE_LAYER_GROUP,
		TYPE_FOLDER,
	};
	enum EFlags
	{
		FLAG_NONE = 0,
		FLAG_NO_MULTI_SELECTION = 1 << 0, // The same node type cannot be selected multiple times
	};

	explicit ITreeNode(const EType Type, const int Flags = FLAG_NONE) :
		m_Type(Type), m_Flags(Flags), m_pLayers(nullptr)
	{
	}

	virtual ~ITreeNode() = default;
	ITreeNode(const ITreeNode &Other) = default;

	virtual bool *Collapse() = 0;
	virtual bool *Visible() = 0;
	virtual const void *Id() = 0;
	virtual std::shared_ptr<IEditorMapObject> Object() { return nullptr; }
	virtual ENodeSelectResult OnSelect() { return ENodeSelectResult::NONE; }
	virtual void OnDeselect() {}

	virtual bool IsLeaf() { return true; }

	EType Type() const { return m_Type; }
	int Flags() const { return m_Flags; }
	virtual const char *Name() const = 0;

protected:
	virtual CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height);
	CLayersView *Layers() const { return m_pLayers; }
	class CEditor *Editor() const { return m_pEditor; }

private:
	static CUi::EPopupMenuFunctionResult RenderPopup(void *pContext, CUIRect View, bool Active, int &Height);

private:
	EType m_Type;
	int m_Flags;
	CLayersView *m_pLayers;
	CEditor *m_pEditor;
};

class ITreeParentNode : public ITreeNode
{
	friend class CLayersView;

public:
	ITreeParentNode(const EType Type, const int Flags = FLAG_NONE) :
		ITreeNode(Type, Flags)
	{
	}

	virtual void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild);
	virtual void RemoveChild(int Index);

	void AddChildren(const std::vector<std::shared_ptr<ITreeNode>> &vpChildren)
	{
		for(int i = 0; i < (int)vpChildren.size(); i++)
			AddChild(i, vpChildren.at(i));
	}

	bool IsLeaf() override { return false; }
};

#endif
