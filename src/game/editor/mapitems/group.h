#ifndef GAME_EDITOR_MAPITEMS_GROUP_H
#define GAME_EDITOR_MAPITEMS_GROUP_H

class ITreeNode
{
};

class ICollapsible : public ITreeNode
{
public:
	bool m_Collapse;
};

class IHideable : public ITreeNode
{
public:
	bool m_Visible;
};

class IGroupNode : public ICollapsible, public IHideable
{
};

class ILayerNode : public IHideable
{
};

#endif
