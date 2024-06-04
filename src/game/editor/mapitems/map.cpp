#include <game/editor/editor.h>

#include "image.h"

void CEditorMap::OnModify()
{
	m_Modified = true;
	m_ModifiedAuto = true;
	m_LastModifiedTime = m_pEditor->Client()->GlobalTime();
}

void CEditorMap::DeleteEnvelope(int Index)
{
	if(Index < 0 || Index >= (int)m_vpEnvelopes.size())
		return;

	OnModify();

	VisitEnvelopeReferences([Index](int &ElementIndex) {
		if(ElementIndex == Index)
			ElementIndex = -1;
		else if(ElementIndex > Index)
			ElementIndex--;
	});

	m_vpEnvelopes.erase(m_vpEnvelopes.begin() + Index);
}

void CEditorMap::SwapEnvelopes(int Index0, int Index1)
{
	if(Index0 < 0 || Index0 >= (int)m_vpEnvelopes.size())
		return;
	if(Index1 < 0 || Index1 >= (int)m_vpEnvelopes.size())
		return;
	if(Index0 == Index1)
		return;

	OnModify();

	VisitEnvelopeReferences([Index0, Index1](int &ElementIndex) {
		if(ElementIndex == Index0)
			ElementIndex = Index1;
		else if(ElementIndex == Index1)
			ElementIndex = Index0;
	});

	std::swap(m_vpEnvelopes[Index0], m_vpEnvelopes[Index1]);
}

template<typename F>
void CEditorMap::VisitEnvelopeReferences(F &&Visitor)
{
	for(auto &pGroup : m_vpGroups)
	{
		for(auto &pLayer : pGroup->m_vpLayers)
		{
			if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				std::shared_ptr<CLayerQuads> pLayerQuads = std::static_pointer_cast<CLayerQuads>(pLayer);
				for(auto &Quad : pLayerQuads->m_vQuads)
				{
					Visitor(Quad.m_PosEnv);
					Visitor(Quad.m_ColorEnv);
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				std::shared_ptr<CLayerTiles> pLayerTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
				Visitor(pLayerTiles->m_ColorEnv);
			}
			else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
			{
				std::shared_ptr<CLayerSounds> pLayerSounds = std::static_pointer_cast<CLayerSounds>(pLayer);
				for(auto &Source : pLayerSounds->m_vSources)
				{
					Visitor(Source.m_PosEnv);
					Visitor(Source.m_SoundEnv);
				}
			}
		}
	}
}

void CEditorMap::MakeGameLayer(const std::shared_ptr<CLayer> &pLayer)
{
	m_pGameLayer = std::static_pointer_cast<CLayerGame>(pLayer);
	m_pGameLayer->m_pEditor = m_pEditor;
}

void CEditorMap::MakeGameGroup(std::shared_ptr<CLayerGroup> pGroup)
{
	m_pGameGroup = std::move(pGroup);
	m_pGameGroup->m_GameGroup = true;
	str_copy(m_pGameGroup->m_aName, "Game");
}

void CEditorMap::Clean()
{
	m_vpGroups.clear();
	m_vpEnvelopes.clear();
	m_vpImages.clear();
	m_vpSounds.clear();
	m_pTreeRoot = std::make_shared<CRootObject>();

	m_MapInfo.Reset();
	m_MapInfoTmp.Reset();

	m_vSettings.clear();

	m_pGameLayer = nullptr;
	m_pGameGroup = nullptr;

	m_Modified = false;
	m_ModifiedAuto = false;

	m_pTeleLayer = nullptr;
	m_pSpeedupLayer = nullptr;
	m_pFrontLayer = nullptr;
	m_pSwitchLayer = nullptr;
	m_pTuneLayer = nullptr;
}

void CEditorMap::CreateDefault(IGraphics::CTextureHandle EntitiesTexture)
{
	// add background
	std::shared_ptr<CLayerGroup> pGroup = NewGroup();
	pGroup->m_ParallaxX = 0;
	pGroup->m_ParallaxY = 0;
	std::shared_ptr<CLayerQuads> pLayer = std::make_shared<CLayerQuads>(m_pEditor);
	CQuad *pQuad = pLayer->NewQuad(0, 0, 1600, 1200);
	pQuad->m_aColors[0].r = pQuad->m_aColors[1].r = 94;
	pQuad->m_aColors[0].g = pQuad->m_aColors[1].g = 132;
	pQuad->m_aColors[0].b = pQuad->m_aColors[1].b = 174;
	pQuad->m_aColors[2].r = pQuad->m_aColors[3].r = 204;
	pQuad->m_aColors[2].g = pQuad->m_aColors[3].g = 232;
	pQuad->m_aColors[2].b = pQuad->m_aColors[3].b = 255;
	pGroup->AddLayer(pLayer);

	// add game layer and reset front, tele, speedup, tune and switch layer pointers
	MakeGameGroup(NewGroup());
	MakeGameLayer(std::make_shared<CLayerGame>(m_pEditor, 50, 50));
	m_pGameGroup->AddLayer(m_pGameLayer);

	m_pFrontLayer = nullptr;
	m_pTeleLayer = nullptr;
	m_pSpeedupLayer = nullptr;
	m_pSwitchLayer = nullptr;
	m_pTuneLayer = nullptr;
}

