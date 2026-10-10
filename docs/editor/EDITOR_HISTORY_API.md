# Editor history: recording API reference

Use the map's **`CEditorDocumentHistory`** adapter in editor tools and UI:

```cpp
auto &History = Map()->m_DocumentHistory;
```

Here `Map()` belongs to the editor/component; static popup callbacks use `pEditor->Map()`. `document.Edit(...)` is a conceptual shorthand, not the current API name. These contracts describe the adapter in [document_history.h](../../src/game/editor/history/document_history.h) and [.cpp](../../src/game/editor/history/document_history.cpp), rather than the lower-level coordinator's different return types.

For the big picture see the [guide](EDITOR_HISTORY.md); for a complete worked example see the [property walkthrough](EDITOR_HISTORY_WALKTHROUGH.md).

## Choose a pattern

| Interaction | Pattern | Existing example |
| --- | --- | --- |
| Button/menu command | `Edit(owner, label, category, callback)` | `CEditor::AddGroup`, [quick_actions.cpp](../../src/game/editor/quick_actions.cpp) |
| Property slider or numeric input | `DoPropertiesWithState → BeginControl → Update → EndControl` | `PopupGroup`, [popups.cpp](../../src/game/editor/popups.cpp) |
| Canvas stroke/drag | `Begin → Update... → Complete` with `TrackPointer` | `DoSoundSource`, [editor.cpp](../../src/game/editor/editor.cpp) |
| Held-key repeats | `EditRepeated`; editor routes `KeyReleased` | [editor_server_settings.cpp](../../src/game/editor/editor_server_settings.cpp) |
| Document text | `DoDocumentEditBox`; editor routes input and `FinishDocumentText` | [editor_ui.cpp](../../src/game/editor/editor_ui.cpp) |
| Helper inside another edit | Mutate within the caller's callback, or `Update` with its owner | Never start a nested `Edit` |

An **owner** is a non-null, non-owning pointer key, usually a UI ID or tool address. Keep it stable for the whole interaction. It is not an object ID or previous value. A local variable works for a synchronous command; its address is unsuitable for a gesture that outlives its scope. Destroying a raw owner key does not itself notify the adapter: use the control/capture lifecycle or explicit cancellation.

`category` is `editor_history::ECategory::MAP`, `ENVELOPE` or `SETTINGS`. It labels an entry in the same map timeline; it does not select a separate history. A label describes the interaction, such as “Move sound source”.

## Commands and gestures

In these signatures, `fn` is a callback returning **`void`**. It changes live runtime values; `Update` does not capture a revision.

| Call | Contract and return value |
| --- | --- |
| `bool Edit(owner, label, category, fn)` | Completes any active edit, even for the same owner, then begins, updates and completes this command. `true` includes a no-op. Do not use it to join an edit. |
| `bool Begin(owner, label, category)` | Call before mutation. Rejoins the same owner without replacing its label/category; a different owner completes the previous edit first. Rejects uninitialized history, null owner, pending restoration or active publication. `true` permits updates; it does not add an entry. |
| `bool Update(owner, fn)` | Runs only for the matching active owner with no pending work. Nested `Update` with that owner is supported. `true` means the callback finished without pending cancellation, not that values changed. |
| `bool Complete(owner, reason)` | Captures, validates and commits once. `true` includes changed and unchanged results. A no-op preserves redo; a changed result removes the redo branch. |
| `bool Cancel(reason)` | Requests rollback of the active edit. `true` means queued, not restored. It does not traverse an older entry. If cancelling on behalf of a specific tool, first check `Owns(owner)`. |
| `bool EditRepeated(owner, label, category, heldKey, fn)` | Begins/rejoins and updates. A nonzero key leaves the edit open until matching `KeyReleased(key)`; zero completes immediately. `true` can mean the gesture is still active. |
| `void TrackPointer(owner, captureItem)` | Tracks the UI active-item identity. Losing it cancels at `FinishFrame`. Complete before clearing/replacing the capture item. |

Pass a reason such as `CEditorDocumentHistory::ECompletion::ACCEPT` or `POINTER_RELEASE` to `Complete`. Cancellation reasons include `ECancellation::ESCAPE`, `CAPTURE_LOST` and `OWNER_DESTROYED`. The enums are in [edit_coordinator.h](../../src/game/editor/history/edit_coordinator.h); reasons explain lifecycle events, not inverse operations.

