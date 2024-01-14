#ifndef GAME_EDITOR_MAPITEMS_LAYER_QUADS_H
#define GAME_EDITOR_MAPITEMS_LAYER_QUADS_H

#include <game/editor/mapitems.h>

#include "layer.h"

class CLayerQuads : public CLayer
{
public:
	explicit CLayerQuads(CEditor *pEditor);
	CLayerQuads(const CLayerQuads &Other);
	~CLayerQuads();

	void Render(bool QuadPicker = false) override;
	CQuad *NewQuad(int x, int y, int Width, int Height);
	int SwapQuads(int Index0, int Index1);

	void BrushSelecting(CUIRect Rect) override;
	int BrushGrab(std::shared_ptr<CLayerGroup> pBrush, CUIRect Rect) override;
	void BrushPlace(std::shared_ptr<CLayer> pBrush, float wx, float wy) override;
	void BrushFlipX() override;
	void BrushFlipY() override;
	void BrushRotate(float Amount) override;

	CUI::EPopupMenuFunctionResult RenderProperties(CUIRect *pToolbox) override;
	static CUI::EPopupMenuFunctionResult RenderCommonProperties(CEditor *pEditor, CUIRect *pToolbox, SMultiLayersInfo &Infos);

	void ModifyImageIndex(FIndexModifyFunction pfnFunc) override;
	void ModifyEnvelopeIndex(FIndexModifyFunction pfnFunc) override;

	void GetSize(float *pWidth, float *pHeight) override;
	std::shared_ptr<CLayer> Duplicate() const override;
	const char *TypeName() const override;

	int m_Image;
	std::vector<CQuad> m_vQuads;

private:
	static int *PropertyAccessor(CLayerQuads &LayerQuads, ELayerQuadsProp Property)
	{
		switch(Property)
		{
		case ELayerQuadsProp::PROP_IMAGE:
			return &LayerQuads.m_Image;
		default:
			break;
		}
		return nullptr;
	}
};

#endif
