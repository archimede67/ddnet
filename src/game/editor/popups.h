#ifndef GAME_EDITOR_POPUPS_H
#define GAME_EDITOR_POPUPS_H

#include <game/client/ui.h>

class CEditor;

namespace EditorPopups {

struct SParentGroupPopupContext : public SPopupMenuId
{
	CEditor *m_pEditor;
	int m_GroupInfoIndex;
};

} // namespace EditorPopups
#endif
