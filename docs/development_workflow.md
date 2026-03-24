# Development Workflow

This document describes the intended incremental development loop for this repository.

- Edit this file if the team changes how work is planned, validated, or reviewed.

## Why this exists

We want students to see a repeatable engineering process, not "prompt and pray" AI usage.

## Iteration loop

1. **Define a small objective**
   - Write clear acceptance criteria before implementation.
   - Example: "Editable mode allows move/resize, but never sends value commands."

2. **Implement the smallest useful slice**
   - Prefer focused changes over large rewrites.
   - Keep architecture boundaries explicit (transport, model, UI).

3. **Validate immediately**
   - Run automated tests for affected components.
   - Run targeted manual checks for UI interactions.

4. **Record rationale and outcomes**
   - Capture decisions, trade-offs, and constraints in notes/docs.
   - Keep design and requirements docs synchronized.

5. **Commit checkpoint**
   - Use concise commit messages that explain intent.
   - Keep checkpoints small enough to review quickly.

## Regression-driven debugging note

When a transport or integration bug appears, do not jump straight to the biggest end-to-end test.

Use an incremental ladder instead:

1. **Prove the simplest observable works**
   - Example: launch the dashboard and confirm a single numeric key appears.

2. **Match the real ownership model**
   - A short-lived helper process is not the same as a running dashboard session.
   - If students normally create or hold a value from the dashboard UI, the test should mimic that path as closely as possible.

3. **Reduce the number of moving parts**
   - Separate legacy numeric controls from chooser-based controls.
   - Rename overlapping keys when needed so one experiment cannot mask another.

4. **Add strategic checkpoints before broad refactors**
   - Small commits make regression testing practical.
   - If a behavior was known-good two commits ago, that history becomes a fast debugging tool instead of a guess.

5. **Promote the next test only after the previous one is trusted**
   - Example progression:
     - direct stream unit test
     - dashboard launch + single-key populate check
     - dashboard + DriverStation paired enable check
     - repeated stress loop

This pattern matters for both hand-written debugging and AI-assisted work: the faster we isolate one truth at a time, the less likely we are to chase a symptom caused by a test harness mistake.

See also:

- `docs/learning/regression_debugging.md`
- `docs/journal/2026-03-17-direct-debugging-journey.md`

## Review expectations

- Behavior changes include proof of validation (tests or reproducible manual checks).
- Architecture changes include explanation of trade-offs.
- Documentation changes keep source-of-truth files easy to find.

## Current repository cadence

- Frequent checkpoints and milestone updates in `docs/project_history.md`
- Lean next-session handoff context in `Agent_Session_Notes.md`
- Design/architecture details in `design/SmartDashboard_Design.md`
- Human requirements in `docs/requirements.md`
- Testing commands and scope in `docs/testing.md`
