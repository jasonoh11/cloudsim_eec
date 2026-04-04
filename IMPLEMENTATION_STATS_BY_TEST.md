# Implementation Statistics by Test Input

This document tracks scheduler outcomes per test input across implementation versions.

Sources:
- Historical runs: SLA_RUN_TRACKER.md (Run Groups A-H)
- Latest run-v batch: tmp/run_logs/runv_batch_summary_clean_20260403_191632.tsv (Run Group I)
- Latest run-v batch: tmp/run_logs/runv_suite_manual_20260403_231110.tsv (Run Group J)
- Latest run-v batch: tmp/run_logs/runv_suite_manual_20260404_005921.tsv (Run Group K)

## Run Labels

- A: Baseline Greedy + Retry
- B: Keep Empty VMs + Periodic Refresh
- C: Host Protection + Frequent Refreshes
- D: Migration v1
- E: Migration v1.1 (Spikey only)
- F: Headroom cap 0.70 + tighter target (Spikey only)
- G: Migration urgency <=10% (Spikey only)
- H: Parallel batch, protection+headroom context
- I: Current codebase run-v batch (2026-04-03), load-cap scheduler
- J: Current codebase run-v batch (2026-04-03), updated greedy implementation
- K: Current codebase run-v batch (2026-04-04), updated greedy rerun

## Big_Small

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| H | 88.8417% | 58.5366% | 0% | - | 0 | 0 | 3 | - | - | Previous implementation |
| I | 0.0499251% | 0% | 0% | 0.029202 | 3841 | 3532384 | 24 | 0 | 3528543 | Current run-v result |
| J | 0% | 0% | 0% | 0.0292385 | 3857 | 3535952 | 24 | 0 | 3532095 | Current updated greedy run |
| K | 0.0499251% | 0% | 0% | 0.029202 | 3841 | 3532384 | 24 | 0 | 3528543 | Current updated greedy rerun |

## canvas.txt

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| I | 0% | 0% | 0% | 7.27281 | 0 | 0 | 27 | 0 | 0 | Current run-v result |
| J | NA | NA | NA | NA | NA | NA | NA | NA | NA | Run produced no final summary lines |
| K | 0% | 0% | 0% | 6.50387 | 0 | 0 | 27 | 0 | 0 | Current updated greedy rerun |

## Day

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| I | 0% | 0% | 0% | 64.0121 | 0 | 0 | 2 | 0 | 0 | Current run-v result |
| J | 0% | 0% | 0% | 46.733 | 0 | 0 | 2 | 0 | 0 | Current updated greedy run |
| K | 0% | 0% | 0% | 50.5081 | 0 | 0 | 2 | 0 | 0 | Current updated greedy rerun |

## Gentler_Hour

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| A | 75.873% | 93.4399% | 6.90742% | - | 6 | 33 | 4478 | - | - | Historical baseline |
| B | 75.873% | 93.3595% | 6.95695% | - | 6 | 33 | 10 | - | - | Keep-empty-VMs variant |
| C | 13.6508% | 86.8586% | 5.59708% | - | 6 | 33 | 11 | - | - | Host protection |
| D | 57.7778% | 84.3436% | 5.471% | - | 6 | 33 | 15 | - | - | Migration v1 |
| H | 13.6508% | 86.8586% | 5.59708% | - | 10 | 78 | 11 | - | - | Parallel batch |
| I | 0% | 0% | 0% | 7.25414 | 0 | 0 | 23 | 0 | 0 | Current run-v result |
| J | NA | NA | NA | NA | NA | NA | NA | NA | NA | Run produced no final summary lines |
| K | 0% | 0% | 0% | 6.47751 | 0 | 0 | 23 | 0 | 0 | Current updated greedy rerun |

## Input

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| H | 0% | 0% | 14.0969% | - | 54 | 10964 | 14 | - | - | Previous implementation |
| I | 0% | 0% | 0% | 0.0124647 | 53 | 15789 | 14 | 0 | 15736 | Current run-v result |
| J | 0% | 0% | 0% | 0.0123505 | 53 | 15789 | 14 | 0 | 15736 | Current updated greedy run |
| K | 0% | 0% | 0% | 0.0124647 | 53 | 15789 | 14 | 0 | 15736 | Current updated greedy rerun |

