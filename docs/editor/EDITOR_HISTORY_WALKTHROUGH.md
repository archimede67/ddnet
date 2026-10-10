# Walkthrough: add an undoable group property

This exercise adds **Practice value**, an integer on a non-game group. You will be able to drag it, type a value, undo and redo. It is deliberately authoring-only: saving does not persist it, and reopening the map resets it to zero. The normal client does not contain this practice property; the snippets below describe the changes to try in a development checkout.

Read the [overview](EDITOR_HISTORY.md) for the architecture, or keep the [recording API reference](EDITOR_HISTORY_API.md) open while following along.

## 1. Put the value in the document

In [document_values.h](../../src/game/editor/mapitems/document_values.h), add this initialized field to `CLayerGroupValues`, before its defaulted `operator==`:

```cpp
int m_ExtensionExerciseValue = 0;
```

The name matches the repository's existing extension exercise. Leave `operator== = default` in place. `CLayerGroup` inherits these values, so the live group exposes the field immediately.

No capture or restoration mapping is needed. [CaptureDocument](../../src/game/editor/mapitems/document_graph.cpp) already copies the entire `CLayerGroupValues` base into a group record; [RestoreDocumentAtSafePoint](../../src/game/editor/mapitems/document_restore.cpp) assigns that base back to a prepared runtime group. Default equality includes the new integer, so a change is retained and a no-op is detected. The field adds no separately allocated storage to account for.

## 2. Add a row to the existing group panel

In [mapitems.h](../../src/game/editor/mapitems.h), insert `PRACTICE_VALUE` immediately before `EGroupProp::NUM_PROPS`:

```cpp
	CLIP_H,
	PRACTICE_VALUE,
	NUM_PROPS,
```

In `CEditor::PopupGroup()` in [popups.cpp](../../src/game/editor/popups.cpp), add a row immediately after “Clip H” and before `{nullptr}` in `aProps`:

```cpp
{"Practice value", pEditor->Map()->SelectedGroup()->m_ExtensionExerciseValue,
	PROPTYPE_INT, 0, 1000},
```

The enum and property-array order must match: the returned property is the row index. The existing `s_aIds[(int)EGroupProp::NUM_PROPS]` grows automatically. The game-group panel terminates its property list before these rows, so this exercise appears only for ordinary groups.

The popup has a fixed height. At its `DoPopupMenu(..., 145, 256, this, PopupGroup)` call in [editor.cpp](../../src/game/editor/editor.cpp), increase `256` to `269` for the extra 13-unit property row. This is normal UI layout work, independent of history.

## 3. Apply the value inside the existing edit

Still in `PopupGroup()`, find the `Update(s_aIds, ...)` callback. Inside its non-game-group block, append this case after the `CLIP_H` case:

```cpp
else if(Prop == EGroupProp::PRACTICE_VALUE)
{
	pEditor->Map()->SelectedGroup()->m_ExtensionExerciseValue = NewVal;
}
```

Keep the existing lifecycle around these cases. Its structure is:

```cpp
auto [State, Prop] = pEditor->DoPropertiesWithState<EGroupProp>(
	&View, aProps, s_aIds, &NewVal);
// Existing OnModify notification stays here.
auto &History = pEditor->Map()->m_DocumentHistory;
if(History.BeginControl(s_aIds, "Edit group", State))
{
	History.Update(s_aIds, [&] {
		// Existing property cases, including PRACTICE_VALUE.
	});
	History.EndControl(s_aIds, State);
}
```

This is an illustration of the existing wrapper, not a second block to add. `DoPropertiesWithState()` handles dragging, numeric text entry and owner presence. `BeginControl()` permits the live update; `EndControl()` completes on `END` or `ONE_GO`. Do not add `Edit()` inside the property case or complete on each frame. The entire drag should produce one “Edit group” entry.

During a drag from 0 through 1, 2 and 3 to 4, only the live field changes on each update. Releasing captures a revision containing 4. Undo requests the previous revision; at the editor's safe point the restored group's field becomes 0. Redo restores 4. Resolve the group again from the current map after restoration; an old pointer may refer to a retired group.

## 4. Check the result

Build the normal client using the [project build instructions](../../README.md). Open a scratch map with an ordinary **non-game group** (create one with “Add group” if needed), and right-click its group header to open the properties. Open the **History** view to count entries. Reopen the group popup after cancellation closes it. Check:

