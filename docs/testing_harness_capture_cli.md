# SmartDashboard Capture CLI (Testing Harness)

`SmartDashboardCaptureCli.exe` is a standalone command-driven telemetry capture tool for automated performance A/B runs.

This tool is intentionally useful for teaching and test harness workflows:

- repeatable captures for experimental conditions (A/B variants)
- metadata-rich output for post-run grouping/plotting
- orchestration-friendly stop conditions (duration and stop-file)

## Build target

- CMake target: `SmartDashboardCaptureCli`
- Expected Windows output path (Debug): `build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe`

## Command arguments

Required:

- `--out <path>` output JSON file
- `--label <string>` human-readable run label
- `--duration-sec <number>` capture duration in seconds

Preferred/optional:

- `--start-delay-ms <number>` wait before capture (default `0`)
- `--sample-ms <number>` coalesce interval in milliseconds (`0` keeps raw update cadence)
- `--overwrite` overwrite existing output file
- `--append` append another run document to the same file
- `--quiet` minimal console output
- `--verbose` additional run diagnostics
- `--tag <k=v>` repeatable metadata tags
- `--mapping-name <name>` direct subscriber mapping override
- `--data-event-name <name>` direct subscriber data-event override
- `--heartbeat-event-name <name>` direct subscriber heartbeat-event override
- `--wait-for-connected-ms <number>` fail if direct channel does not reach `Connected` in time (default `2000`)
- `--require-first-sample` fail non-zero if zero telemetry samples were captured
- `--connect-method <direct|auto>` channel selection strategy (`direct` by default)
- `--list-signals` print observed signals and exit
- `--signals <csv>` capture only selected signals
- `--stop-file <path>` stop early when file appears
- `--run-id <string>` explicit run id (auto-generated when omitted)

## Behavior summary

- Exit code `0` on success, non-zero on argument/start/write errors.
- Exit code `6` when connection does not reach `Connected` before timeout.
- Exit code `7` when `--require-first-sample` is set and no data was captured.
- Prints start/end time, output path, and per-signal sample counts.
- Prints connection diagnostics in summary:
  - `Connection observed during capture`
  - `Connection state at capture end` (before subscriber stop)
  - `Post-stop connection state` (typically `Disconnected`)
- Writes metadata and signal-series data in stable JSON schema.
- Uses temp-file + rename for overwrite mode to avoid truncated output on normal completion.

### Connection troubleshooting for empty captures

If files are valid JSON but signals are empty, the most common issue is direct channel name mismatch.

Use one or more of:

- `--mapping-name`
- `--data-event-name`
- `--heartbeat-event-name`

And add:

- `--wait-for-connected-ms 5000`
- `--require-first-sample`

If you are unsure which direct channel family is active, use:

- `--connect-method auto`

`auto` currently tries explicit overrides first (if provided), then known built-in direct channel defaults.

This gives an explicit failure instead of silently producing an empty run file.

## Example commands

### example_name_1 worker ON prefetch OFF

```powershell
build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe \
  --out runs/example_name_1_worker_on_prefetch_off.json \
  --label "example_name_1 worker ON prefetch OFF" \
  --duration-sec 45 \
  --tag app=example_name_1 --tag worker=on --tag prefetch=off
```

### example_name_1 worker OFF prefetch OFF

```powershell
build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe \
  --out runs/example_name_1_worker_off_prefetch_off.json \
  --label "example_name_1 worker OFF prefetch OFF" \
  --duration-sec 45 \
  --tag app=example_name_1 --tag worker=off --tag prefetch=off
```

### example_name_2 worker OFF prefetch OFF

```powershell
build/ClientInterface_direct/Debug/SmartDashboardCaptureCli.exe \
  --out runs/example_name_2_worker_off_prefetch_off.json \
  --label "example_name_2 worker OFF prefetch OFF" \
  --duration-sec 45 \
  --tag app=example_name_2 --tag worker=off --tag prefetch=off
```

## Output schema example (abridged)

```json
{
  "schema_version": 1,
  "metadata": {
    "label": "example_name_1 worker OFF prefetch OFF",
    "run_id": "run-1741813375123-13540",
    "start_time_utc": "2026-03-12T19:10:12Z",
    "end_time_utc": "2026-03-12T19:10:57Z",
    "duration_sec": 45,
    "captured_update_count": 12640,
    "args": {
      "start_delay_ms": 0,
      "sample_ms": 0,
      "overwrite": false,
      "append": false,
      "mapping_name": "",
      "data_event_name": "",
      "heartbeat_event_name": "",
      "wait_for_connected_ms": 2000,
      "require_first_sample": false,
      "connect_method": "direct",
      "signals": "",
      "stop_file": ""
    },
    "tags": {
      "app": "example_name_1",
      "worker": "off",
      "prefetch": "off"
    }
  },
  "signals": [
    {
      "key": "Perf/Fps",
      "type": "double",
      "sample_count": 2700,
      "samples": [
        { "t_us": 0, "value": 71.2 },
        { "t_us": 16670, "value": 70.9 }
      ]
    }
  ]
}
```