void CEditorMap::MakeTeleLayer(const std::shared_ptr<CLayer> &pLayer)
{
	m_pTeleLayer = std::static_pointer_cast<CLayerTele>(pLayer);
	m_pTeleLayer->m_pEditor = m_pEditor;
}

void CEditorMap::MakeSpeedupLayer(const std::shared_ptr<CLayer> &pLayer)
{
	m_pSpeedupLayer = std::static_pointer_cast<CLayerSpeedup>(pLayer);
	m_pSpeedupLayer->m_pEditor = m_pEditor;
}

void CEditorMap::MakeFrontLayer(const std::shared_ptr<CLayer> &pLayer)
{
	m_pFrontLayer = std::static_pointer_cast<CLayerFront>(pLayer);
	m_pFrontLayer->m_pEditor = m_pEditor;
}

void CEditorMap::MakeSwitchLayer(const std::shared_ptr<CLayer> &pLayer)
{
	m_pSwitchLayer = std::static_pointer_cast<CLayerSwitch>(pLayer);
	m_pSwitchLayer->m_pEditor = m_pEditor;
}

void CEditorMap::MakeTuneLayer(const std::shared_ptr<CLayer> &pLayer)
{
	m_pTuneLayer = std::static_pointer_cast<CLayerTune>(pLayer);
	m_pTuneLayer->m_pEditor = m_pEditor;
}

std::shared_ptr<CLayerGroup> CEditorMap::NewGroup(bool CreateInfo)
{
	const int Index = (int)m_vpGroups.size();

	OnModify();
	std::shared_ptr<CLayerGroup> pGroup = std::make_shared<CLayerGroup>();
	pGroup->m_pMap = this;
	m_vpGroups.push_back(pGroup);

	// std::shared_ptr<CLayerGroupObject> pGroupObject = std::make_shared<CLayerGroupObject>(Index);
	// pGroupObject->m_pMap = this;
	// m_vpRootObjects.push_back(pGroupObject);

	const auto pGroupObj = CreateObject<CLayerGroupObject>(Index);
	pGroupObj->m_pMap = this;
	m_pTreeRoot->m_vpChildren.push_back(pGroupObj);

	// if(CreateInfo)
	//{
	//	// Insert a group info for each layers group
	//	CEditorGroupInfo Info;
	//	Info.m_GroupIndex = Index;
	//	Info.m_Type = CEditorGroupInfo::TYPE_LAYER_GROUP;
	//	Info.m_ParentIndex = CEditorGroupInfo::PARENT_NONE;
	//	m_vGroupInfos.push_back(Info);
	// }

	// TODO: removeme
	// std::shared_ptr<CEditorParentGroup> pGroupParent = std::make_shared<CEditorParentGroup>();
	// str_format(pGroupParent->m_aName, sizeof(pGroupParent->m_aName), "Test Parent #%d", (int)m_vpGroupParents.size());
	// CEditorGroupInfo ParentInfo;
	// ParentInfo.m_GroupIndex = (int)m_vpGroupParents.size();
	// ParentInfo.m_Children.push_back(m_vGroupInfos.size() - 1);
	// ParentInfo.m_Type = CEditorGroupInfo::PARENT_GROUP;
	// m_vpGroupParents.push_back(pGroupParent);
	// m_vGroupInfos.push_back(ParentInfo);

	return pGroup;
}

// CEditorGroupInfo &CEditorMap::GroupSelection(const std::vector<int> &vSelectedGroupItems)
//{
//	int TargetIndex = vSelectedGroupItems[0];
//	int ParentIndex = m_vGroupInfos[TargetIndex].m_ParentIndex;
//
//	// Create a new ParentGroup
//	CEditorGroupInfo &Info = NewParentGroup();
//	int NewTargetIndex = m_vGroupInfos.size() - 1;
//
//	// Fix the index pointers
//	for(auto &GroupInfo : m_vGroupInfos)
//	{
//		if(GroupInfo.m_ParentIndex == TargetIndex)
//			GroupInfo.m_ParentIndex = NewTargetIndex;
//	}
//
//	// Change the parent of the selection to this new GroupInfo
//	for(int Selected : vSelectedGroupItems)
//		m_vGroupInfos[Selected].m_ParentIndex = TargetIndex;
//
//	// Change the parent of the newly created GroupInfo to be the parent of the first selected element
//	Info.m_ParentIndex = ParentIndex;
//
//	// Swap the group info to be at the position of the first element in the
//	std::swap(m_vGroupInfos[TargetIndex], m_vGroupInfos[NewTargetIndex]);
//
//	return Info;
// }

