# Dashboard and Telemetry History in FRC

This document explains how FRC dashboard and telemetry architectures evolved, why key design decisions were made, and how those lessons inform modern tools.

- Edit this file when educational historical context needs updates.

Audience: students and mentors who want to understand software architecture trade-offs, not just tool names.

For current repository-specific debugging lessons, also see:

- `docs/learning/`
- `docs/journal/`

## Why this history matters

When teams build dashboards, they are really building **data systems**: a robot produces state, software transports it, and humans interpret it in real time. FRC dashboard history is a series of attempts to improve that pipeline under real constraints:

- limited and sometimes unstable network links
- strict match-time reliability needs
- fast iteration by students each season
- many programming languages and team skill levels

Understanding those constraints helps explain why older systems looked the way they did, and why newer systems are shifting toward more structured telemetry and logging.

## 1) Early FRC telemetry and dashboards

In early control systems, the primary goal was dependable operation during competition, not rich UI.

- Telemetry was relatively small and pragmatic (status values, basic diagnostics, simple indicators).
- Data traveled through the field/driver-station control path, where bandwidth and reliability were precious.
- Dashboard capabilities were limited compared to modern expectations.

### Early design goals

The architecture prioritized:

- **Simplicity**: fewer moving parts reduced failure modes.
- **Reliability**: if something broke, teams needed predictable behavior under pressure.
- **Low bandwidth usage**: links had to serve control traffic first, telemetry second.

### Conceptual early pipeline

```text
[Robot Code] --> [Control/Comms Layer] --> [Driver Station] --> [Basic Dashboard View]

Design priority: keep control stable, keep telemetry lightweight.
```

## 2) The introduction of SmartDashboard

As teams wanted richer runtime visibility, SmartDashboard was introduced to make telemetry easier to publish and visualize.

### Why SmartDashboard was compelling

SmartDashboard reduced friction for teams:

- publish values with simple API calls (`PutNumber`, `PutBoolean`, etc.)
- automatically show values in a dynamic UI
- support multiple robot languages through a shared protocol

### Architecture at a glance

SmartDashboard relied on **NetworkTables**, a distributed key-value system:

```text
               +----------------------+
               |   Dashboard Client   |
               | (widgets, UI layout) |
               +----------^-----------+
                          |
                          | key/value sync
                          v
               +----------------------+
               |   NetworkTables      |
               | distributed state    |
               +----------^-----------+
                          |
                          | publish/subscribe
                          v
               +----------------------+
               |      Robot Code      |
               | SmartDashboard API   |
               +----------------------+
```

### Benefits at the time

- **Simple data publishing** for teams learning software quickly.
- **Dynamic widgets** without hardcoding every screen element.
- **Language-agnostic integration** via a common transport abstraction.

### Limitations that appeared over time

As robots and software stacks became more complex, teams hit constraints:

- **Loosely typed key/value model**: key naming and type consistency became social conventions, not hard contracts.
- **Synchronization complexity**: distributed shared state can be hard to reason about under reconnects, stale values, and multi-client scenarios.
- **UI performance constraints**: dynamic widget systems can struggle when value counts and update rates grow.
- **Scaling challenges**: ad-hoc key spaces become harder to maintain as projects and subsystems expand.

## 3) NetworkTables architecture and trade-offs

NetworkTables is best understood as a distributed, synchronized key-value layer for robot and tool communication.

### Core design characteristics

- distributed publish/subscribe key-value store
- automatic synchronization between peers
- multi-client communication (robot, dashboards, utility tools)

### Why it helped teams

It removed a lot of custom networking work. Teams could focus on robot behavior and quickly attach dashboards and tooling.

### Architectural coupling it introduced

The same shared-state abstraction that enabled speed also coupled systems:

- robot code and UI logic often depended on the same key namespace
- UI behavior could implicitly depend on transport/state side effects
- debugging often required understanding both data production and synchronization semantics

In short: NetworkTables optimized for accessibility and interoperability, but that convenience can hide complexity when systems scale.

## 4) Evolution of modern FRC dashboards

Later tools addressed different parts of the SmartDashboard-era limitations:

- **Shuffleboard**: richer layouting and plugin-based visualization workflows.
- **Glass**: lightweight, focused runtime inspection and control experiences.
- **AdvantageScope**: strong analysis workflow centered around logs and high-fidelity playback.

