#ifndef GAME_EDITOR_COMPONENTS_DEBUG_VIEW_H
#define GAME_EDITOR_COMPONENTS_DEBUG_VIEW_H

#include <game/editor/component.h>

class CDebugView final : public CEditorComponent
{
public:
	void OnRender(CUIRect View) override;

private:
	void RenderSelection(CUIRect View);
};

#endif