## Match_Me_If_You_Can

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| A | 88.5671% | 58.5366% | 0% | - | 0 | 0 | 8 | - | - | Historical baseline |
| B | 88.4923% | 58.5366% | 0% | - | 0 | 0 | 3 | - | - | Keep-empty-VMs variant |
| C | 88.4423% | 58.5366% | 0% | - | 0 | 0 | 3 | - | - | Host protection |
| D | 92.012% | 58.5366% | 0% | - | 0 | 0 | 3 | - | - | Migration v1 |
| H | 88.8417% | 58.5366% | 0% | - | 0 | 0 | 3 | - | - | Parallel batch |
| I | 0% | 0% | 0% | 0.045626 | 1671 | 1001785 | 40 | 0 | 1000114 | Current run-v result |
| J | 0% | 0% | 0% | 0.0438264 | 1940 | 1074368 | 40 | 0 | 1072428 | Current updated greedy run |
| K | 0% | 0% | 0% | 0.045626 | 1671 | 1001785 | 40 | 0 | 1000114 | Current updated greedy rerun |

## Nice_and_Smooth

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| H | 0% | 0% | 0% | - | 0 | 0 | 1 | - | - | Previous implementation |
| I | 0% | 0% | 0% | 0.0121098 | 0 | 0 | 1 | 0 | 0 | Current run-v result |
| J | 0% | 0% | 0% | 0.0100042 | 0 | 0 | 1 | 0 | 0 | Current updated greedy run |
| K | 0% | 0% | 0% | 0.0121098 | 0 | 0 | 1 | 0 | 0 | Current updated greedy rerun |

## Spikey_Mean

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Migrations | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| A | 88.5671% | 58.5366% | 0% | - | 0 | 0 | 8 | 0 | - | - | Historical baseline |
| B | 88.4923% | 58.5366% | 0% | - | 0 | 0 | 3 | 0 | - | - | Keep-empty-VMs variant |
| C | 88.4423% | 58.5366% | 0% | - | 0 | 0 | 3 | 0 | - | - | Host protection |
| D | 92.012% | 58.5366% | 0% | - | 0 | 0 | 3 | 48 | - | - | Migration v1 |
| E | 89.4159% | 58.5366% | 0% | - | 0 | 0 | 3 | 41 | - | - | Migration v1.1 |
| F | 88.8417% | 58.5366% | 0% | - | 0 | 0 | 3 | 37 | - | - | Headroom 0.70 |
| G | 88.8417% | 58.5366% | 0% | - | 0 | 0 | 3 | 37 | - | - | Urgent migration only |
| H | 88.8417% | 58.5366% | 0% | - | 0 | 0 | 3 | 0 | - | - | Parallel batch |
| I | 0% | 0% | 0% | 0.0250182 | 3875 | 3626180 | 16 | 0 | 0 | 3622305 | Current run-v result |
| J | 0% | 0% | 0% | 0.025083 | 3907 | 3643937 | 16 | 0 | 0 | 3640030 | Current updated greedy run |
| K | 0% | 0% | 0% | 0.0250182 | 3875 | 3626180 | 16 | 0 | 0 | 3622305 | Current updated greedy rerun |

## SpikeyNefarious

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| H | 52.5397% | 37.8049% | 0% | - | 0 | 0 | 2 | - | - | Previous implementation |
| I | 0% | 0% | 0% | 0.0116119 | 0 | 0 | 16 | 0 | 0 | Current run-v result |
| J | 0% | 0% | 0% | 0.0115645 | 49 | 419 | 16 | 0 | 370 | Current updated greedy run |
| K | 0% | 0% | 0% | 0.0116119 | 0 | 0 | 16 | 0 | 0 | Current updated greedy rerun |

## Tall_Short

| Run | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Retry Enqueues | Retry Attempts | VMs Created | Migrations | Placement Failures | Total Retry Misses | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| A | 100% | 58.5366% | 0% | - | 0 | 0 | 8 | 0 | - | - | Historical baseline |
| B | 100% | 58.5366% | 0% | - | 0 | 0 | 3 | 0 | - | - | Keep-empty-VMs variant |
| C | 100% | 58.5366% | 0% | - | 0 | 0 | 3 | 0 | - | - | Host protection |
| D | 100% | 58.5366% | 0% | - | 0 | 0 | 3 | 69 | - | - | Migration v1 |
| H | 100% | 58.5366% | 0% | - | 0 | 0 | 3 | 0 | - | - | Parallel batch |
| I | 40.4393% | 0% | 0% | 0.0351485 | 3846 | 4714178 | 24 | 0 | 0 | 4710332 | Current run-v result |
| J | 40.7888% | 0% | 0% | 0.0350112 | 3862 | 4729450 | 24 | 0 | 0 | 4725588 | Current updated greedy run |
| K | 40.4393% | 0% | 0% | 0.0351485 | 3846 | 4714178 | 24 | 0 | 0 | 4710332 | Current updated greedy rerun |

