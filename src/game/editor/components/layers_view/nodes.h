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
		m_pMap(pMap), ITreeParentNode(TYPE_ROOT) {}

	void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(int Index) override;

	bool *Visible() override { return nullptr; }
	bool *Collapse() override { return nullptr; }
	const void *Id() override { return m_pMap; }
	const char *Name() const override { return "Root"; }

private:
	CEditorMap *m_pMap;
};

// Node for CLayerGroup
class CLayerGroupNode : public ITreeParentNode
{
public:
	CLayerGroupNode(int Index, const std::shared_ptr<CLayerGroup> &pGroup);

	void AddChild(int Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(int Index) override;

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pGroup.get(); }
	const char *Name() const override { return m_aName; }

	const std::shared_ptr<CLayerGroup> &Group() { return m_pGroup; }
	CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height) override final;

private:
	std::shared_ptr<CLayerGroup> m_pGroup;
	char m_aName[32];
};

class CEditorFolderNode : public ITreeParentNode
{
public:
	CEditorFolderNode(const std::shared_ptr<CEditorParentGroup> &pFolder) :
		m_pFolder(pFolder), ITreeParentNode(TYPE_FOLDER) {}

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pFolder.get(); }
	const char *Name() const override;

	const std::shared_ptr<CEditorParentGroup> &Folder() { return m_pFolder; }
	std::shared_ptr<IEditorMapObject> Object() override { return std::static_pointer_cast<IEditorMapObject>(Folder()); }

private:
	std::shared_ptr<CEditorParentGroup> m_pFolder;
};

// Node for CLayer ---------------------------------------
class CLayerNode : public ITreeNode
{
public:
	CLayerNode(const std::shared_ptr<CLayer> &pLayer) :
		m_pLayer(pLayer), ITreeNode(TYPE_LAYER) {}

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pLayer.get(); }
	const char *Name() const override;

	const std::shared_ptr<CLayer> &Layer() { return m_pLayer; }
	CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height) override final;

private:
	std::shared_ptr<CLayer> m_pLayer;
};

#endif
