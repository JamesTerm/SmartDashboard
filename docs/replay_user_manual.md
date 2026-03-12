# Replay Features User Manual

This guide explains how to use SmartDashboard replay features for post-run troubleshooting.

## What replay is for

Replay mode lets you inspect telemetry after a run or match using a shared timeline cursor across all widgets.

Use replay to answer questions like:

- What happened right before a brownout?
- Did command and telemetry values diverge before an incident?
- Did connection state changes line up with control issues?

## Prerequisites

- A recorded replay session file (JSONL capture/replay file).
- Dashboard layout with widgets already mapped to relevant telemetry keys.

## Opening replay mode

1. Open `Connection` menu.
2. Select `Use Replay transport`.
3. Choose `Replay: Open session file...` and pick your file.

Notes:

- If a previous replay file is persisted, switching to replay may auto-start with that file.
- In replay mode, status text shows `Replay` and file context appears in window title.

## Replay controls

- `Play/Pause` button: toggles replay movement.
- `|◀` rewind button: returns cursor to start and pauses.
- Speed selector: choose replay speed (`0.25x`, `0.5x`, `1x`, `2x`).

## Timeline interactions

The timeline is the horizontal bar in the status area.

- Left-click or left-drag: scrub cursor.
- Mouse wheel: zoom in/out on timeline.
- Right-drag: pan visible timeline window.

Current implementation note:

- Timeline visuals are intentionally minimal in this phase (track + cursor), but interactions are fully functional.

## Typical troubleshooting workflow

1. Start broad: view full run and scrub near incident time.
2. Zoom in: use wheel to narrow to a short window.
3. Fine inspect: left-drag cursor through critical moments.
4. Correlate: verify all relevant widgets at same timestamp.
5. Rewind and replay at slower speed for confirmation.

## Interpreting connection indicators

For capture CLI run summaries and replay diagnostics:

- `Connection observed during capture: true` indicates healthy connected period.
- `Connection state at capture end: Stale` can be normal near session tail.
- `Post-stop connection state: Disconnected` is expected after shutdown.

## Known limitations (current phase)

- No marker glyph rendering on timeline yet.
- No marker list panel yet.
- No annotation/bookmark workflow yet.

See `docs/replay_parity_roadmap.md` for planned iterations.