## Run I Full End-of-Run Statistics

| Input | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Sim Seconds | Tasks Seen | Tasks Completed | Successful Placements | Retry Enqueues | Retry Attempts | Placement Failures | VMs Created | Failed Task IDs | Total Retry Misses |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| Big_Small | 0.0499251% | 0% | 0% | 0.029202 | 29.82 | 4088 | 4088 | 4088 | 3841 | 3532384 | 0 | 24 | none | 3528543 |
| canvas.txt | 0% | 0% | 0% | 7.27281 | 3603.48 | 256587 | 256587 | 256587 | 0 | 0 | 0 | 27 | none | 0 |
| Day | 0% | 0% | 0% | 64.0121 | 86401.1 | 481258 | 481258 | 481258 | 0 | 0 | 0 | 2 | none | 0 |
| Gentler_Hour | 0% | 0% | 0% | 7.25414 | 3603.48 | 46550 | 46550 | 46550 | 0 | 0 | 0 | 23 | none | 0 |
| Input | 0% | 0% | 0% | 0.0124647 | 17.34 | 865 | 865 | 865 | 53 | 15789 | 0 | 14 | none | 15736 |
| Match_Me_If_You_Can | 0% | 0% | 0% | 0.045626 | 18.54 | 4088 | 4088 | 4088 | 1671 | 1001785 | 0 | 40 | none | 1000114 |
| Nice_and_Smooth | 0% | 0% | 0% | 0.0121098 | 16.32 | 82 | 82 | 82 | 0 | 0 | 0 | 1 | none | 0 |
| Spikey_Mean | 0% | 0% | 0% | 0.0250182 | 28.02 | 4088 | 4088 | 4088 | 3875 | 3626180 | 0 | 16 | none | 3622305 |
| SpikeyNefarious | 0% | 0% | 0% | 0.0116119 | 16.32 | 712 | 712 | 712 | 0 | 0 | 0 | 16 | none | 0 |
| Tall_Short | 40.4393% | 0% | 0% | 0.0351485 | 34.56 | 4088 | 4088 | 4088 | 3846 | 4714178 | 0 | 24 | none | 4710332 |

## Run J Full End-of-Run Statistics

| Input | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Sim Seconds | Tasks Seen | Tasks Completed | Successful Placements | Retry Enqueues | Retry Attempts | Placement Failures | VMs Created | Failed Task IDs | Total Retry Misses |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| Big_Small | 0% | 0% | 0% | 0.0292385 | 29.88 | 4088 | 4088 | 4088 | 3857 | 3535952 | 0 | 24 | none | 3532095 |
| canvas.txt | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA |
| Day | 0% | 0% | 0% | 46.733 | 86401.1 | 481258 | 481258 | 481258 | 0 | 0 | 0 | 2 | none | 0 |
| Gentler_Hour | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA | NA |
| Input | 0% | 0% | 0% | 0.0123505 | 17.34 | 865 | 865 | 865 | 53 | 15789 | 0 | 14 | none | 15736 |
| Match_Me_If_You_Can | 0% | 0% | 0% | 0.0438264 | 18.66 | 4088 | 4088 | 4088 | 1940 | 1074368 | 0 | 40 | none | 1072428 |
| Nice_and_Smooth | 0% | 0% | 0% | 0.0100042 | 16.32 | 82 | 82 | 82 | 0 | 0 | 0 | 1 | none | 0 |
| Spikey_Mean | 0% | 0% | 0% | 0.025083 | 28.14 | 4088 | 4088 | 4088 | 3907 | 3643937 | 0 | 16 | none | 3640030 |
| SpikeyNefarious | 0% | 0% | 0% | 0.0115645 | 16.32 | 712 | 712 | 712 | 49 | 419 | 0 | 16 | none | 370 |
| Tall_Short | 40.7888% | 0% | 0% | 0.0350112 | 34.38 | 4088 | 4088 | 4088 | 3862 | 4729450 | 0 | 24 | none | 4725588 |

## Run K Full End-of-Run Statistics

