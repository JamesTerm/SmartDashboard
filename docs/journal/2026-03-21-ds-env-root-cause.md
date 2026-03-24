# 2026-03-21 - DS Native Link TCP root cause: CRT/Win32 environment block desync

## Summary

The Driver Station would not bind port 5810 when launched normally (double-click or clean shell), even though `DriverStation.ini` correctly said `Mode=3` and `NativeLinkCarrier=tcp`. The port only bound when the DS was launched from a shell that already had `NATIVE_LINK_CARRIER=tcp` set as an inherited environment variable.

The fix was a one-line change in `Robot_Simulation`. The deeper lesson is about a subtle but documented Windows API trap.

---

## How the bug was isolated

The first step was ruling out all the plausible wrong explanations:

- The INI file was correct.
- The startup ordering (`ApplyNativeLinkEnvironment` before `CreateNativeLinkBackend`) was correct.
- `TcpServerCarrier::Start()` code itself was correct — it worked reliably when the env vars were inherited.
- There was no double `WSAStartup` anywhere.
- The `DashboardTransportRouter::SetMode` guard was not the issue.

The isolation came from two scripts:

- `C:\Temp\launch_ds_tcp.ps1` — launched DS with `$env:NATIVE_LINK_CARRIER = "tcp"` already set in the parent PowerShell. Port 5810 bound. Confirmed working.
- `C:\Temp\launch_ds_clean.ps1` — launched DS with `ProcessStartInfo` and no inherited vars. Port 5810 did not bind. Bug reproduced.

This proved the bug lived entirely in how `LoadServerConfigFromEnvironment()` read back the carrier value that `ApplyNativeLinkEnvironment()` had just written — even within the same process.

---

## The root cause

`NativeLink.cpp:104` (in `Robot_Simulation`) used `_dupenv_s` to read `NATIVE_LINK_CARRIER`:

```cpp
// Before (broken):
char* value = nullptr;
std::size_t required = 0;
if (_dupenv_s(&value, &required, "NATIVE_LINK_CARRIER") == 0 && value != nullptr)
    ...
```

While `ApplyNativeLinkEnvironment()` wrote it using `SetEnvironmentVariableA`:

```cpp
// In DriverStation.cpp, called before the backend is constructed:
SetEnvironmentVariableA("NATIVE_LINK_CARRIER", "tcp");
```

The problem: **`SetEnvironmentVariableA` is a Win32 API that writes to the OS environment block. `_dupenv_s` is a CRT function that reads from the CRT's own internal environment cache.** On Windows, these are separate stores. The CRT cache is initialized from the OS block at process startup and is not automatically kept in sync when the Win32 API mutates the OS block later. A write via `SetEnvironmentVariableA` after the CRT cache was initialized is invisible to `_dupenv_s`.

The same `LoadServerConfigFromEnvironment()` function was already using `GetEnvironmentVariableA` (Win32) for the three other variables it read (`NATIVE_LINK_CHANNEL_ID`, `NATIVE_LINK_HOST`, `NATIVE_LINK_PORT`). Only `NATIVE_LINK_CARRIER` used the mismatched CRT read — so only the carrier defaulted back to `SharedMemory`.

---

## The fix

Replace `_dupenv_s` with `GetEnvironmentVariableA` so the read uses the same Win32 env block as the write:

```cpp
// After (fixed):
char buffer[256] = {};
DWORD len = GetEnvironmentVariableA("NATIVE_LINK_CARRIER", buffer, static_cast<DWORD>(sizeof(buffer)));
if (len > 0 && len < sizeof(buffer))
{
    CarrierKind parsed = config.carrierKind;
    if (TryParseCarrierKind(buffer, parsed))
        config.carrierKind = parsed;
}
```

An `Ian:` comment was added at the fix site explaining the CRT/Win32 desync hazard so this trap is not rediscovered.

---

## Important ah-ha moments

### 1. "It works with inherited env vars" is a precise diagnostic, not a quirk

When the env vars were inherited from the parent shell, they were part of the process environment from the moment the CRT initialized its cache — so `_dupenv_s` could see them. When the DS set them itself after startup via `SetEnvironmentVariableA`, the CRT cache was already stale. That asymmetry was the signal.

### 2. Mixing Win32 and CRT env APIs in the same function is a latent trap

The other three reads in `LoadServerConfigFromEnvironment()` used `GetEnvironmentVariableA` and worked fine. Only the one CRT call was broken. The inconsistency was not obviously wrong from a code review, but the rule is simple: never mix write-side and read-side APIs from different layers for the same variable.

### 3. A passing test can mask a stale contract

Two `SmartDashboard_tests` were expecting `Start()` to return `false` when no server was listening. They had been written against the old synchronous blocking TCP connect, which did fail immediately and return `false`. After the TCP client was made non-blocking (in a prior session), `Start()` always returned `true` and failure was reported asynchronously via `Disconnected` callbacks. The tests had been passing at baseline only because they ran in isolation before the "DefaultsToTcp" test touched those ports — a fragile coincidence. The correct fix was to update the tests to match the actual contract: `Start()` succeeds (non-blocking), and `Disconnected` arrives within the connect timeout. Both tests now use `auto_connect:false` and `WaitForCondition` for the `Disconnected` state.

### 4. Stale tests can be harder to notice than stale code

The test failure drew attention to a contract mismatch that had been quietly lurking. When the behavioral contract of a component changes, the tests that described the old behavior need to be updated to describe the new intent — not just to pass. The updated tests are now more useful: they assert that `Start()` is prompt and that a missing server produces a `Disconnected` callback rather than a refused launch.

---

## Verification

- `C:\Temp\test_clean_launch.ps1`: DS launched with clean `ProcessStartInfo` (no inherited env). Port 5810 bound (PID confirmed via `netstat`).
- 21/21 `NativeLinkTransport_tests` pass.
- 32/32 `SmartDashboard_tests` pass (including the two updated no-authority tests).