Keep all mutations inside the accepted callback. A `false` result is not a no-op signal and does not always mean nothing happened: failed capture/commit or a caught callback exception can leave partial live changes until queued rollback runs. Stop the interaction and let the editor recover. Do not call completion, cancellation, navigation or a new-owner operation from inside `Update`; those operations are rejected while the coordinator is busy. If a callback detects invalid data, record that outcome and request `Cancel()` after the callback returns.

## Property controls

```cpp
if(History.BeginControl(pIds, "Edit property", State))
{
	History.Update(pIds, [&] {
		// Apply the reported property/value, if a property was reported.
	});
	History.EndControl(pIds, State);
}
```

This is the lifecycle skeleton; the [walkthrough](EDITOR_HISTORY_WALKTHROUGH.md#3-apply-the-value-inside-the-existing-edit) supplies real property cases. `BeginControl(owner, label, state, category = MAP)` returns `bool`; `EndControl(owner, state)` returns `void` and handles completion after the last value update. Its completion result is not returned to the caller.

| `EEditState` | `BeginControl` | `EndControl` |
| --- | --- | --- |
| `NONE` | Returns false; does nothing | Does nothing |
| `START`, `EDITING` | Begins/rejoins and permits an update | Leaves edit active |
| `END`, `ONE_GO` | Begins/rejoins and permits the final update | Completes with `ACCEPT` |
| `CANCELLED` | Requests cancellation for the matching owner; returns false | Does nothing |

States come from [ui.h](../../src/game/client/ui.h); begin/rejoin can still fail under the normal ownership/pending-work rules. `START` need not change a value, and an `EDITING` frame can be unchanged. `TouchControl(owner)` must run on **every frame that the owning control is rendered**, including `NONE` frames. `DoPropertiesWithState()` already does this using `pIds`; a custom control must do it itself. Missing presence causes cancellation at `FinishFrame`.

For strings bound directly to document buffers, use `DoDocumentEditBox()` with a persistent `CLineInput`, rebinding to the current object's buffer while rendering. It groups typing and accepts on Enter/valid blur or cancels on Escape/owner disappearance. Ordinary `DoEditBox()` does not record document edits. The [guide's text example](EDITOR_HISTORY.md#a-property-control-stroke-or-drag) shows the binding.

## Navigation and state

| Call | Meaning |
| --- | --- |
| `bool Undo()` | Queues cancellation if an edit is active; otherwise queues the previous revision. One call does not do both. |
| `bool Redo()` / `bool GoTo(index)` | Queue traversal; reject active edits. `index` is a position in the currently retained revision list, not a stable revision ID. Going to the current index can succeed without changing state. |
| `Ready()` / `Active()` | Coordinator exists / a draft exists. Neither means updates are currently allowed; `Active()` can remain true during pending cancellation. |
| `Owns(owner)` | Matching active owner with no pending work. Use this for tool-specific participation/cancellation. |
| `Pending()` / `Publishing()` | Restoration is queued / restoration is executing. `Publishing()` does not cover all callback/busy states. |
| `CanUndo()` / `CanRedo()` | Availability for UI commands. Undo includes an active draft; redo requires no active draft. Both exclude pending work. |

Navigation `true` means a request was accepted, not that live objects have changed. At the safe point, restoration can replace groups, layers and vector elements. Do not retain mutable pointers/references across it; reacquire objects from the current map, using stable document IDs when needed.

## Editor-owned lifecycle

Ordinary tools use the patterns above and leave these operations to editor integration:

- `CompleteActive(reason)` settles whichever owner is active, for example before a manual save. If none is active, it returns `!Pending()`; it is not an initialization test. Helpers use owner-specific `Complete` only at the interaction boundary.
- `StartFrame`, `FinishFrame` and `KeyReleased` maintain presence/capture/repeat lifetimes. The existing editor dispatch supplies these calls.
- `PublishAtSafePoint()` applies pending restoration after rendering/UI callbacks return. `false` can mean no work, same-current navigation or a restore failure. Failed navigation can be abandoned; failed cancellation/rollback remains pending and blocks new edits. Never force publication inside a tool callback.
- `OnModify()` on the map updates timing/presentation; it does not capture a revision. Keep normal notifications, but use an edit boundary to record changes.

`History()` exposes a const core pointer for inspection (null before initialization). Revision management, retention, saving and memory diagnostics are explained in the [main guide](EDITOR_HISTORY.md#retention-and-memory-figures); a new property normally does not need to call those APIs.
