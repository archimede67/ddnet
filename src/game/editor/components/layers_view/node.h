#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODE_H

#include <memory>

class ITreeNode
{
public:
	enum EType
	{
		TYPE_ROOT = -1,
		TYPE_LAYER,
		TYPE_ENTITIES_LAYER,
		TYPE_LAYER_GROUP,
		TYPE_FOLDER,
	};

	virtual bool *Collapse() = 0;
	virtual bool *Visible() = 0;
	virtual const void *Id() = 0;

	virtual bool IsLeaf() { return true; }
};

class ITreeParentNode : public ITreeNode
{
public:
	virtual void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild) = 0;
	virtual void RemoveChild(int Index) = 0;

	bool IsLeaf() override { return false; }
};

#endif
