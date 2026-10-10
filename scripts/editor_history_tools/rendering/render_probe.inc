// Included only in an isolated source copy. No benchmark trigger enters the client.
#include <base/str.h>
#include <engine/client.h>
#include <engine/client/graphics_threaded.h>
#include <engine/gfx/image_loader.h>
#include <engine/shared/config.h>
#include <chrono>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>
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

class CRenderProbeEditor final : public CEditor
{
	using CClock = std::chrono::steady_clock;
	bool m_Ran = false;
	int m_Owner = 0;
	int m_GameGroup = -1, m_GameLayer = -1;
	vec2 m_Center;
	CTile m_Original;
	static void Check(bool Ok, const char *pMessage)
	{
		if(!Ok)
		{
			std::fprintf(stderr, "RENDER_FAIL %s\n", pMessage);
			std::abort();
		}
	}
	void Memory(const char *pPhase)
	{
		PROCESS_MEMORY_COUNTERS_EX Memory{};
		Memory.cb = sizeof(Memory);
		Check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&Memory), sizeof(Memory)), "process memory");
		std::printf("RENDER_MEMORY phase=%s private=%zu resident=%zu peak_private=%zu peak_resident=%zu\n", pPhase, static_cast<size_t>(Memory.PrivateUsage), static_cast<size_t>(Memory.WorkingSetSize), static_cast<size_t>(Memory.PeakPagefileUsage), static_cast<size_t>(Memory.PeakWorkingSetSize));
	}
	void SetView(int Zoom, vec2 Center)
	{
		MapView()->Zoom()->SetValueInstant(Zoom);
		MapView()->UpdateZoom();
		MapView()->SetWorldOffset(Center);
		MapView()->SetEditorOffset(vec2(0, 0));
		Check(MapView()->GetWorldZoom() == Zoom / 100.0f, "effective zoom");
	}
	void Frame()
	{
		CEditor::OnUpdate();
		CEditor::OnRender();
		Graphics()->Swap();
		Graphics()->WaitForIdle();
	}
	void Capture(int Zoom)
	{
#if !defined(CONF_HEADLESS_CLIENT)
		SetView(Zoom, m_Center);
		Graphics()->Clear(0, 0, 0);
		Graphics()->ClipDisable();
		// Pure map pixels, independent of UI labels/cursors between versions.
		for(const auto &pGroup : Map()->m_vpGroups)
			if(pGroup->m_Visible)
				pGroup->Render(Map());
		char aName[64];
		str_format(aName, sizeof(aName), "scene-%d.png", Zoom);
		// Read the real framebuffer directly: normal screenshots require focus.
		CImageInfo Image;
		static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
		Check(Image.m_pData != nullptr, "native framebuffer");
		Check(CImageLoader::SavePng(Storage()->OpenFile(aName, IOFLAG_WRITE, IStorage::TYPE_SAVE), aName, Image), "native screenshot");
		Image.Free();
#endif
	}
	void Edit(bool Changed)
	{
		CTile Tile = m_Original;
		if(Changed)
			Tile.m_Index = Tile.m_Index == TILE_SOLID ? TILE_DEATH : TILE_SOLID;
#if defined(RENDER_PROBE_LEGACY)
		Map()->m_pGameLayer->SetTile(0, 0, Tile);
		auto pAction = std::make_shared<CEditorBrushDrawAction>(Map(), m_GameGroup);
		Check(!pAction->IsEmpty(), "edit action");
		Map()->m_EditorHistory.RecordAction(pAction);
#else
		Check(Map()->m_DocumentHistory.Edit(&m_Owner, "Render probe", editor_history::ECategory::MAP, [&] { Map()->m_pGameLayer->SetTile(0, 0, Tile); }), "edit commit");
#endif
	}
	void Undo()
	{
#if defined(RENDER_PROBE_LEGACY)
		Check(Map()->m_EditorHistory.Undo(), "undo");
#else
		Check(Map()->m_DocumentHistory.Undo() && Map()->m_DocumentHistory.PublishAtSafePoint(), "undo");
#endif
		Check(Map()->m_pGameLayer->GetTile(0, 0).m_Index == m_Original.m_Index, "undo value");
	}
	void Redo()
	{
#if defined(RENDER_PROBE_LEGACY)
		Check(Map()->m_EditorHistory.Redo(), "redo");
#else
		Check(Map()->m_DocumentHistory.Redo() && Map()->m_DocumentHistory.PublishAtSafePoint(), "redo");
#endif
		Check(Map()->m_pGameLayer->GetTile(0, 0).m_Index != m_Original.m_Index, "redo value");
	}
	template<typename F>
	void Measure(int Zoom, const char *pPhase, int Samples, F Function)
	{
		std::vector<double> vTimes;
		vTimes.reserve(Samples);
		for(int Index = -10; Index < Samples; ++Index)
		{
			const auto Start = CClock::now();
			Function(Index);
			const auto Ms = std::chrono::duration<double, std::milli>(CClock::now() - Start).count();
			if(Index >= 0)
				vTimes.push_back(Ms);
		}
		for(std::size_t Index = 0; Index < vTimes.size(); ++Index)
			std::printf("RENDER_SAMPLE zoom=%d phase=%s sample=%zu ms=%.6f\n", Zoom, pPhase, Index, vTimes[Index]);
		std::fflush(stdout);
	}
	void MeasureEdits(int Zoom, int Samples)
	{
		std::vector<std::array<double, 4>> vTimes;
		vTimes.reserve(Samples);
		for(int Index = -10; Index < Samples; ++Index)
		{
			const auto Time = [&](auto Function) {
				const auto Start = CClock::now();
				Function();
				Frame();
				return std::chrono::duration<double, std::milli>(CClock::now() - Start).count();
			};
			const std::array<double, 4> Times{Time([&] { Edit(true); }), Time([&] { Undo(); }), Time([&] { Redo(); }), Time([&] { Undo(); })};
			if(Index >= 0)
				vTimes.push_back(Times);
#if defined(RENDER_PROBE_LEGACY)
			Check(Map()->m_EditorHistory.m_vpUndoActions.size() + Map()->m_EditorHistory.m_vpRedoActions.size() == 50, "retained history");
#else
			Check(Map()->m_DocumentHistory.History()->Revisions().size() == 51, "retained history");
#endif
		}
		const char *apPhases[] = {"edit_frame", "undo_frame", "redo_frame", "reset_undo_frame"};
		for(std::size_t Index = 0; Index < vTimes.size(); ++Index)
			for(int Phase = 0; Phase < 4; ++Phase)
				std::printf("RENDER_SAMPLE zoom=%d phase=%s sample=%zu ms=%.6f\n", Zoom, apPhases[Phase], Index, vTimes[Index][Phase]);
		std::fflush(stdout);
	}

