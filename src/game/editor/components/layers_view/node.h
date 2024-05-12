#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H

#include <game/client/ui.h>

#include <memory>
#include <vector>

struct IEditorMapObject;

class ITreeNode : SPopupMenuId
{
	friend class CLayersView;

public:
	enum EType
	{
		TYPE_ROOT = -1,
		TYPE_LAYER,
		TYPE_ENTITIES_LAYER,
		TYPE_LAYER_GROUP,
		TYPE_FOLDER,
	};

	ITreeNode(EType Type) :
		m_Type(Type) {}

	virtual bool *Collapse() = 0;
	virtual bool *Visible() = 0;
	virtual const void *Id() = 0;
	virtual std::shared_ptr<IEditorMapObject> Object() { return nullptr; }

	virtual bool IsLeaf() { return true; }

	EType Type() const { return m_Type; }
	virtual const char *Name() const = 0;

protected:
	virtual CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height);

private:
	static CUi::EPopupMenuFunctionResult RenderPopup(void *pContext, CUIRect View, bool Active, int &Height);

private:
	EType m_Type;
};

class ITreeParentNode : public ITreeNode
{
public:
	ITreeParentNode(EType Type) :
		ITreeNode(Type) {}

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
