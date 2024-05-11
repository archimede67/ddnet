#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODES_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODES_H

#include "node.h"

#include <memory>

class CLayer;
class CLayerGroup;
class CLayerNode;
class CEditorMap;
class CEditorParentGroup;

// Node for CEditorMap (root)
class CEditorMapNode : public ITreeParentNode
{
public:
	CEditorMapNode(CEditorMap *pMap) :
		m_pMap(pMap) {}

	void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(int Index) override;

	bool *Visible() override { return nullptr; }
	bool *Collapse() override { return nullptr; }
	const void *Id() override { return m_pMap; }

private:
	CEditorMap *m_pMap;
};

// Node for CLayerGroup
class CLayerGroupNode : public ITreeParentNode
{
public:
	CLayerGroupNode(const std::shared_ptr<CLayerGroup> &pGroup) :
		m_pGroup(pGroup) {}

	void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(int Index) override;

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pGroup.get(); }

	const std::shared_ptr<CLayerGroup> &Group() { return m_pGroup; }

private:
	std::shared_ptr<CLayerGroup> m_pGroup;
};

class CEditorFolderNode : public ITreeParentNode
{
public:
	CEditorFolderNode(const std::shared_ptr<CEditorParentGroup> &pFolder) :
		m_pFolder(pFolder) {}

	void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(int Index) override;

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pFolder.get(); }

	const std::shared_ptr<CEditorParentGroup> &Folder() { return m_pFolder; }

private:
	std::shared_ptr<CEditorParentGroup> m_pFolder;
};

// Node for CLayer ---------------------------------------
class CLayerNode : public ITreeNode
{
public:
	CLayerNode(const std::shared_ptr<CLayer> &pLayer) :
		m_pLayer(pLayer) {}

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pLayer.get(); }

	const std::shared_ptr<CLayer> &Layer() { return m_pLayer; }

private:
	std::shared_ptr<CLayer> m_pLayer;
};

#endif
