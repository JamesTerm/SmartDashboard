# 2026-03-17 - Direct debugging journey with Robot_Simulation

## Summary

This session started as a chooser/debugging problem and ended as a cleaner lesson in systems debugging.

The final result was not just a code fix. It was a better method:

- keep a numeric baseline
- isolate chooser experiments
- verify the helper tool behaves like the real dashboard session
- commit at useful checkpoints so regression testing stays practical.

## Important ah-ha moments

### 1. The probe was part of the experiment

The first helper wrote values in a short burst and then exited. That did not behave like a real dashboard session that stays alive and owns operator state.

Once the probe published over a short stream window with repeated flushes, the simplest populate test started behaving like the known-good stream unit test.

### 2. Shared-state reads can lie by looking successful

On the robot side, a missing Direct numeric value could still look like a successful read of `0`. That made `TestMove` appear present but collapsed the motion command to zero distance.

Adding non-destructive `TryGet*` APIs turned that hidden failure into a readable distinction between:

- value present
- value absent

### 3. One control path should not mask another

Using the same selection name for chooser and numeric experiments made it harder to understand what the dashboard was actually exercising.

Separating chooser state under `Test/Auton_Selection/AutoChooser` kept the numeric `AutonTest` baseline understandable.

### 4. Layout persistence should not restore stale live values

Saving widget layout and saving live control values are not the same problem. For teaching and debugging, startup should restore layout/config without overwriting live robot state.

## Why this matters for students

This is a useful example of how modern debugging should work whether code is written by hand or with AI assistance:

- start small
- control variables tightly
- preserve a baseline
- log the exact handoff points
- keep commits small enough to compare behavior over time.
