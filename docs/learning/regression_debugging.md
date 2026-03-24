# Regression Debugging

This folder is for reusable lessons that help students and contributors debug dashboard behavior without guessing.

## Recommended ladder

1. Prove a single publish/observe case.
2. Verify the running dashboard owns the state, not just a helper tool.
3. Verify the robot or consumer reads the same value at the moment of use.
4. Only then expand to reconnect loops or stress harnesses.

## Key lesson from Direct-mode debugging

- Big end-to-end failures often hide two different bugs:
  - a real product bug
  - a harness or probe bug

You want to separate those early.

## Commit strategy

Small strategic commits make regression testing faster because they provide checkpoints that can be compared or restored without throwing away current work.
