#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_H

#include <game/client/ui_scrollregion.h>

#include <game/editor/component.h>
#include <game/editor/popups.h>

#include <game/editor/mapitems/group_ex.h>

using namespace EditorPopups;

class CEditorMap;
class CLayer;

class CToggleFlags
{
public:
	enum EToggle
	{
		TOGGLE_VISIBILE,
		TOGGLE_COLLAPSE,
		NUM_TOGGLES
	};

	CToggleFlags() :
		m_vpFlags(NUM_TOGGLES)
	{
		std::fill(m_vpFlags.begin(), m_vpFlags.end(), nullptr);
	}

	bool *&operator[](EToggle Toggle)
	{
		dbg_assert(Toggle != NUM_TOGGLES, "Invalid toggle flag");
		return m_vpFlags[Toggle];
	}

	bool *operator[](EToggle Toggle) const
	{
		dbg_assert(Toggle != NUM_TOGGLES, "Invalid toggle flag");
		return m_vpFlags.at(Toggle);
	}

private:
	std::vector<bool *> m_vpFlags;
};

struct SEditorSelectableInfo
{
	enum EType
	{
		TYPE_GROUP_INFO,
		TYPE_LAYER
	};

	int m_Type;
	int m_Index;
};

// Idea: decouple actual objects from the UI
struct CTreeNode
{
	char m_aName[64];
	int m_Type;
	std::shared_ptr<ITreeNode> m_pData;
	std::vector<std::shared_ptr<CTreeNode>> m_vpChildren;

	enum
	{
		TYPE_NONE = -1,
		TYPE_LAYER,
		TYPE_LAYER_GROUP,
		TYPE_FOLDER,
	};
};

class CTreeView;

class CLayersView : public CEditorComponent
{
public:
	void Init(CEditor *pEditor) override;

	void Render(CUIRect LayersBox);
	//void OnRender(CUIRect View) override;

	//void InternalRender(CTreeView &, const std::shared_ptr<TreeNode> &);

private:
	struct SRenderContext
	{
		bool m_ScrollToSelection;
	};

	struct SExtraRenderInfo
	{
		const char *m_pIcon = nullptr;
	};

private:
	CScrollRegion m_ScrollRegion;
	bool m_ScrollToSelectionNext;

	SRenderContext m_RenderContext;
	SParentGroupPopupContext m_ParentPopupContext;

	std::vector<SEditorSelectableInfo> m_vSelection;

private:
	void RenderLayersGroup(CUIRect *pRect, const CEditorGroupInfo &Info);
	void RenderParentGroup(CUIRect *pRect, int Index, const CEditorGroupInfo &Info);

	inline void ResetRenderContext();

	int DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra = {});
	//std::shared_ptr<IGroup> GetGroupBase(const CEditorGroupInfo &pGroupInfo);

	int DoToggleIconButton(const void *pButtonId, const void *pParentId, const char *pIcon, bool Checked, const CUIRect *pRect, const char *pToolTip);
	void DoIcon(const char *pIcon, const CUIRect *pRect, float FontSize);

	CToggleFlags GroupFlags(const CEditorGroupInfo &pGroupInfo);
	CToggleFlags LayerFlags(const std::shared_ptr<CLayer> &pLayer);

	bool CanHandleInput() const;

private:
	static constexpr float ROW_HEIGHT = 12.0f;
};

#endif
