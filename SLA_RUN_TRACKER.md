# SLA Run Tracker

This file tracks observed SLA results by run configuration so changes can be compared over time.

## Update Rule

After each test batch, append one new run block with:
- Date/time
- Commit hash
- Features enabled/disabled
- Inputs tested
- SLA0/SLA1/SLA2
- Key counters (`VMs created`, `Migrations`, `Wakeups`, retries)
- Notes (regressions/improvements)

## Historical Runs (Recovered From Conversation)

### Run Group A: Baseline Greedy + Retry (Before Host Protection)

Context: initial stabilized MVP policy with greedy placement/retry and VM reuse improvements, no migration policy.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations |
|---|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 88.5671% | 58.5366% | 0% | 8 | 0 | 0 | 0 |
| Tall Short | 100% | 58.5366% | 0% | 8 | 0 | 0 | 0 |
| Gentler Hour | 75.873% | 93.4399% | 6.90742% | 4478 | 6 | 33 | 0 |
| Match Me If You Can | 88.5671% | 58.5366% | 0% | 8 | 0 | 0 | 0 |

### Run Group B: MVP Alignment Fixes (Keep Empty VMs + Periodic Refresh)

Context: empty VMs kept alive for reuse; periodic check refreshes machine state before retry processing.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations |
|---|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 88.4923% | 58.5366% | 0% | 3 | 0 | 0 | 0 |
| Tall Short | 100% | 58.5366% | 0% | 3 | 0 | 0 | 0 |
| Gentler Hour | 75.873% | 93.3595% | 6.95695% | 10 | 6 | 33 | 0 |
| Match Me If You Can | 88.4923% | 58.5366% | 0% | 3 | 0 | 0 | 0 |

### Run Group C: Host Protection + Frequent Refreshes (No Migration)

Context: at-risk detection (20% remaining SLA window), protected-machine placement skipping, frequent refresh in placement/event paths.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 88.4423% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |
| Tall Short | 100% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |
| Gentler Hour | 13.6508% | 86.8586% | 5.59708% | 11 | 6 | 33 | 0 | 0 |
| Match Me If You Can | 88.4423% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |

### Run Group D: Migration v1 (Guarded At-Risk VM Migration)

Context: migration enabled with cooldown + per-tick cap + target selection and standby wake fallback.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 92.012% | 58.5366% | 0% | 3 | 0 | 0 | 48 | 0 |
| Tall Short | 100% | 58.5366% | 0% | 3 | 0 | 0 | 69 | 0 |
| Gentler Hour | 57.7778% | 84.3436% | 5.471% | 15 | 6 | 33 | 215 | 0 |
| Match Me If You Can | 92.012% | 58.5366% | 0% | 3 | 0 | 0 | 123 | 0 |

### Run Group E: Migration v1.1 (Low-Util Target Gate + Longer Cooldown)

Context: migration only allowed if projected target utilization is low enough (`<= 0.55`) and per-VM migration cooldown increased from 2s to 10s. Quick iteration run on Spikey Mean only.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 89.4159% | 58.5366% | 0% | 3 | 0 | 0 | 41 | 0 |

Notes:
- Relative to Migration v1 on Spikey Mean (`SLA0 92.012%`, `Migrations 48`), this tuning improved SLA0 and reduced migrations.
- Relative to host-protection-without-migration baseline (`SLA0 88.4423%`), this is still worse, so migration tuning likely still needs tighter triggering or better destination policy.

### Run Group F: Headroom Increase (Cap 0.70 + Target Gate 0.45)

Context: increased headroom by lowering placement cap from 0.80 to 0.70 and making migration stricter by only allowing targets with projected utilization <= 0.45. Spikey Mean only.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 88.8417% | 58.5366% | 0% | 3 | 0 | 0 | 37 | 0 |

Notes:
- This is better than Migration v1 and v1.1 on Spikey Mean, but still not better than the no-migration host-protection baseline.
- It suggests extra headroom helps a bit, but the migration policy is still not yet a net win.

### Run Group G: Migration Tightened Further (Urgent <=10% Only)

Context: migration trigger tightened so it only fires for imminent SLA0 tasks (<=10% remaining SLA window). This run was used to decide whether to keep migration active.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean | 88.8417% | 58.5366% | 0% | 3 | 0 | 0 | 37 | 0 |

Notes:
- Still worse than the protection+headroom baseline (`SLA0 88.4423%`) on Spikey Mean.
- Final decision: keep protection + headroom only as the active policy and disable migration in the default path.

### Run Group H: Parallel Batch (Non-Day Completed, Day Still Running)

Context: active policy is protection + headroom only (`kCapacityCap=0.70`), migration disabled; Day was launched in a separate background run and non-Day inputs were executed in parallel.

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Big Small | 88.8417% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |
| Gentler Hour | 13.6508% | 86.8586% | 5.59708% | 11 | 10 | 78 | 0 | 0 |
| Input | 0% | 0% | 14.0969% | 14 | 54 | 10964 | 0 | 0 |
| Match Me If You Can | 88.8417% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |
| Nice and Smooth | 0% | 0% | 0% | 1 | 0 | 0 | 0 | 0 |
| Spikey Mean | 88.8417% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |
| SpikeyNefarious | 52.5397% | 37.8049% | 0% | 2 | 0 | 0 | 0 | 0 |
| Tall Short | 100% | 58.5366% | 0% | 3 | 0 | 0 | 0 | 0 |

Notes:
- Day is still running in background and will be added in a separate edit when complete.
- AnHour is also pending a separate clean run entry due earlier filename mismatch (`An Hour` vs `AnHour (1)`).

## Current Read

- Migration v1 increased migration count but regressed SLA0 on multiple traces.
- Host protection with no migration gave better SLA0 for Spikey/Match than migration v1.
- Gentler Hour is highly sensitive to policy changes and should remain part of every test gate.

## Next Entry Template

Copy/paste and fill after each run batch:

```md
### Run Group X: <short label>

Context: <features on/off>

| Input | SLA0 | SLA1 | SLA2 | VMs Created | Retry Enqueues | Retry Attempts | Migrations | Wakeups |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spikey Mean |  |  |  |  |  |  |  |  |
| Tall Short |  |  |  |  |  |  |  |  |
| Gentler Hour |  |  |  |  |  |  |  |  |
| Match Me If You Can |  |  |  |  |  |  |  |  |

Notes:
- 
```
