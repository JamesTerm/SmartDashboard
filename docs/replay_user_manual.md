# Replay Features User Manual

This guide is the training reference for SmartDashboard replay workflows.

## What replay is for

Replay mode lets you inspect telemetry after a run or match using one shared timeline cursor across all widgets.

Use replay to answer questions like:

- What happened right before a brownout or disconnect?
- Did command and telemetry values diverge before an incident?
- Did connection-state transitions line up with control issues?

## Prerequisites

- A recorded replay session file (`.json`).
- Format note: replay files are newline-delimited JSON events (JSONL-style content) stored with a `.json` extension.
- A dashboard layout with widgets mapped to the keys you want to analyze.

## Why there are two replay-related JSON formats

You may see two valid telemetry file shapes in this project:

- **Replay event stream format** (produced by SmartDashboard recording):
  - one JSON event per line
  - event-centric fields like `eventKind`, `timestampUs`, `key`, `value`
  - optimized for timeline replay controls (`play`, `seek`, markers, connection-state events)
- **Capture session format** (produced by `SmartDashboardCaptureCli`):
  - one top-level JSON object with `metadata` and `signals[]`
  - each signal has a `samples[]` series with `t_us`
  - optimized for automated harness analysis, A/B comparisons, and metadata tagging

Both are now accepted by Replay transport. They exist because they were designed for different workflows (operator replay vs. automated test-harness analysis).

## Opening replay mode

1. Open `Connection` menu.
2. Select `Use Replay transport`.
3. Choose `Replay: Open session file...` and pick your file.

Notes:

- If a replay file path is already saved, replay can auto-start when replay transport is selected.
- In replay mode, status text shows `Replay`; file context is shown in the window title.

## Control reference

Replay controls live in the `Replay Controls` panel, which is docked at the bottom by default.

- `Play/Pause` (`▶` / `⏸`): toggles replay movement.
- `|◀`: rewind to start and pause.
- `⏮` / `⏭`: jump to previous/next marker.
- `B+`: add a user bookmark at current cursor time.
- `Bx`: clear all user bookmarks for this session.
- Speed selector: `0.25x`, `0.5x`, `1x`, `2x`.
- Status bar replay readouts:
  - `t=...` shows current replay cursor time
  - `window=...` shows current visible timeline window span

## Timeline reference

The timeline lives in the `Replay Timeline` panel.

Timeline interactions:

- Left-click or left-drag: scrub cursor.
- Mouse wheel: zoom in/out.
- Right-drag: pan visible window.

Timeline readability features:

- Adaptive tick marks and time labels scale with zoom.
- Overview strip shows full replay duration and highlighted current zoom window.

Note:

- Cursor/window readouts are displayed on the status bar to keep timeline panel height compact.

Marker rendering:

- Marker lines are shown in both overview and detailed track.
- Marker kinds include connect, disconnect, stale, anomaly, and generic markers/bookmarks.

## Marker list panel

The `Replay Markers` dock provides click-to-seek and fast scanning.

- Each row shows timestamp, marker kind, and label.
- Click or activate a row to seek to that timestamp.
- Selection auto-follows replay cursor to nearest marker at-or-before current time.
- Summary line shows visible-window counts:
  - total markers
  - anomaly markers
  - bookmarks
  - visible window span

## Dockable replay workspace

Replay analysis panels can be arranged per operator preference.

- Panels:
  - `Replay Controls`
  - `Replay Timeline`
  - `Replay Markers`
- Show/hide from `View` menu.
- Right-click panel title bars for `Float`, `Dock Left`, `Dock Right`, `Dock Bottom`.
- `Reset Replay Layout` returns controls + timeline to default bottom side-by-side layout.
- Panel visibility is persisted between sessions.

## Keyboard navigation

In replay mode:

- `Left` / `Right`: step cursor by 100 ms.
- `Shift+Left` / `Shift+Right`: step cursor by 1 s.

Use this for frame-like inspection when zoomed into short time windows.

## Analysis helpers

### Bookmarks

- Use `B+` at important moments (driver note, impact event, unusual sensor behavior).
- Bookmark creation uses a short dedupe window to avoid near-duplicate spam.
- Use `Bx` to reset bookmarks and run a fresh analysis pass.

### Anomaly markers

Replay can surface anomaly markers from:

- explicit replay event flags (`anomaly=true`)
- inferred low-voltage/brownout-style numeric conditions on relevant keys

These markers help you jump directly to likely incident windows.

## Recommended training walkthrough (15 minutes)

1. Load a known replay and press play at `1x`.
2. Pause near an incident, then use wheel zoom to narrow to a short window.
3. Use `Left/Right` and `Shift+Left/Right` to step precisely.
4. Add 2-3 bookmarks (`B+`) at key moments.
5. Use `⏮` / `⏭` to move across system/anomaly markers.
6. Use marker list click-to-seek to revisit bookmarks and anomalies.
7. Verify summary counts change as you zoom/pan different windows.
8. Clear bookmarks (`Bx`) and repeat once to reinforce workflow.

## Practical incident workflow

1. Start broad at full-run scale.
2. Jump between markers to locate likely problem period quickly.
3. Zoom into 2-10 second window.
4. Step with keyboard for sub-second inspection.
5. Add bookmarks to build a timestamped narrative.
6. Cross-check all relevant widgets at each bookmark timestamp.

## Interpreting connection indicators

For capture/replay diagnostics:

- `Connection observed during capture: true` means there was a healthy connected interval.
- `Connection state at capture end: Stale` can be normal near session tail.
- `Post-stop connection state: Disconnected` is expected after shutdown.

## Current limitations

- User bookmarks are session-local (not yet persisted back to replay files).
- Auto-anomaly detection is heuristic; treat markers as investigation hints, not final verdicts.

See `docs/replay_parity_roadmap.md` for planned parity follow-ups.
