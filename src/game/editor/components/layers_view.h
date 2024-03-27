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

// TODO: have an object that points to anything based on a type enum, used to render? So we can store bounding box/rect used to render

class CLayersView : public CEditorComponent
{
public:
	void Init(CEditor *pEditor) override;

	void Render(CUIRect LayersBox);
	void OnRender(CUIRect View) override;

private:
	struct SDragContext
	{
		int m_GroupAfterDraggedLayer;
		int m_LayerAfterDraggedLayer;
		bool m_DraggedPositionFound;
		bool m_MoveLayers;
		bool m_MoveGroup;
		bool m_StartDragLayer;
		bool m_StartDragGroup;
	};

	struct SRenderContext
	{
		CUIRect m_UnscrolledLayersBox;
		std::vector<int> m_vButtonsPerGroup;
		bool m_ScrollToSelection;
	};

	struct SExtraRenderInfo
	{
		const char *m_pIcon = nullptr;
	};

private:
	enum
	{
		OP_NONE = 0,
		OP_CLICK,
		OP_LAYER_DRAG,
		OP_GROUP_DRAG
	};

	int m_Operation = OP_NONE;
	int m_PreviousOperation = OP_NONE;
	const void *m_pDraggedButton = nullptr;
	float m_InitialMouseY = 0;
	float m_InitialCutHeight = 0;
	CScrollRegion m_ScrollRegion;
	bool m_ScrollToSelectionNext;

	SDragContext m_DragContext;
	SRenderContext m_RenderContext;
	SParentGroupPopupContext m_ParentPopupContext;

	std::vector<SEditorSelectableInfo> m_vSelection;

private:
	void RenderLayersGroup(CUIRect *pRect, const CEditorGroupInfo &Info);
	void RenderParentGroup(CUIRect *pRect, int Index, const CEditorGroupInfo &Info);

	void SetOperation(int Operation);
	inline void ResetDragContext();
	inline void ResetRenderContext();

	int DoSelectable(const void *pId, const char *pText, const CToggleFlags &Flags, int Checked, const CUIRect *pRect, const char *pToolTip, const SExtraRenderInfo &Extra = {});
	std::shared_ptr<IGroup> GetGroupBase(const CEditorGroupInfo &pGroupInfo);

	int DoToggleIconButton(const void *pButtonId, const void *pParentId, const char *pIcon, bool Checked, const CUIRect *pRect, const char *pToolTip);
	void DoIcon(const char *pIcon, const CUIRect *pRect, float FontSize);

	CToggleFlags GroupFlags(const CEditorGroupInfo &pGroupInfo);
	CToggleFlags LayerFlags(const std::shared_ptr<CLayer> &pLayer);

private:
	static constexpr float MIN_DRAG_DISTANCE = 5.0f;
	static constexpr float INDENT = 12.0f;
	static constexpr float ROW_HEIGHT = 12.0f;
};

#endif
