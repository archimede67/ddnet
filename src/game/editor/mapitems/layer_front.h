#ifndef GAME_EDITOR_MAPITEMS_LAYER_FRONT_H
#define GAME_EDITOR_MAPITEMS_LAYER_FRONT_H

#include "layer_tiles.h"

class CLayerFront : public CLayerTiles
{
public:
	CLayerFront(CEditor *pEditor, int w, int h);

	void Resize(int NewW, int NewH) override;
	void SetTile(int x, int y, CTile Tile) override;
	const char *TypeName() const override;

	inline const char *Icon() const override { return FONT_ICON_BORDER_ALL; }
};

#endif