// void CEditorMap::UngroupSelection(const std::vector<int> &vSelectedGroupItems)
//{
//	std::vector<int> vSortedSelectedGroupItems = vSelectedGroupItems;
//	std::sort(vSortedSelectedGroupItems.begin(), vSortedSelectedGroupItems.end());
//
//	// Ungroup : assign the parent of each children of the item in the selection to the parent of that item
//	for(size_t i = 0; i < vSortedSelectedGroupItems.size(); i++)
//	{
//		int Selected = vSortedSelectedGroupItems[i];
//		int SelectedGroupIndex = m_vGroupInfos[Selected].m_GroupIndex;
//
//		int NewParent = m_vGroupInfos[Selected].m_ParentIndex;
//		NewParent = NewParent > Selected ? NewParent - 1 : NewParent;
//
//		for(size_t k = 0; k < m_vGroupInfos.size(); k++)
//		{
//			if(m_vGroupInfos[k].m_ParentIndex == Selected)
//				m_vGroupInfos[k].m_ParentIndex = NewParent;
//
//			// Shift indices
//			if(m_vGroupInfos[k].m_ParentIndex > Selected)
//				m_vGroupInfos[k].m_ParentIndex--;
//
//			if(m_vGroupInfos[k].m_GroupIndex > SelectedGroupIndex && m_vGroupInfos[k].m_Type == CEditorGroupInfo::TYPE_PARENT_GROUP)
//				m_vGroupInfos[k].m_GroupIndex--;
//		}
//
//		// Remove group info & parent group
//		m_vpGroupParents.erase(m_vpGroupParents.begin() + SelectedGroupIndex);
//		m_vGroupInfos.erase(m_vGroupInfos.begin() + Selected);
//
//		// Shift selection indices
//		for(size_t k = i + 1; k < vSortedSelectedGroupItems.size(); k++)
//			vSortedSelectedGroupItems[k]--;
//	}
// }

// void CEditorMap::NewGroups(const std::vector<int> &vSelectedGroupItems)
//{
//	// Add a new layers group for each selected group
//	for(int Selected : vSelectedGroupItems)
//	{
//		NewGroup(true);
//		CEditorGroupInfo &Info = m_vGroupInfos.back();
//		Info.m_ParentIndex = Selected;
//	}
// }

// void CEditorMap::NewNestedGroups(const std::vector<int> &vSelectedGroupItems)
//{
//	// Add a new parent group inside each selected group
//	for(int Selected : vSelectedGroupItems)
//	{
//		CEditorGroupInfo &Info = NewParentGroup();
//		Info.m_ParentIndex = Selected;
//	}
// }

// CEditorGroupInfo &CEditorMap::NewParentGroup()
//{
//	// Create a new ParentGroup
//	std::shared_ptr<CEditorParentGroup> pGroupParent = std::make_shared<CEditorParentGroup>();
//	str_copy(pGroupParent->m_aName, "New group");
//	pGroupParent->m_Collapse = false;
//	pGroupParent->m_Visible = true;
//	int Index = m_vpGroupParents.size();
//	m_vpGroupParents.push_back(pGroupParent);
//
//	// Create a GroupInfo pointing to this new ParentGroup
//	CEditorGroupInfo Info;
//	Info.m_Type = CEditorGroupInfo::TYPE_PARENT_GROUP;
//	Info.m_GroupIndex = Index;
//	Info.m_ParentIndex = CEditorGroupInfo::PARENT_NONE;
//	int GroupInfoIndex = m_vGroupInfos.size();
//	m_vGroupInfos.push_back(Info);
//
//	return m_vGroupInfos.back();
// }

void CEditorMap::DeleteGroup(int Index)
{
	if(Index < 0 || Index >= (int)m_vpGroups.size())
		return;
	OnModify();
	m_vpGroups.erase(m_vpGroups.begin() + Index);

	// Remove linked GroupInfo
	// auto GroupInfoPos = std::find_if(m_vGroupInfos.begin(), m_vGroupInfos.end(), [Index](const CEditorGroupInfo &Info) {
	//	return Info.m_GroupIndex == Index;
	//});

	// if(GroupInfoPos != m_vGroupInfos.end())
	//{
	//	const int GroupInfoIndex = GroupInfoPos - m_vGroupInfos.begin();
	//	m_vGroupInfos.erase(GroupInfoPos);

	//	// Shift indices
	//	for(size_t k = 0; k < m_vGroupInfos.size(); k++)
	//	{
	//		if(m_vGroupInfos[k].m_ParentIndex >= GroupInfoIndex)
	//			m_vGroupInfos[k].m_ParentIndex--;
	//		if(m_vGroupInfos[k].m_Type == CEditorGroupInfo::TYPE_LAYER_GROUP && m_vGroupInfos[k].m_GroupIndex >= Index)
	//			m_vGroupInfos[k].m_GroupIndex--;
	//	}
	//}
}

// int CEditorMap::GroupInfoIndex(CEditorGroupInfo::EType Type, int GroupIndex)
//{
//	// Gets a group info index from a group index
//	auto GroupInfoPos = std::find_if(m_vGroupInfos.begin(), m_vGroupInfos.end(), [Type, GroupIndex](const CEditorGroupInfo &Info) {
//		return Info.m_Type == Type && Info.m_GroupIndex == GroupIndex;
//	});
//
//	if(GroupInfoPos == m_vGroupInfos.end())
//		return -1;
//
//	return GroupInfoPos - m_vGroupInfos.begin();
// }
