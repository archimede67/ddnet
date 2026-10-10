#include <engine/shared/config.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#undef DELETE
#undef IMAGE_BITMAP
#undef IMAGE_ICON
#undef IMAGE_CURSOR
#pragma comment(lib, "psapi.lib")

namespace perf_v1 {
void Memory(const char *Phase) {
 PROCESS_MEMORY_COUNTERS_EX M{}; M.cb=sizeof(M);
 GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&M), sizeof(M));
 std::printf("PERF_MEMORY phase=%s working=%zu private=%zu peak_working=%zu peak_private=%zu\n",Phase,static_cast<size_t>(M.WorkingSetSize),static_cast<size_t>(M.PrivateUsage),static_cast<size_t>(M.PeakWorkingSetSize),static_cast<size_t>(M.PeakPagefileUsage));
 std::fflush(stdout);
}
}

class CPerfEditor final : public CEditor
{
 bool m_Ran=false;
 void RenderProbe(const char *Phase) { perf_v1::Timer T(Phase); CEditor::OnRender(); Graphics()->Swap(); Graphics()->WaitForIdle(); }
 void Exercise(int GroupIndex,int LayerIndex,const char *Label)
 {
  auto pTiles=std::static_pointer_cast<CLayerTiles>(Map()->m_vpGroups[GroupIndex]->m_vpLayers[LayerIndex]);
  Map()->SelectLayer(LayerIndex,GroupIndex);
  std::printf("PERF_LAYER label=%s group=%d layer=%d width=%d height=%d name=%s\n",Label,GroupIndex,LayerIndex,pTiles->Width(),pTiles->Height(),pTiles->m_aName);
  auto Original=pTiles->m_Tiles[0];
  auto Changed=Original; Changed.m_Index=Original.m_Index==1?2:1;
  // Retain no layer wrapper across history publication.
  pTiles.reset();
  auto Layer=[&] { return std::static_pointer_cast<CLayerTiles>(Map()->m_vpGroups[GroupIndex]->m_vpLayers[LayerIndex]); };
  auto &Timeline=Map()->m_DocumentHistory;
  int Owner=0;
  if(std::getenv("PERF_PREFILL50"))
  {
   g_Config.m_ClEditorMaxHistory=50; Timeline.ApplyPreferences(); perf_v1::Check(Timeline.Clear(),"prefill clear");
   for(int Entry=0;Entry<50;++Entry)
    perf_v1::Check(Timeline.Edit(&Owner,"Prefill",editor_history::ECategory::MAP,[&]{ auto Tile=Layer()->m_Tiles[0]; Tile.m_Index=Tile.m_Index==1?2:1; Layer()->m_Tiles.Set(0,Tile); }),"prefill edit");
   perf_v1::Check(Timeline.History()->Revisions().size()==51,"prefill count");
  }
  for(int Sample=0;Sample<55;++Sample)
  {
   Original=Layer()->m_Tiles[0]; Changed=Original; Changed.m_Index=Original.m_Index==1?2:1;
   if(std::getenv("PERF_PREFILL50")) perf_v1::Check(Timeline.History()->Revisions().size()==51,"full timeline sample");
   std::printf("PERF_SAMPLE label=%s sample=%d\n",Label,Sample);
   { perf_v1::Timer T("edit_begin"); perf_v1::Check(Timeline.Begin(&Owner,"Sparse tile",editor_history::ECategory::MAP),"begin"); }
   { perf_v1::Timer T("edit_write"); perf_v1::Check(Timeline.Update(&Owner,[&]{Layer()->m_Tiles.Set(0,Changed);}),"write"); }
   { perf_v1::Timer T("edit_commit"); perf_v1::Check(Timeline.Complete(&Owner,CEditorDocumentHistory::ECompletion::ACCEPT),"commit"); }
   perf_v1::Check(Layer()->m_Tiles[0]==Changed,"edit value");
   RenderProbe("edit_next_render");
   { perf_v1::Timer T("undo"); perf_v1::Check(Timeline.Undo() && Timeline.PublishAtSafePoint(),"undo"); }
   perf_v1::Check(Layer()->m_Tiles[0]==Original,"undo value");
   RenderProbe("undo_next_render");
   { perf_v1::Timer T("redo"); perf_v1::Check(Timeline.Redo() && Timeline.PublishAtSafePoint(),"redo"); }
   perf_v1::Check(Layer()->m_Tiles[0]==Changed,"redo value");
   RenderProbe("redo_next_render");
   if(!std::getenv("PERF_ACCUMULATE_HISTORY")) perf_v1::Check(Timeline.Undo() && Timeline.PublishAtSafePoint(),"reset undo");
  }
  std::printf("PERF_RETAINED label=%s revisions=%zu\n", Label, Timeline.History()->Revisions().size());
  if(std::getenv("PERF_ACCUMULATE_HISTORY")) perf_v1::Check(Timeline.History()->Revisions().size()==51, "fifty undo entries plus baseline");
  perf_v1::Memory(Label);
 }
public:
 void OnUpdate() override
 {
  CEditor::OnUpdate();
  if(m_Ran || str_comp(Map()->m_aFilename,"maps/fixture.map")!=0) return;
  m_Ran=true;
  CloseMap(0,false);
  perf_v1::Memory("loaded");
  m_Dialog=DIALOG_NONE;
  if(std::getenv("PERF_HISTORY_PANEL")) m_ActiveExtraEditor=EXTRAEDITOR_HISTORY;
  RenderProbe("first_editor_render");
  perf_v1::Memory("first_rendered");
  if(std::getenv("PERF_LOAD_ONLY")) { std::puts("PERF_PASS load_only"); std::fflush(stdout); Client()->Quit(); return; }
  int GameG=-1,GameL=-1,LargeG=-1,LargeL=-1;
  size_t Largest=0;
  for(size_t G=0;G<Map()->m_vpGroups.size();++G)
   for(size_t L=0;L<Map()->m_vpGroups[G]->m_vpLayers.size();++L)
   {
    auto pLayer=Map()->m_vpGroups[G]->m_vpLayers[L];
    if(pLayer==Map()->m_pGameLayer){ GameG=static_cast<int>(G);GameL=static_cast<int>(L); }
    if(auto pTiles=std::dynamic_pointer_cast<CLayerTiles>(pLayer);pTiles && !pTiles->m_HasGame && !pTiles->m_HasFront && !pTiles->m_HasTele && !pTiles->m_HasSpeedup && !pTiles->m_HasSwitch && !pTiles->m_HasTune)
    {
     size_t Cells=static_cast<size_t>(pTiles->Width())*pTiles->Height();
     if(Cells>Largest){Largest=Cells;LargeG=static_cast<int>(G);LargeL=static_cast<int>(L);}
    }
   }
  perf_v1::Check(GameG>=0,"no game layer");
  Exercise(GameG,GameL,"game");
  if(LargeG>=0) Exercise(LargeG,LargeL,"largest_ordinary");
  std::printf("PERF_PASS baseline=v2\n");
  std::fflush(stdout);
  Client()->Quit();
 }
};
IEditor *CreateEditor() { return new CPerfEditor; }
