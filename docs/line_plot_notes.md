# Line Plot Notes

This note captures line-plot design decisions and tradeoffs in student-friendly language, with enough depth for mentors reviewing architecture choices.

## What problem were we solving?

- At high update rates (for example 16 ms/sample with a 250-sample buffer), the line trace was mostly fine, but x-axis number lines and gridlines could look jittery.
- The jitter came from slight span changes that repeatedly re-picked x tick spacing.

## Core concepts

- **Render cadence decoupling**: sample ingest and paint cadence are separate. The widget repaints at a fixed timer rate instead of repainting on every sample.
- **Sample-anchored viewport**: the visible x-range is anchored to retained samples, not wall-clock progression during gaps.
  - left edge = oldest retained sample
  - right edge = newest retained sample
  - with buffer full (N samples), the left edge is always the Nth sample back
- **EMA sample-period estimate**: each new sample updates a smoothed period estimate. This helps keep sample spacing stable when arrival times jitter.
- **Absolute tick anchoring**: x ticks are generated from a global grid using `floor(min/step) * step`, then only visible ticks are rendered.
- **Tick hysteresis**: once a tick spacing is chosen, small span drift does not immediately switch to another spacing bucket.

## Why sample-anchored x can feel better

- For dashboards, readability is usually more important than perfect timestamp geometry.
- If you directly map wall-clock timestamp gaps to x-pixels, pauses or bursty transport can cause visual jumps.
- Sample-anchored x keeps motion smooth and predictable while preserving meaningful time labels.

## Tradeoff triangle

You typically balance three goals:

- perfect timestamp accuracy
- nice rounded ticks
- stable visuals

Most telemetry dashboards prioritize the last two.

## Current behavior summary

- New samples are appended with x increasing by smoothed sample period.
- Buffer is capped to configured sample count.
- X range is computed from oldest/newest retained sample.
- X tick spacing uses nice 1/2/5 steps with hysteresis to reduce flip-flop.

## Useful mental model

Think of this as oscilloscope-like display behavior:

- fixed sample window
- newest data at right edge
- stable scan/readability preferred over strict time-gap fidelity

## Diagnostics ideas

- Log span, chosen tick step, and tick-step change count while running stress loops.
- Add a debug overlay for oldest/newest x and computed span.
- Keep a regression test with jittered cadence patterns around target sample rate.
