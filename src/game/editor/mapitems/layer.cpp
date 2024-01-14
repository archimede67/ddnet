#include "layer.h"

#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>

CUI::EPopupMenuFunctionResult CLayer::RenderCommonProperties(CEditor *pEditor, CUIRect *pToolbox, SMultiLayersInfo &Infos)
{
	std::shared_ptr<CLayerGroup> pCurrentGroup = pEditor->GetSelectedGroup();

	{ // Render red label "Editing multiple layers"
		CUIRect Warning;
		pToolbox->HSplitTop(13.0f, &Warning, pToolbox);
		Warning.HMargin(0.5f, &Warning);

		pEditor->TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
		SLabelProperties Props;
		Props.m_MaxWidth = Warning.w;
		pEditor->UI()->DoLabel(&Warning, "Editing multiple layers", 9.0f, TEXTALIGN_ML, Props);
		pEditor->TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		pToolbox->HSplitTop(2.0f, nullptr, pToolbox);
	}

	bool AnyEntities = false;
	bool AllEntities = true;

	auto &vpLayers = Infos.m_vpLayers;
	auto &vLayerIndices = Infos.m_vLayerIndices;

	for(auto &pLayer : vpLayers)
	{
		if(pLayer->IsEntitiesLayer())
			AnyEntities = true;
		else
			AllEntities = false;
	}

	// Delete button
	{
		CUIRect DeleteButton;
		pToolbox->HSplitBottom(12.0f, pToolbox, &DeleteButton);
		static int s_DeleteButton = 0;
		if(pEditor->DoButton_Editor(&s_DeleteButton, "Delete layers", 0, &DeleteButton, 0, "Deletes the selected layers (excluding game layer)"))
		{
			std::vector<int> vLocalIndices(vLayerIndices.begin(), vLayerIndices.end());
			std::sort(vLocalIndices.begin(), vLocalIndices.end());

			for(auto &pLayer : vpLayers)
			{
				if(pLayer == pEditor->m_Map.m_pFrontLayer)
					pEditor->m_Map.m_pFrontLayer = nullptr;
				if(pLayer == pEditor->m_Map.m_pTeleLayer)
					pEditor->m_Map.m_pTeleLayer = nullptr;
				if(pLayer == pEditor->m_Map.m_pSpeedupLayer)
					pEditor->m_Map.m_pSpeedupLayer = nullptr;
				if(pLayer == pEditor->m_Map.m_pSwitchLayer)
					pEditor->m_Map.m_pSwitchLayer = nullptr;
				if(pLayer == pEditor->m_Map.m_pTuneLayer)
					pEditor->m_Map.m_pTuneLayer = nullptr;
			}

			// Erase from the back to avoid issues
			std::vector<std::shared_ptr<IEditorAction>> vpActions;
			for(auto &pIt = vLocalIndices.rbegin(); pIt != vLocalIndices.rend(); pIt++)
			{
				int Index = *pIt;
				if(pCurrentGroup->m_vpLayers[Index] == pEditor->m_Map.m_pGameLayer) // Do not delete game layer
					continue;

				vpActions.emplace_back(std::make_shared<CEditorActionDeleteLayer>(pEditor, pEditor->m_SelectedGroup, Index));
				pCurrentGroup->DeleteLayer(Index);
			}
			pEditor->m_vSelectedLayers.clear();
			pEditor->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditMultipleLayers>(pEditor, vpActions, vLayerIndices, "Delete many layers", true));

			return CUI::POPUP_CLOSE_CURRENT;
		}
	}

	// Duplicate button
	// Check if all layers are entities, in which case do not show the duplicate button
	if(!AllEntities)
	{
		CUIRect DuplicateButton;
		pToolbox->HSplitBottom(4.0f, pToolbox, nullptr);
		pToolbox->HSplitBottom(12.0f, pToolbox, &DuplicateButton);
		static int s_DuplicationButton = 0;
		if(pEditor->DoButton_Editor(&s_DuplicationButton, "Duplicate layers", 0, &DuplicateButton, 0, "Duplicates the selected non-entities layers"))
		{
			pEditor->m_vSelectedLayers.clear();

			std::vector<int> vLocalIndices(vLayerIndices.begin(), vLayerIndices.end());
			std::sort(vLocalIndices.begin(), vLocalIndices.end());

			std::vector<std::shared_ptr<IEditorAction>> vpActions;
			int Offset = 0;
			for(int LayerIndex : vLocalIndices)
			{
				pCurrentGroup->DuplicateLayer(LayerIndex + Offset);
				pEditor->AddSelectedLayer(LayerIndex + Offset);
				vpActions.emplace_back(std::make_shared<CEditorActionAddLayer>(pEditor, pEditor->m_SelectedGroup, LayerIndex + Offset + 1, true));
				Offset++;
			}
			pEditor->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditMultipleLayers>(pEditor, vpActions, vLayerIndices, "Duplicate many layers", true));

			return CUI::POPUP_CLOSE_CURRENT;
		}
	}

	pToolbox->HSplitBottom(10.0f, pToolbox, nullptr);

	// Make a multi value for detail
	CMultiPropertyValue<CLayer, int, 2> Detail(
		vpLayers, [](const std::shared_ptr<CLayer> &pLayer) { return &pLayer->m_Flags; }, [](int Flags) { return Flags & LAYERFLAG_DETAIL; });

	CProperty aProps[] = {
		{"Group", pEditor->m_SelectedGroup, PROPTYPE_INT_STEP, 0, (int)pEditor->m_Map.m_vpGroups.size() - 1},
		{"Detail", Detail(), PROPTYPE_BOOL, 0, 1, Detail.Mixed()},
		{nullptr},
	};

	// Don't render group and detail properties if we have any entities layer selected, as this could be confusing when
	// mixing entities and non entities layers
	if(AnyEntities)
	{
		aProps[(int)ELayerCommonProp::PROP_GROUP].m_Type = PROPTYPE_NULL;
		aProps[(int)ELayerCommonProp::PROP_HQ].m_Type = PROPTYPE_NULL;
	}

	static int s_aIds[(int)ELayerCommonProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<ELayerCommonProp>(pToolbox, aProps, s_aIds, &NewVal);
	if(Prop != ELayerCommonProp::PROP_NONE)
		pEditor->m_Map.OnModify();

	//static CMultiLayerTilesPropTracker s_Tracker(pEditor);

	//s_Tracker.m_pvLayerIndices = &vLayerIndices;
	//s_Tracker.m_pvpLayers = &vpLayers;
	//s_Tracker.Begin(nullptr, Prop, State);

	if(Prop == ELayerCommonProp::PROP_GROUP)
	{
		if(NewVal >= 0 && (size_t)NewVal < pEditor->m_Map.m_vpGroups.size() && NewVal != pEditor->m_SelectedGroup)
		{
			pEditor->m_vSelectedLayers.clear();
			pEditor->m_SelectedGroup = NewVal;
			for(auto &pLayer : vpLayers)
			{
				auto Position = std::find(pCurrentGroup->m_vpLayers.begin(), pCurrentGroup->m_vpLayers.end(), pLayer);
				if(Position != pCurrentGroup->m_vpLayers.end())
					pCurrentGroup->m_vpLayers.erase(Position);
				pEditor->AddSelectedLayer(pEditor->m_Map.m_vpGroups[NewVal]->m_vpLayers.size());
				pEditor->m_Map.m_vpGroups[NewVal]->m_vpLayers.push_back(pLayer);
			}
		}
	}
	else if(Prop == ELayerCommonProp::PROP_HQ)
	{
		Detail.Set([NewVal](int Flags) {
			Flags &= ~LAYERFLAG_DETAIL;
			if(NewVal)
				Flags |= LAYERFLAG_DETAIL;

			return Flags;
		});
	}

	//s_Tracker.End(Prop, State);

	return CUI::POPUP_KEEP_OPEN;
}