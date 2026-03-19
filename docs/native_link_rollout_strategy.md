# Native Link Rollout Strategy

This note captures the long-term product and architecture strategy for `Native Link` so future sessions can make cleanup decisions against the same rollout goal.

## Core goal

`Native Link` should become a transport/runtime option teams can adopt with low friction, while also growing into a richer first-class interface for teams that want stronger semantics than `NetworkTables`-style shared state.

That means the rollout should support two paths at the same time:

- compatibility-first adoption for teams that do not want to rewrite robot code yet
- native-first adoption for teams that do want explicit descriptors, retained-state rules, ownership, and stronger diagnostics.

## Product stance

We should not frame `Native Link` as "rewrite everything and leave existing dashboard ecosystems behind."

We should frame it as:

- a cleaner semantic contract for dashboard/robot interaction
- a transport stack that can coexist with current FRC workflows
- an installable ecosystem with adapters and bridges, not only one dashboard app
- an upgrade path from compatibility mode to richer native semantics.

## The three-layer model

Long-term architecture should stay split into three layers.

### 1. Language interface layer

This is what robot/application code uses directly.

Planned examples:

- Java Native Link library
- C++ Native Link library
- possible Python package later.

This layer should feel natural for robot authors and be distributed in the same ways teams already expect from WPILib and vendor ecosystems.

### 2. Native Link semantic core

This is the contract that must stay stable across languages, dashboards, and carriers.

It owns:

- descriptor schema
- server-authoritative session/generation
- snapshot -> retained state -> live delta ordering
- explicit state vs command/event behavior
- lease/ownership rules
- no command replay
- provenance/freshness metadata.

This layer must stay transport-agnostic and dashboard-agnostic.

### 3. Adapter and carrier layer

This is where deployment-specific integration lives.

Examples:

- carriers: `shm`, `tcp`
- dashboard adapters: SmartDashboard, Shuffleboard, Elastic
- compatibility bridges: NT-facing adapters or standalone bridge services.

Important boundary rule:

- carriers move Native Link frames/semantics
- adapters translate between Native Link semantics and a host/dashboard ecosystem
- neither should redefine the contract.

## Rollout tracks

### Track A: compatibility-first

Goal: teams adopt without changing robot code.

Examples:

- a dashboard-side adapter that lets an existing dashboard consume Native Link data
- a bridge service that translates between `NT4` and `Native Link`
- installable packages that drop into existing operator workflows.

This is likely the easiest way to earn trust and real-world trials.

### Track B: native-first

Goal: teams adopt a true Native Link robot library.

Examples:

- Java vendordep + Maven artifacts
- C++ vendordep + native artifacts
- later Python package if useful.

This is where the full semantic value shows up most clearly, because robot code can publish descriptors and ownership intent directly instead of relying on compatibility inference.

## Distribution model to aim for

The rollout should match familiar FRC installation patterns where possible.

### Robot-side libraries

For Java/C++, the natural target is the WPILib/vendor-dependency style flow:

- vendordep JSON
- Maven/native artifacts
- optional offline installer for teams with poor connectivity or labs.

That matters because it matches how teams already add CTRE/REV/path-planning libraries.

### Dashboard-side integrations

Different dashboard ecosystems use different installation flows.

- `Shuffleboard` uses Java plugins loaded into the Shuffleboard plugin directory
- `Elastic` is positioned today as an `NT4` dashboard, so early adoption likely means an adapter/bridge strategy before a deeper native integration exists
- standalone SmartDashboard-family apps can ship their own plugin or built-in transport support.

So the product should plan for multiple package flavors instead of assuming one universal installer.

## Recommended package families

The long-term packaging story could look like this:

- `Native Link Runtime`
  - core runtime and diagnostics
  - optional bridge/service process
- `Native Link Robot SDK - Java`
  - vendordep + Maven artifacts
- `Native Link Robot SDK - C++`
  - vendordep + native artifacts
- `Native Link for SmartDashboard`
  - transport plugin or built-in support
- `Native Link for Shuffleboard`
  - plugin or bridge-assisted package
- `Native Link for Elastic`
  - bridge-assisted package first, deeper integration later if worth it.

This gives teams a low-friction first step and avoids making the whole ecosystem depend on one app.

## Strategic implications for current cleanup

These rollout goals should directly influence the code cleanup now.

### Do now

- keep one semantic contract above all carriers
- keep SmartDashboard-specific concerns out of the transport core
- keep Robot_Simulation-specific authority behavior separate from the reusable contract where practical
- create explicit adapter boundaries in addition to carrier boundaries
- document install/distribution assumptions early so future work does not lock us into one app.

### Avoid now

- hard-coding SmartDashboard assumptions into the core protocol
- treating `Robot_Simulation` as the permanent home of authority-side logic
- mixing dashboard widget behavior with transport semantics
- baking `NT4` topic conventions into the Native Link contract.

## Robot_Simulation's role

`Robot_Simulation` is still valuable, but its role should be clear.

- it is the first reference authority and validation harness
- it is not the whole product story
- it should model a good example use case for students: authority semantics, retained state, leases, reconnects
- it should not force the semantic core to stay trapped inside one simulation app.

That means future cleanup on the simulator side should keep moving toward reusable authority-side components under the same contract.

## Near-term roadmap

1. finish the carrier cleanup so `shm` and `tcp` both sit under the same semantic contract
2. preserve `shm` as the diagnostic/reference backend
3. complete localhost `tcp` parity in the current SmartDashboard + Robot_Simulation reference path
4. document an explicit adapter boundary beside the carrier boundary
5. sketch a future `NT4 <-> Native Link` bridge/service path
6. plan packaging for Java/C++ robot-side libraries and dashboard-side adapters.

## Decision rule for future sessions

When a design choice appears, prefer the option that best preserves all of the following at once:

- one semantic contract
- multiple carriers
- multiple adapters
- familiar FRC installation workflows
- a low-friction compatibility path for teams
- a richer native path for advanced teams.

If a shortcut helps one dashboard today but makes that harder later, it is probably the wrong shortcut.
