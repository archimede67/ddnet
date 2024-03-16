#ifndef GAME_EDITOR_MAPITEMS_IMAGE_H
#define GAME_EDITOR_MAPITEMS_IMAGE_H

#include <engine/graphics.h>

#include <game/editor/component.h>
#include <game/editor/components/auto_map.h>

class CEditorImage : public CImageInfo, public CEditorComponent
{
public:
	explicit CEditorImage(CEditor *pEditor);
	~CEditorImage();

	void Init(CEditor *pEditor) override;
	void AnalyseTileFlags();
	bool DataEquals(const CEditorImage &Other) const;

	IGraphics::CTextureHandle m_Texture;
	int m_External = 0;
	char m_aName[IO_MAX_PATH_LENGTH] = "";
	unsigned char m_aTileFlags[256];
	CAutoMapper m_AutoMapper;
};

#endif