public:
	void OnUpdate() override
	{
		CEditor::OnUpdate();
		if(m_Ran || str_comp(Map()->m_aFilename, "maps/fixture.map") != 0)
			return;
		m_Ran = true;
		CloseMap(0, false);
		m_Dialog = DIALOG_NONE;
		g_Config.m_EdAutosaveInterval = 0;
		g_Config.m_EdZoomTarget = 0;
		Map()->m_EnvelopeEvaluator.m_Animate = false;
		Map()->m_EnvelopeEvaluator.m_AnimateTime = 0;
		for(size_t G = 0; G < Map()->m_vpGroups.size(); ++G)
			for(size_t L = 0; L < Map()->m_vpGroups[G]->m_vpLayers.size(); ++L)
				if(Map()->m_vpGroups[G]->m_vpLayers[L] == Map()->m_pGameLayer)
				{
					m_GameGroup = static_cast<int>(G);
					m_GameLayer = static_cast<int>(L);
				}
		Check(m_GameGroup >= 0, "game layer");
		Map()->SelectLayer(m_GameLayer, m_GameGroup);
#if defined(RENDER_PROBE_LEGACY)
		const int Width = Map()->m_pGameLayer->m_Width, Height = Map()->m_pGameLayer->m_Height;
#else
		const int Width = Map()->m_pGameLayer->Width(), Height = Map()->m_pGameLayer->Height();
#endif
		m_Center = vec2(Width * 16.0f, Height * 16.0f);
		bool Found = false;
		for(int Y = 0; Y < Height && !Found; ++Y)
			for(int X = 0; X < Width; ++X)
				if(Map()->m_pGameLayer->GetTile(X, Y).m_Index == ENTITY_SPAWN + ENTITY_OFFSET)
				{
					m_Center = vec2(X * 32.0f + 16, Y * 32.0f + 16);
					Found = true;
					break;
				}
		std::printf("RENDER_CONTEXT width=%d height=%d center_x=%.1f center_y=%.1f texture_arrays=%d\n", Graphics()->ScreenWidth(), Graphics()->ScreenHeight(), m_Center.x, m_Center.y, Graphics()->HasTextureArraysSupport());
		std::printf("RENDER_DRIVER vendor=%s renderer=%s version=%s\n", Graphics()->GetVendorString(), Graphics()->GetRendererString(), Graphics()->GetVersionString());
		Memory("loaded");
		Capture(100);
		Capture(900);
		if(std::getenv("RENDER_PROBE_VISUAL_ONLY"))
		{
			std::puts("RENDER_PASS visual");
			std::fflush(stdout);
			Client()->Quit();
			return;
		}
		g_Config.m_ClEditorMaxHistory = 50;
#if defined(RENDER_PROBE_LEGACY)
		Map()->m_EditorHistory.Clear();
#else
		Map()->m_DocumentHistory.ApplyPreferences();
		Check(Map()->m_DocumentHistory.Clear(), "clear");
#endif
		m_Original = Map()->m_pGameLayer->GetTile(0, 0);
		for(int Entry = 0; Entry < 50; ++Entry)
			Edit(Entry % 2 == 0);
		const int Samples = std::getenv("RENDER_PROBE_SAMPLES") ? std::atoi(std::getenv("RENDER_PROBE_SAMPLES")) : 50;
		Check(Samples > 0, "sample count");
		for(int Zoom : {200, 900, 2000})
		{
			SetView(Zoom, m_Center);
			const auto Rect = Map()->m_pGameGroup->Mapping();
			std::printf("RENDER_VIEW zoom=%d world_zoom=%.1f left=%.1f top=%.1f right=%.1f bottom=%.1f\n", Zoom, MapView()->GetWorldZoom(), Rect.m_TopLeft.x, Rect.m_TopLeft.y, Rect.m_BottomRight.x, Rect.m_BottomRight.y);
			Measure(Zoom, "idle", Samples, [&](int) { Frame(); });
			Measure(Zoom, "pan", Samples, [&](int Index) { MapView()->SetWorldOffset(m_Center + vec2((Index % 7) * 64.0f, (Index % 5) * 32.0f)); Frame(); });
			SetView(Zoom, m_Center);
			MeasureEdits(Zoom, Samples);
			Memory("zoom_complete");
		}
		if(std::getenv("RENDER_PROBE_MEMORY_DIAGNOSTIC"))
		{
#if !defined(RENDER_PROBE_LEGACY)
			editor_history::CStorageUsage Caches;
			Map()->AccountRuntimeCaches(Caches);
			std::printf("RENDER_OWNED retained=%zu caches=%zu\n", Map()->m_DocumentHistory.History()->RetainedBytes(), Caches.Bytes());
#endif
			Memory("before_readback");
#if !defined(CONF_HEADLESS_CLIENT)
			CImageInfo Image;
			static_cast<CGraphics_Threaded *>(Graphics())->ReadFramebuffer(Image);
			Check(Image.m_pData != nullptr, "terminal framebuffer");
			Image.Free();
#endif
			Memory("after_readback");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			Memory("settled");
		}
		std::puts("RENDER_PASS full");
		std::fflush(stdout);
		Client()->Quit();
	}
};

IEditor *CreateEditor() { return new CRenderProbeEditor; }
