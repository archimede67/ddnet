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

	for(const auto &Index : vLayerIndices)
	{
		char aBuf[16];
		CUIRect Label;
		View.HSplitTop(20.0f, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%d", Index);
		Ui()->DoLabel(&Label, aBuf, 18.0f, TEXTALIGN_ML);
	}
}
