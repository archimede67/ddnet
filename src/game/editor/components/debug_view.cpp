#include "debug_view.h"

#include <game/editor/editor.h>

void CDebugView::OnRender(CUIRect View)
{
	CUIRect Panel;
	View.VSplitRight(250.0f, nullptr, &Panel);

	Panel.Margin(16.0f, &Panel);
	RenderSelection(Panel);
}

void CDebugView::RenderSelection(CUIRect View)
{
	const std::vector<int> &vLayerIndices = Editor()->m_vSelectedLayers;

	char aBuf[64];
	CUIRect CurrentGroupLabel;
	View.HSplitTop(12.0f, &CurrentGroupLabel, &View);
	str_format(aBuf, sizeof(aBuf), "Current group: %d", Editor()->m_SelectedGroup);
	Ui()->DoLabel(&CurrentGroupLabel, aBuf, 8.0f, TEXTALIGN_ML);

	CUIRect Rect;
	View.HSplitTop(12.0f, &Rect, &View);
	Ui()->DoLabel(&Rect, "Selection:", 8.0f, TEXTALIGN_ML);
	for(const auto &SelectedPath : Editor()->LayersView()->Selection())
	{
		View.HSplitTop(12.0f, &Rect, &View);
		SelectedPath.m_pNode->m_Path.ToString(aBuf);
		Ui()->DoLabel(&Rect, aBuf, 8.0f, TEXTALIGN_ML);
	}

	View.HSplitTop(12.0f, &Rect, &View);
	Ui()->DoLabel(&Rect, "Current groups order:", 8.0f, TEXTALIGN_ML);

	for(int i = 0; i < Editor()->m_Map.m_vpGroups.size(); i++)
	{
		View.HSplitTop(12.0f, &Rect, &View);
		str_format(aBuf, sizeof(aBuf), "%d: %s", i, Editor()->m_Map.m_vpGroups[i]->m_aName);
		Ui()->DoLabel(&Rect, aBuf, 8.0f, TEXTALIGN_ML);
	}

	View.HSplitTop(12.0f, &Rect, &View);
	Ui()->DoLabel(&Rect, "Selected layers:", 8.0f, TEXTALIGN_ML);

	for(const auto &Index : vLayerIndices)
	{
		CUIRect Label;
		View.HSplitTop(12.0f, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%d", Index);
		Ui()->DoLabel(&Label, aBuf, 8.0f, TEXTALIGN_ML);
	}
}
