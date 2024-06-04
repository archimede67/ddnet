#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H

#include <game/client/ui.h>

#include <memory>

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

public:
	explicit ITreeNode(const EType Type) :
		m_Type(Type), m_pLayers(nullptr), m_pEditor(nullptr)
	{
	}

	virtual ~ITreeNode() = default;
	ITreeNode(const ITreeNode &Other) = default;

	virtual bool *Collapse() = 0;
	virtual bool *Visible() = 0;
	virtual const void *Id() = 0;

	virtual std::shared_ptr<IEditorMapObject> Object() { return nullptr; }
	virtual ENodeSelectResult OnSelect() { return ENodeSelectResult::ALLOW; }
	virtual void OnDeselect() {}

	virtual bool IsLeaf() { return true; }
	virtual void Decorate(CUIRect View) {}

	EType Type() const { return m_Type; }
	virtual const char *Name() = 0;

protected:
	virtual CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height);
	CLayersView *Layers() const { return m_pLayers; }
	class CEditor *Editor() const { return m_pEditor; }

	bool Hovered();

private:
	static CUi::EPopupMenuFunctionResult RenderPopup(void *pContext, CUIRect View, bool Active, int &Height);

private:
	EType m_Type;
	CLayersView *m_pLayers;
	CEditor *m_pEditor;
};

class ITreeParentNode : public ITreeNode
{
	friend class CLayersView;

public:
	struct CIndex
	{
		size_t m_Index;
	};

public:
	ITreeParentNode(const EType Type) :
		ITreeNode(Type)
	{
	}

	virtual void AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild);
	virtual void RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild);

	bool IsLeaf() override { return false; }
};

#endif
