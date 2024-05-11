#include "map_object.h"

void IEditorMapObject::Render()
{
	for(auto &pChild : m_vpChildren)
		pChild->Render();
}
