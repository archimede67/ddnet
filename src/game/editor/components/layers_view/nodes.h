#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODES_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_NODES_H

#include "node.h"

#include <memory>

class CLayerGroupObject;
class CLayer;
class CLayerGroup;
class CLayerNode;
class CEditorMap;
class CEditorParentGroup;

// Node for CEditorMap (root)
class CEditorMapNode final : public ITreeParentNode
{
public:
	CEditorMapNode(CEditorMap *pMap) :
		ITreeParentNode(TYPE_ROOT), m_pMap(pMap) {}

	void AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild) override;

	std::shared_ptr<IEditorMapObject> Object() override;

	bool *Visible() override { return nullptr; }
	bool *Collapse() override { return nullptr; }
	const void *Id() override { return m_pMap; }
	const char *Name() override { return "Root"; }

private:
	CEditorMap *m_pMap;
};

// Node for CLayerGroup
class CLayerGroupNode final : public ITreeParentNode
{
public:
	CLayerGroupNode(int Index, const std::shared_ptr<CLayerGroupObject> &pGroupObject, const std::shared_ptr<CLayerGroup> &pGroup);

	void AddChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild) override;
	void RemoveChild(const CIndex &Index, const std::shared_ptr<ITreeNode> &pChild) override;

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pGroup.get(); }
	const char *Name() override;

	ENodeSelectResult OnSelect() override;
	void OnDeselect() override;
	void Decorate(CUIRect View) override;

	const std::shared_ptr<CLayerGroup> &Group() const { return m_pGroup; }

	CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height) override;
	std::shared_ptr<IEditorMapObject> Object() override;

private:
	int m_GroupIndex;
	std::shared_ptr<CLayerGroupObject> m_pGroupObject;
	std::shared_ptr<CLayerGroup> m_pGroup;
	char m_aName[32];

	friend struct CLayerGroupNodeQuery;
};

class CEditorFolderNode final : public ITreeParentNode
{
public:
	CEditorFolderNode(const std::shared_ptr<CEditorParentGroup> &pFolder) :
		ITreeParentNode(TYPE_FOLDER), m_pFolder(pFolder) {}

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pFolder.get(); }
	const char *Name() override;

	const std::shared_ptr<CEditorParentGroup> &Folder() { return m_pFolder; }
	std::shared_ptr<IEditorMapObject> Object() override;

private:
	std::shared_ptr<CEditorParentGroup> m_pFolder;
};

// Node for CLayer ---------------------------------------
class CLayerNode final : public ITreeNode
{
public:
	CLayerNode(const int GroupIndex, const int Index, const std::shared_ptr<CLayer> &pLayer) :
		ITreeNode(TYPE_LAYER), m_GroupIndex(GroupIndex), m_Index(Index), m_pLayer(pLayer) {}

	bool *Visible() override;
	bool *Collapse() override;
	const void *Id() override { return m_pLayer.get(); }
	const char *Name() override;

	ENodeSelectResult OnSelect() override;
	void OnDeselect() override;

	const std::shared_ptr<CLayer> &Layer() { return m_pLayer; }
	CUi::EPopupMenuFunctionResult Popup(CUIRect View, int &Height) override;

private:
	int m_GroupIndex;
	int m_Index;
	std::shared_ptr<CLayer> m_pLayer;

	friend struct CLayerNodeQuery;
	friend struct CLayerGroupNodeChildrenQuery;
};

#endif
