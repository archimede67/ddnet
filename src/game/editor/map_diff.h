#ifndef GAME_EDITOR_MAP_DIFF_H
#define GAME_EDITOR_MAP_DIFF_H

// Owned, canonical document values. This module has no dependency on the editor,
// graphics, storage, or the map file writer. Coordinates and object order matter;
// editor state and resource table indices do not.
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MapDiff
{
	using FCancel = std::function<bool()>;
	struct CProperty
	{
		std::string m_Name;
		std::string m_Value;
		bool operator==(const CProperty &Other) const { return m_Name == Other.m_Name && m_Value == Other.m_Value; }
	};
	using TProperties = std::vector<CProperty>;

	enum class ELayerKind
	{
		TILES,
		GAME,
		FRONT,
		TELE,
		SPEEDUP,
		SWITCH,
		TUNE,
		QUADS,
		SOUNDS,
		UNKNOWN,
	};
	struct CTile
	{
		uint8_t m_Index = 0;
		uint8_t m_Flags = 0;
		uint8_t m_Number = 0;
		uint8_t m_Delay = 0;
		uint8_t m_Force = 0;
		uint8_t m_MaxSpeed = 0;
		int16_t m_Angle = 0;
		bool operator==(const CTile &Other) const;
	};
	struct CCell
	{
		int m_X = 0;
		int m_Y = 0;
		CTile m_Tile;
	};
	enum class EResourceKind
	{
		IMAGE,
		SOUND,
		ENVELOPE
	};
	struct CReference
	{
		EResourceKind m_Kind;
		int m_Index;
	};
	struct CObject
	{
		TProperties m_vProperties;
		// World-space bounds are presentation data, not compared a second time.
		std::array<float, 4> m_aBounds{};
		// Source indices are presentation-only and excluded from equality/hashing.
		std::vector<CReference> m_vReferences;
	};
	struct CLayer
	{
		std::string m_Name;
		ELayerKind m_Kind = ELayerKind::TILES;
		TProperties m_vProperties;
		int m_Width = 0;
		int m_Height = 0;
		// Nonzero cells, sorted by (y, x), independent of the layer's row stride.
		std::vector<CCell> m_vCells;
		std::vector<CObject> m_vObjects;
		std::vector<std::string> m_vIssues;
		std::vector<CReference> m_vReferences;
	};
	struct CGroup
	{
		std::string m_Name;
		bool m_Game = false;
		TProperties m_vProperties;
		std::vector<CLayer> m_vLayers;
	};
	struct CResource
	{
		std::string m_Name;
		TProperties m_vProperties;
		std::vector<uint8_t> m_vData;
		bool m_Missing = false;
		// Cached once by the snapshot adapter; references use this semantic key.
		std::string m_ContentKey;
	};
	struct CSnapshot
	{
		TProperties m_vInfo;
		std::vector<std::string> m_vSettings;
		std::vector<CGroup> m_vGroups;
		std::vector<CResource> m_vImages;
		std::vector<CResource> m_vSounds;
		std::vector<CResource> m_vEnvelopes;
	};
	struct CPropertyChange
	{
		std::string m_Name;
		std::string m_Before;
		std::string m_After;
	};
	struct CTileChange
	{
		int m_X;
		int m_Y;
		CTile m_Before;
		CTile m_After;
	};
	struct CObjectChange
	{
		int m_Before = -1;
		int m_After = -1;
		bool m_Ambiguous = false;
		bool m_Reordered = false;
	};
	enum class EEntryKind
	{
		INFO,
		SETTINGS,
		IMAGE,
		SOUND,
		ENVELOPE,
		GROUP,
		LAYER,
	};
	struct CEntry
	{
		EEntryKind m_Kind = EEntryKind::INFO;
		std::string m_Name;
		int m_Parent = -1;
		int m_BeforeGroup = -1;
		int m_AfterGroup = -1;
		int m_Before = -1;
		int m_After = -1;
		bool m_Ambiguous = false;
		bool m_Reordered = false;
		std::vector<CPropertyChange> m_vProperties;
		std::vector<CTileChange> m_vTiles;
		std::vector<CObjectChange> m_vObjects;
		std::vector<std::string> m_vIssues;
		bool Changed() const;
		size_t ChangeCount() const;
		const char *Status() const;
	};
	struct CResult
	{
		std::vector<CEntry> m_vEntries;
		bool m_Cancelled = false;
		size_t ChangeCount() const;
	};

	// Cancellation discards the partial result. No output ever claims a partial
	// comparison is an equal map. Callers may execute this with owned snapshots on a worker.
	CResult Compare(const CSnapshot &Before, const CSnapshot &After, const FCancel &Cancel = {});
	std::vector<CPropertyChange> CompareProperties(const TProperties &Before, const TProperties &After);
	std::string TileDescription(const CTile &Tile, ELayerKind Kind);
	const char *LayerKindName(ELayerKind Kind);
	// Canonical field-wise fingerprint, never a hash of struct padding.
	std::string ResourceContentKey(const CResource &Resource);
}
#endif
