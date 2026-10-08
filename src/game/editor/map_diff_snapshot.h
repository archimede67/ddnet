#ifndef GAME_EDITOR_MAP_DIFF_SNAPSHOT_H
#define GAME_EDITOR_MAP_DIFF_SNAPSHOT_H
#include "map_diff.h"
class CEditorMap;
namespace MapDiff
{
	// Reads authoritative in-memory fields only. Never saves, prepares tiles for
	// saving, evaluates animation, changes selection, or touches editor histories.
	// A false return discards the partial snapshot after cancellation.
	bool Capture(const CEditorMap &Map, CSnapshot &Snapshot, const FCancel &Cancel = {});
}
#endif
