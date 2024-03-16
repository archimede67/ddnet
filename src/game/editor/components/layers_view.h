#ifndef GAME_EDITOR_COMPONENTS_LAYERS_VIEW_H
#define GAME_EDITOR_COMPONENTS_LAYERS_VIEW_H

#include <game/client/ui_scrollregion.h>

#include <game/editor/component.h>
#include <game/editor/popups.h>

#include <game/editor/mapitems/group_ex.h>

using namespace EditorPopups;
class CEditorMap;

struct SObjectSelection
{
	int m_Type;
	int m_Index;
};

class CLayersView : public CEditorComponent
{
public:
	void Init(CEditor *pEditor) override;

	void Render(CUIRect LayersBox);

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

	struct SCommonGroupProps
	{
		bool *m_pVisible;
		bool *m_pCollapse;
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

	std::vector<SObjectSelection> m_vSelection;

private:
	void RenderLayersGroup(CUIRect *pRect, const CEditorGroupInfo &Info);
	void RenderParentGroup(CUIRect *pRect, int Index, const CEditorGroupInfo &Info);

	void SetOperation(int Operation);
	inline void ResetDragContext();
	inline void ResetRenderContext();

	int DoGroupButton(const CEditorGroupInfo *pGroupInfo, const char *pText, int Checked, const CUIRect *pRect, bool *pClicked, bool *pAbrupted, const char *pToolTip);
	std::shared_ptr<IGroup> GetGroupBase(const CEditorGroupInfo *pGroupInfo);

private:
	static constexpr float MIN_DRAG_DISTANCE = 5.0f;
	static constexpr float INDENT = 12.0f;
	static constexpr float ROW_HEIGHT = 12.0f;
};

#endif