| Input | SLA0 | SLA1 | SLA2 | Energy (KW-Hour) | Sim Seconds | Tasks Seen | Tasks Completed | Successful Placements | Retry Enqueues | Retry Attempts | Placement Failures | VMs Created | Failed Task IDs | Total Retry Misses |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| Big_Small | 0.0499251% | 0% | 0% | 0.029202 | 29.82 | 4088 | 4088 | 4088 | 3841 | 3532384 | 0 | 24 | none | 3528543 |
| canvas.txt | 0% | 0% | 0% | 6.50387 | 3603.48 | 256587 | 256587 | 256587 | 0 | 0 | 0 | 27 | none | 0 |
| Day | 0% | 0% | 0% | 50.5081 | 86401.1 | 481258 | 481258 | 481258 | 0 | 0 | 0 | 2 | none | 0 |
| Gentler_Hour | 0% | 0% | 0% | 6.47751 | 3603.48 | 46550 | 46550 | 46550 | 0 | 0 | 0 | 23 | none | 0 |
| Input | 0% | 0% | 0% | 0.0124647 | 17.34 | 865 | 865 | 865 | 53 | 15789 | 0 | 14 | none | 15736 |
| Match_Me_If_You_Can | 0% | 0% | 0% | 0.045626 | 18.54 | 4088 | 4088 | 4088 | 1671 | 1001785 | 0 | 40 | none | 1000114 |
| Nice_and_Smooth | 0% | 0% | 0% | 0.0121098 | 16.32 | 82 | 82 | 82 | 0 | 0 | 0 | 1 | none | 0 |
| Spikey_Mean | 0% | 0% | 0% | 0.0250182 | 28.02 | 4088 | 4088 | 4088 | 3875 | 3626180 | 0 | 16 | none | 3622305 |
| SpikeyNefarious | 0% | 0% | 0% | 0.0116119 | 16.32 | 712 | 712 | 712 | 0 | 0 | 0 | 16 | none | 0 |
| Tall_Short | 40.4393% | 0% | 0% | 0.0351485 | 34.56 | 4088 | 4088 | 4088 | 3846 | 4714178 | 0 | 24 | none | 4710332 |

## Run K Log References

- tmp/run_logs/runv_Big_Small_20260404_005921.log
- tmp/run_logs/runv_canvas.txt_20260404_005921.log
- tmp/run_logs/runv_Day_20260404_005921.log
- tmp/run_logs/runv_Gentler_Hour_20260404_005921.log
- tmp/run_logs/runv_Input_20260404_005921.log
- tmp/run_logs/runv_Match_Me_If_You_Can_20260404_005921.log
- tmp/run_logs/runv_Nice_and_Smooth_20260404_005921.log
- tmp/run_logs/runv_Spikey_Mean_20260404_005921.log
- tmp/run_logs/runv_SpikeyNefarious_20260404_005921.log
- tmp/run_logs/runv_Tall_Short_20260404_005921.log

## Run J Log References

- tmp/run_logs/runv_Big_Small_20260403_231110.log
- tmp/run_logs/runv_canvas.txt_20260403_231110.log
- tmp/run_logs/runv_Day_20260403_231110.log
- tmp/run_logs/runv_Gentler_Hour_20260403_231110.log
- tmp/run_logs/runv_Input_20260403_231110.log
- tmp/run_logs/runv_Match_Me_If_You_Can_20260403_231110.log
- tmp/run_logs/runv_Nice_and_Smooth_20260403_231110.log
- tmp/run_logs/runv_Spikey_Mean_20260403_231110.log
- tmp/run_logs/runv_SpikeyNefarious_20260403_231110.log
- tmp/run_logs/runv_Tall_Short_20260403_231110.log

## Run I Log References

- tmp/run_logs/runv_Big_Small_20260403_191632.log
- tmp/run_logs/runv_canvas.txt_20260403_191632.log
- tmp/run_logs/runv_Day_20260403_191632.log
- tmp/run_logs/runv_Gentler_Hour_20260403_191632.log
- tmp/run_logs/runv_Input_20260403_191632.log
- tmp/run_logs/runv_Match_Me_If_You_Can_20260403_191632.log
- tmp/run_logs/runv_Nice_and_Smooth_20260403_191632.log
- tmp/run_logs/runv_Spikey_Mean_20260403_191632.log
- tmp/run_logs/runv_SpikeyNefarious_20260403_191632.log
- tmp/run_logs/runv_Tall_Short_20260403_191632.log
