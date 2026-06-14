#include "editor_tree_node.h"

#include <game/editor/components/editor_tree_view.h>
#include <game/editor/editor.h>

bool IEditorTreeNode::Selected() const
{
	return TreeView()->IsSelected(this);
}