### Broad improvements across modern tools

- **More structured telemetry usage** (clearer schemas and conventions).
- **Logging-first analysis** for post-match debugging and deterministic replay.
- **Improved visualization pipelines** that separate acquisition, storage, and rendering concerns.

### Shift in mindset

The ecosystem gradually moved from "live shared state is everything" toward "choose the right data path for the job":

- live stream for operator awareness and quick checks
- logs for deep analysis, reproducibility, and performance tuning

## 5) Lessons learned (architecture)

Several recurring lessons appear across FRC dashboard generations.

### A) Separate telemetry transport from UI

When transport and widgets are tightly coupled, changing one often destabilizes the other.

```text
Preferred layering:

[Robot Producers] -> [Telemetry Transport] -> [Ingestion/Model] -> [Visualization]
```

### B) Structured telemetry beats ad-hoc dynamic keys at scale

Dynamic keys are great for fast prototyping, but larger systems benefit from explicit contracts (types, units, ownership).

### C) Logging and live visualization serve different purposes

- live data answers "what is happening now?"
- logs answer "what exactly happened, and can we replay it?"

Treating these as separate design targets improves both.

### D) Deterministic pipelines are easier to debug than implicit event chains

Predictable ingestion/update flow reduces race conditions and "it depends" behavior across clients.

## 6) Implications for new dashboard designs

Modern dashboard architectures often choose to:

- simplify networking layers where possible
- avoid unnecessary globally synchronized distributed state
- separate telemetry ingestion from visualization rendering
- use predictable, explicit data pipelines

### Example architectural contrast

```text
Legacy-style emphasis:
Robot <-> Shared Distributed State <-> UI

Modern-style emphasis:
Robot -> Telemetry Stream/Log -> Local Model -> UI
```

The second pattern does not forbid distributed tooling; it just makes boundaries clearer and behavior easier to reason about.

## 7) Relation to this repository

This repository is an experimental C++ dashboard prototype exploring an alternative architecture.

- It intentionally **avoids NetworkTables** to reduce shared-state coupling and avoid NetworkTables-style key-space/synchronization bloat.
- It focuses on a **simpler data pipeline** aimed at clarity, predictable behavior, and performance.
- It treats architecture as a teaching tool so students can see where transport decisions affect UI and system reliability.

### What this project preserves (and why it remains useful)

Even while avoiding NetworkTables, this repository intentionally preserves several ideas that made prior FRC tooling effective:

- **Simple, typed publishing surface**: `PutBoolean`/`PutDouble`/`PutString` and corresponding `get` patterns (passive/assertive/callback) keep robot-side usage approachable.
- **Key-based dashboard workflow**: dynamic key discovery and widget binding still support rapid iteration during build season.
- **Transport abstraction boundary**: app/client logic depends on interfaces, not a single protocol, which keeps future transport swaps practical.
- **Runtime layout ergonomics**: editable widget placement and persistence preserve the “tune it live” workflow teams rely on.
- **Cross-team extensibility**: keeping the direct layer modular allows other teams to plug in alternate backends without rewriting UI/model code.

These preserved concepts are useful because they keep onboarding and iteration speed high while reducing distributed shared-state complexity.

### Deliberate trade-offs in current prototype

- **Single-dashboard-instance operation** is currently enforced for reliability with the present ring-buffer read-cursor model.
- **Direct transport simplicity over full feature parity** was chosen to get a deterministic baseline before broader multi-client semantics.
- **Incremental bidirectional support** (dashboard -> app controls) is being added in slices to keep behavior testable and understandable.

### Expansion paths for future teams

- Add a multi-consumer cursor strategy if true concurrent dashboard instances are required.
- Add additional transport adapters (e.g., different IPC/network backends) behind the same client/subscriber interfaces.
- Keep the typed API and widget/model boundaries stable so teams can evolve transport independently of UI behavior.

As the project evolves, this history should be updated with concrete results, trade-offs observed in testing, and final design decisions.

## Closing perspective

SmartDashboard and NetworkTables solved real problems for their time and enabled thousands of teams to move faster. Modern tools and architectures are not "replacements" in a simplistic sense; they are responses to new scale, new analysis needs, and deeper software maturity in the FRC community.

The key engineering habit for students: understand the constraint, then choose the architecture that fits it.