| Action | Expected result |
| --- | --- |
| Drag from 0 to 100 and release | One new history entry; undo returns to 0, redo to 100 |
| Right-click the number, type 250 and accept | One entry for the accepted value |
| Type `-` into the number and press Enter | Invalid text stays in editing; no completed entry; Escape cancels |
| Type a valid number and click outside the input | Valid blur accepts one entry |
| Change the focused numeric text, then press Ctrl+Z | Local text undo; document history does not traverse |
| Start another edit and cancel with Escape | Previous value restored; no entry for the cancelled edit |
| Undo, then accept an unchanged value | No new entry; redo remains available |
| Undo, then complete a different value | A new branch; the former redo is removed |
| Save and reopen this practice map | Value becomes 0, because this field is not exported |

For a dirty-state check, first save the scratch map and confirm it is clean; a newly created unsaved map has no saved marker. Change only Practice value. While editing, the editor reports dirty conservatively; after completion it returns to clean while retaining the undo entry, because this field leaves the persisted-content key unchanged.

For your **modified checkout**, add this test alongside the other `TEST_F(CEditorHistoryRuntime, ...)` cases in [src/test/editor_runtime/history.cpp](../../src/test/editor_runtime/history.cpp), inside the existing anonymous namespace and before `class CTestEditor final`. It uses the fixture's initial baseline:

```cpp
TEST_F(CEditorHistoryRuntime, PracticeGroupProperty)
{
	ASSERT_FALSE(Map().m_vpGroups.empty());
	int ToolOwner = 0; // Lives for the entire synchronous test interaction.
	ASSERT_TRUE(Timeline().Edit(&ToolOwner, "New group authoring property",
		editor_history::ECategory::MAP, [&] {
			Map().m_vpGroups.front()->m_ExtensionExerciseValue = 314;
		}));
	ASSERT_TRUE(Timeline().Undo());
	ASSERT_TRUE(Timeline().PublishAtSafePoint());
	EXPECT_EQ(Map().m_vpGroups.front()->m_ExtensionExerciseValue, 0);
	ASSERT_TRUE(Timeline().Redo());
	ASSERT_TRUE(Timeline().PublishAtSafePoint());
	EXPECT_EQ(Map().m_vpGroups.front()->m_ExtensionExerciseValue, 314);
}
```

Tests can explicitly publish after their callbacks return; production tools leave publication to the editor frame. Configure a separate headless test build with GTest available, then run the registered target:

```sh
cmake -S . -B build-property-tests -DHEADLESS_CLIENT=ON -DCLIENT=ON -DSERVER=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-property-tests --target run_editor_tests --config Debug
```

The target builds the runner and runs the runtime tests on bundled fixtures. For fuller coverage, the [complete extension exercise](../../scripts/editor_history_tools/extension_test.inc) also checks 200-update cancellation and no-op behavior. `ActualNumericDraftAcceptBlurEscapeAndLocalUndo` in the runtime test file demonstrates control-input testing.

Alternatively, to run the packaged exercise without modifying your checkout, start from an **unmodified source tree** and prepare an isolated copy:

```sh
python scripts/editor_history_tools/prepare.py --source . --output build-property-source --mode extension
cmake -S build-property-source -B build-property-exercise-tests -DHEADLESS_CLIENT=ON -DCLIENT=ON -DSERVER=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-property-exercise-tests --target editor-testrunner --config Debug
python scripts/editor_history_tools/run.py --source build-property-source --client build-property-exercise-tests/editor-testrunner --output property-results
```

For either route, use the project's normal compiler/dependency setup and provide GTest (or configure with `-DDOWNLOAD_GTEST=ON`). In the last command, Windows needs the `.exe` suffix; a multi-configuration generator may put the client in `Debug/`. The prepared-source and result directories must be new. The preparer inserts the field and automated test itself; do not run it on a checkout where you already added the field. It does **not** add the UI row from steps 2–3, so the automated exercise does not replace the manual control checks. See the [tool instructions](../../scripts/editor_history_tools/README.md) for configuration details.

## 5. Make a real property persistent, when needed

A production property that users expect to keep after reopening needs file-format integration as well as history. Use an existing group property such as `m_ParallaxX` to trace the path:

- Define compatible native storage/versioning in [game/mapitems.h](../../src/game/mapitems.h), including a default for maps written before the field existed. Do not blindly append bytes to an existing item layout.
- Import it into the authoritative value in [map_io.cpp](../../src/game/editor/mapitems/map_io.cpp).
- Export it in `ExportEditorDocument()` in [document_export.cpp](../../src/game/editor/mapitems/document_export.cpp). The persisted fingerprint uses that projection too; do not add a separate dirty-state field list.
- Test an older map's default, save/reload preservation, and dirty → save → clean → edit → undo-to-saved transitions. Add validation and derived-cache updates if the property's meaning requires them.

For this exercise, the history-specific work was choosing the value record and using the panel's existing edit boundary. A new object kind, reference, resource or heap collection needs the additional integrations in the [extension checklist](EDITOR_HISTORY.md#extending-the-editor).
