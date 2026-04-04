# CloudSim EEC Scheduler

This repository contains the CloudSim simulator and the current scheduler implementation in `Scheduler.cpp` and `Scheduler.hpp`.

## Project Layout

- `Scheduler.hpp`, `Scheduler.cpp`: scheduling policy, shadow-state accounting, retry handling, wake handling, and end-of-run reporting.
- `Interfaces.h`: scheduler-facing public simulator APIs.
- `Internal_Interfaces.h`: internal simulator APIs.
- `SimTypes.h`: shared enums and data structures (machine/task/VM metadata).
- `Makefile`: build and run targets (`run-v`, `run-v4`, and logged variants).
- `tmp/`: workload input files.
- `tmp/run_logs/`: generated run logs.

## Current Scheduler Implementation

### Core Placement Policy

The scheduler is first-fit placement with VM reuse, guided by SLA-aware machine ordering and load caps:

1. New task arrives in `NewTask()`.
2. Task priority is set from SLA and mirrored in simulator metadata via `SetTaskPriority`.
3. `TryPlaceTask()` scans machines in a precomputed `sorted_machines` list (fastest to slowest by `mips_p0`).
4. For each machine, scheduler prefers reusing a compatible VM; otherwise it creates a new VM.
5. Placement feasibility is checked by `IsMachineFeasible()`.

### Feasibility Rules

`IsMachineFeasible()` currently requires:

- machine in `S0`
- `state_change_pending == false`
- CPU type match
- GPU match for GPU-capable tasks
- raw memory fit (`projected_memory < machine_memory`)
- projected load (`(active_tasks + 1) / num_cores`) below SLA-specific cap

Load caps by SLA (`MaxLoadForSLA`):

- `SLA0`: `<= 1.0` tasks per core
- `SLA1`: `<= 2.0` tasks per core
- `SLA2`: `<= 4.0` tasks per core
- `SLA3`: `<= 8.0` tasks per core

### Placement Direction by SLA

- `SLA0` and `SLA1` iterate fast-to-slow, so urgent work prefers faster machines.
- `SLA2` and `SLA3` iterate slow-to-fast, leaving faster machines available for tighter-SLA work.

### Retry Queue Behavior

- Failed immediate placements are enqueued by `EnqueueRetry()`.
- `ProcessRetryQueue()` processes up to `kRetryBatchCap = 1000` entries per call.
- Tasks are re-enqueued while capacity-constrained.
- Tasks are permanently failed only when no compatible hardware exists (`IsHardwareCompatible == false`).
- `retry_count` and `Total retry misses` are diagnostic counters; they do not force failure.

### Wake Handling

- `Init()` issues `Machine_SetState(..., S0)` for all machines and marks `state_change_pending` while transitions complete.
- `StateChangeComplete()` delegates to `HandleMachineWake()`.
- `HandleMachineWake()` clears pending state, refreshes that machine's shadow view, and immediately drains retry queue.

### Periodic and Event Flow

- `PeriodicCheck(now)`: refresh machine state from simulator, then process retry queue.
- `SLAWarn(now, task_id)`: currently uses warning as another opportunity to process retries.
- `TaskComplete(now, task_id)`: updates shadow memory/load immediately and triggers retry processing if queue is non-empty.
- `MigrationComplete(...)`: implemented as a no-op callback (scheduler does not actively migrate in this implementation).

### State Mirrors and Accounting

Scheduler keeps mirrored state for:

- machines: `machine_views` (includes cores, P0 MIPS, memory, GPU, state, active-task count)
- VMs: `vm_states`
- tasks: `task_states`
- per-machine VM index: `machine_to_vms`
- retry queue and failed task IDs
- counters (`tasks_seen`, `retry_attempts`, `placement_failures`, `vms_created`, and `Total retry misses`)

Important design point: `RefreshMachineStatesFromSimulator()` is called only from `PeriodicCheck()` to avoid overwriting shadow updates mid-burst.

### Shutdown Reporting

At simulation end (`SimulationComplete()` then `Scheduler::Shutdown()`), output includes:

- SLA0/SLA1/SLA2 percentages
- total cluster energy
- simulated runtime
- scheduler counters:
  - tasks seen/completed
  - successful placements
  - retry enqueues/attempts
  - placement failures
  - VMs created
  - total retry misses
  - failed task IDs

## Build

```bash
make all
```

Build artifact: `./simulator`

## Run

Direct:

```bash
./simulator
```

Verbose presets:

```bash
make run-v           # -v 3
make run-v4          # -v 4
```

Custom input:

```bash
make run-v INPUT=./tmp/Input
```

Logged runs:

```bash
make run-log
make run-v-log
make run-v4-log
```

Example:

```bash
make run-v-log LABEL=spikey_mean INPUT=./tmp/Spikey_Mean
```

Logs are written under `tmp/run_logs/`.

## Notes

- Historical experiment outcomes are tracked in `SLA_RUN_TRACKER.md`.
- This README reflects the current code in `Scheduler.cpp` / `Scheduler.hpp`; use those files as the source of truth when experimenting with policy changes.


