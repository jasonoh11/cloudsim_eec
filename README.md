# CloudSim EEC Scheduler

This repository contains a Cloud simulator and a Greedy scheduler implementation for VM/task placement, retry handling, and end-of-run reporting.

## Project Layout

- `Scheduler.hpp` / `Scheduler.cpp`: scheduler policy and bookkeeping.
- `Interfaces.h`: public simulator/scheduler APIs.
- `Internal_Interfaces.h`: internal simulator APIs.
- `SimTypes.h`: shared enums and structs (tasks, machines, VMs, states).
- `Makefile`: build and run helpers, including timestamped log targets.
- `tmp/Input`: original workload input.
- `tmp/run_logs`: generated run logs.

## Scheduler Design

### Policy Summary

The current scheduler implements a memory-aware, first-fit Greedy policy:

1. Place each new task onto the first feasible machine (lowest machine ID order).
2. Prefer reusing an existing compatible VM on that machine.
3. Create a new compatible VM only if no reusable VM exists.
4. Enforce capacity cap using memory utilization: `projected_memory / machine_memory < 0.80`.
5. Restrict placement to machines in `S0` and with matching CPU type.
6. If immediate placement fails, enqueue the task for periodic retries.
7. Retries continue on each periodic scheduler tick until task placement succeeds.

### Priority Mapping

Task priorities are derived from SLA and pushed into simulator task metadata:

- `SLA0`, `SLA1` -> `HIGH_PRIORITY`
- `SLA2` -> `MID_PRIORITY`
- `SLA3` -> `LOW_PRIORITY`

### Internal Bookkeeping

The scheduler tracks its own mirrors of simulator state:

- Machine view:
	- memory capacity and tracked usage
	- machine power state
	- active VM set and active task count
- VM view:
	- host machine
	- CPU/VM type
	- active task set and tracked memory footprint
	- migration-in-progress flag
- Task view:
	- required CPU/VM/SLA/memory metadata
	- assigned priority
	- placement/assignment/completion fields
	- retry count
- Retry queue:
	- FIFO queue processed during `SchedulerCheck`.

### Completion Behavior

On task completion, the scheduler:

1. Marks the task as completed.
2. Updates VM and machine tracked memory/task counts.
3. Clears task-to-VM assignment mapping.
4. Shuts down empty VMs.

Note: the simulator removes tasks from VMs before `HandleTaskCompletion` callback, so scheduler-side completion logic only updates scheduler mirrors and does not call `VM_RemoveTask`.

## End-of-Run Reporting

At simulation completion, output includes:

- SLA violation report from simulator (`SLA0`, `SLA1`, `SLA2`)
- Total energy consumed
- Simulated runtime
- Scheduler report:
	- tasks seen
	- tasks completed
	- successful placements
	- retry enqueues
	- retry attempts
	- placement failures
	- VMs created
	- migrations
	- wakeups
	- failed task IDs

## Build Instructions

Build all:

```bash
make all
```

This produces executable `./simulator`.

## Run Instructions

### Direct run

```bash
./simulator
```

### Verbose runs

```bash
make run-v           # verbose level 3
make run-v4          # verbose level 4 (scheduler-level visibility)
```

You can override input path:

```bash
make run-v4 INPUT=./tmp/Input
```

### Logged runs (recommended)

```bash
make run-log         # default verbosity
make run-v-log       # verbosity 3
make run-v4-log      # verbosity 4
```

With custom label and input:

```bash
make run-v4-log LABEL=testinputretryfix INPUT=./tmp/Input
```

Logs are written to:

- `tmp/run_logs/<LABEL>_<YYYYMMDD_HHMMSS>.log`

## Practical Validation Workflow

For long original-input runs:

1. Start with a logged run.
2. Heartbeat-check progress by sampling:
	 - last `SchedulerCheck` time
	 - task completion count
	 - presence of exceptions
3. Confirm completion marker and scheduler report at end.

Useful checks:

```bash
grep -c "HandleNewTask(): Received new task" <log>
grep -c "HandleTaskCompletion(): Task" <log>
grep -c "SimulationComplete(): Simulation finished" <log>
grep -E -c "Caught an exception|Bailing out|ThrowException" <log>
```

## Troubleshooting

### Symptom: Log has one line then many NUL characters

Cause: disk quota exceeded while writing log output. The simulator writes the first line, then later writes fail and output file may appear as one text line plus NUL padding.

Fix:

1. Free space in `tmp/run_logs` (older large logs).
2. Re-run with lower verbosity for long tests (`run-v-log` or `run-log`).

Check log directory size:

```bash
du -h tmp/run_logs
ls -lhS tmp/run_logs | head
```

### Symptom: long run appears stuck

Use heartbeat checks on latest log:

- if simulated time and completion counts increase, it is progressing;
- if both are flat for a long interval, inspect retry behavior and queue dynamics.

## Final Policy Decision

After comparing several migration experiments against the protection-only baseline on `Spikey Mean`, the active policy is now:

1. `kCapacityCap = 0.70` for additional headroom.
2. Proactive host protection for at-risk tasks.
3. No active migration in the default path.

Why:

- Protection plus headroom consistently performed better than migration variants on the tested burst trace.
- Migration added churn without improving SLA enough to justify keeping it in the active path.
- The migration helpers remain in the code for future experimentation, but they are not part of the default policy.

Tracked comparisons are maintained in [SLA_RUN_TRACKER.md](SLA_RUN_TRACKER.md).

## Known Scope / Non-Goals (Current Implementation)

Not currently implemented as active policy logic:

- consolidation/migration strategy
- wake/sleep machine state policy
- P-state tuning policy
- SLA warning response policy

The scheduler tracks counters for these but does not actively drive those controls yet.


