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
