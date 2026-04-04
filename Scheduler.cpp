//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <algorithm>
#include <limits>

// ---------------------------------------------------------------------------
// InitializeMachineViews
// Build the shadow-state map from simulator ground truth.  Called once at
// Init before any tasks arrive.
// ---------------------------------------------------------------------------
void Scheduler::InitializeMachineViews() {
    machines.clear();
    machine_views.clear();

    const unsigned total = Machine_GetTotal();
    for(unsigned i = 0; i < total; i++) {
        const MachineId_t   machine_id = MachineId_t(i);
        const MachineInfo_t info       = Machine_GetInfo(machine_id);

        machines.push_back(machine_id);
        machine_views[machine_id] = MachineStateView{
            machine_id,
            info.cpu,
            info.num_cpus,
            info.performance[P0],   // MIPS at highest performance state
            info.memory_size,
            info.memory_used,
            info.gpus,
            info.s_state,
            info.active_tasks,
            false,                  // state_change_pending
            0                       // last_active_time — initialised to 0 (already active)
        };
    }

    // Build sorted_machines: fastest (highest mips_p0) first.
    sorted_machines = machines;
    sort(sorted_machines.begin(), sorted_machines.end(),
         [this](MachineId_t a, MachineId_t b) {
             return machine_views[a].mips_p0 > machine_views[b].mips_p0;
         });
}

// ---------------------------------------------------------------------------
// RefreshMachineStatesFromSimulator
// Overwrites tracked_memory_used, active_task_count, and s_state from the
// simulator.  Called ONLY from PeriodicCheck so that shadow state accumulated
// during a burst is never clobbered mid-event.
// ---------------------------------------------------------------------------
void Scheduler::RefreshMachineStatesFromSimulator() {
    for(const auto machine_id : machines) {
        const MachineInfo_t info = Machine_GetInfo(machine_id);
        auto &view = machine_views[machine_id];
        view.s_state             = info.s_state;
        view.tracked_memory_used = info.memory_used;
        view.active_task_count   = info.active_tasks;
    }
}

// ---------------------------------------------------------------------------
// PriorityFromSLA / MaxLoadForSLA
// ---------------------------------------------------------------------------
Priority_t Scheduler::PriorityFromSLA(SLAType_t sla) const {
    switch(sla) {
        case SLA0:  return HIGH_PRIORITY;
        case SLA1:  return MID_PRIORITY;
        case SLA2:  return LOW_PRIORITY;
        case SLA3:
        default:    return LOW_PRIORITY;
    }
}

// Maximum tasks-per-core ratio allowed for each SLA tier.
// SLA0 tasks must never share a core; SLA3 can be heavily oversubscribed.
double Scheduler::MaxLoadForSLA(SLAType_t sla) const {
    switch(sla) {
        case SLA0:  return kLoadCapSLA0;
        case SLA1:  return kLoadCapSLA1;
        case SLA2:  return kLoadCapSLA2;
        case SLA3:
        default:    return kLoadCapSLA3;
    }
}

// ---------------------------------------------------------------------------
// AdditionalPlacementMemory
// How much tracked_memory_used will increase if we place this task, counting
// VM_MEMORY_OVERHEAD only when we need to create a new VM.
// ---------------------------------------------------------------------------
unsigned Scheduler::AdditionalPlacementMemory(TaskId_t task_id, bool creating_vm) const {
    const auto it = task_states.find(task_id);
    if(it == task_states.end()) return 0;
    unsigned extra = it->second.required_memory;
    if(creating_vm) extra += VM_MEMORY_OVERHEAD;
    return extra;
}

// ---------------------------------------------------------------------------
// IsHardwareCompatible
// Returns true if at least one machine in the cluster has the right CPU type
// and GPU capability for this task, regardless of current load or memory.
// Used only to detect tasks that can never run anywhere — the only valid
// reason to permanently drop a task.
// ---------------------------------------------------------------------------
bool Scheduler::IsHardwareCompatible(TaskId_t task_id) const {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return false;
    const TaskState &task = task_it->second;

    for(const auto machine_id : machines) {
        const auto &machine = machine_views.at(machine_id);
        if(machine.cpu != task.required_cpu) continue;
        if(task.gpu_capable && !machine.gpu_enabled) continue;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// IsMachineFeasible
// A machine is feasible for a task if:
//   1. It has reached S0 and is not mid-transition.
//   2. Its CPU type matches.
//   3. It has GPU capability if the task requires it.
//   4. Projected memory usage fits within the physical capacity.
//   5. Projected load (tasks/cores) is within the SLA-appropriate cap.
//
// Memory cap is intentionally raw (no percentage headroom): the load cap is
// the binding constraint for SLA compliance.
// ---------------------------------------------------------------------------
bool Scheduler::IsMachineFeasible(TaskId_t task_id, MachineId_t machine_id,
                                   bool creating_vm) const {
    const auto machine_it = machine_views.find(machine_id);
    if(machine_it == machine_views.end()) return false;
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return false;

    const MachineStateView &machine = machine_it->second;
    const TaskState        &task    = task_it->second;

    if(machine.s_state != S0)              return false;
    if(machine.state_change_pending)       return false;
    if(machine.cpu != task.required_cpu)   return false;
    if(task.gpu_capable && !machine.gpu_enabled) return false;
    if(machine.memory_capacity == 0)       return false;

    // Memory: projected usage must not exceed physical capacity.
    const unsigned projected_mem = machine.tracked_memory_used
                                 + AdditionalPlacementMemory(task_id, creating_vm);
    if(projected_mem >= machine.memory_capacity) return false;

    // Load: tasks-per-core must stay below SLA-appropriate ceiling.
    if(machine.num_cores == 0) return false;
    const double projected_load = static_cast<double>(machine.active_task_count + 1)
                                / static_cast<double>(machine.num_cores);
    return projected_load <= MaxLoadForSLA(task.required_sla);
}

// ---------------------------------------------------------------------------
// HasReusableVM
// Looks up the per-machine VM index for a VM matching vm_type and cpu.
// O(small constant) since each machine has at most a handful of VMs.
// ---------------------------------------------------------------------------
bool Scheduler::HasReusableVM(MachineId_t machine_id, VMType_t vm_type,
                               CPUType_t cpu, VMId_t &out_vm_id) const {
    out_vm_id = VMId_t(-1);
    const auto it = machine_to_vms.find(machine_id);
    if(it == machine_to_vms.end()) return false;

    for(const VMId_t id : it->second) {
        const auto vm_it = vm_states.find(id);
        if(vm_it == vm_states.end()) continue;
        const VMState &vm = vm_it->second;
        if(vm.vm_type == vm_type && vm.cpu == cpu) {
            out_vm_id = id;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// CountActiveOrWakingMachines
// Returns the number of machines of the given CPU type that are either fully
// in S0, or currently transitioning TO S0 (wake command issued but not yet
// complete).  Used by ShutdownIdleMachines to enforce kMinS0MachinesPerCPU.
//
// Including pending-wake machines is critical: without it, a machine mid-wake
// looks like "not S0" and the floor check permits another shutdown to race
// alongside it, draining the cluster faster than the floor intends.
// ---------------------------------------------------------------------------
unsigned Scheduler::CountS0Machines(CPUType_t cpu) const {
    unsigned count = 0;
    for(const auto machine_id : machines) {
        const auto &view = machine_views.at(machine_id);
        if(view.cpu != cpu) continue;
        // Fully awake and stable.
        const bool is_s0 = (view.s_state == S0 && !view.state_change_pending);
        // Wake issued but transition not yet complete: s_state is still the
        // old sleep state, but state_change_pending is true.
        const bool waking_to_s0 = (view.s_state != S0 && view.state_change_pending);
        if(is_s0 || waking_to_s0) count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// WakeCompatibleMachine
// Finds a sleeping (non-S0, non-pending) machine matching the given CPU type
// and GPU requirement, issues Machine_SetState(S0), and marks it pending.
//
// KEY GUARD: before searching for a candidate, we check whether any machine
// of this CPU type already has a wake command in flight (state_change_pending
// while s_state != S0).  If so, we return false immediately.  This prevents
// multiple independent callers (separate TaskComplete events, PeriodicCheck,
// HandleMachineWake) from each issuing their own wake for the same CPU type
// in the same burst — the root cause of the thundering-herd crash.
// ---------------------------------------------------------------------------
bool Scheduler::WakeCompatibleMachine(CPUType_t cpu, bool need_gpu, Time_t now) {
    (void)now;

    // If any machine of this CPU type is already transitioning to S0, do not
    // issue another wake — one is already on its way.
    for(const auto machine_id : machines) {
        const auto &view = machine_views.at(machine_id);
        if(view.cpu != cpu) continue;
        if(view.s_state != S0 && view.state_change_pending) return false;
    }

    // No wake in flight — find the fastest compatible sleeping machine.
    for(const auto machine_id : sorted_machines) {
        auto &view = machine_views[machine_id];
        if(view.cpu != cpu) continue;
        if(need_gpu && !view.gpu_enabled) continue;
        if(view.s_state == S0) continue;           // already awake
        if(view.state_change_pending) continue;    // already waking (redundant safety)

        Machine_SetState(machine_id, S0);
        view.state_change_pending = true;
        machines_woken++;
        SimOutput("WakeCompatibleMachine(): issuing S0 to machine "
                  + to_string(machine_id), 2);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// ShutdownIdleMachines
// Conservative idle-shutdown pass.  Called from PeriodicCheck after
// refreshing simulator state and draining the retry queue.
//
// A machine is eligible for shutdown only when ALL of the following hold:
//   1. It is in S0 and not mid-transition.
//   2. It has zero active tasks (shadow state).
//   3. The retry queue is empty — no pending demand anywhere.
//   4. No queued retry task requires this CPU type (redundant if (3) holds,
//      kept as belt-and-suspenders).
//   5. It has been continuously idle for at least kIdleShutdownThreshold
//      sim-time units.
//   6. Shutting it down would leave at least kMinS0MachinesPerCPU other S0
//      machines of the same CPU type awake.
//
// At most kShutdownBatchCap machines are shut down per call to avoid
// oscillation and to give the retry queue time to react.
// ---------------------------------------------------------------------------
void Scheduler::ShutdownIdleMachines(Time_t now) {
    // Condition (3): bail immediately if anything is queued.
    if(!retry_queue.empty()) return;

    unsigned shut_this_call = 0;

    // Iterate slowest→fastest: prefer to keep fast machines awake for latency-
    // sensitive tasks that may arrive at any moment.
    const int n = static_cast<int>(sorted_machines.size());
    for(int idx = n - 1; idx >= 0 && shut_this_call < kShutdownBatchCap; idx--) {
        const MachineId_t machine_id = sorted_machines[idx];
        auto &view = machine_views[machine_id];

        // Conditions (1) & (2)
        if(view.s_state != S0)          continue;
        if(view.state_change_pending)   continue;
        if(view.active_task_count > 0)  continue;

        // Condition (5): must have been idle long enough.
        if(now - view.last_active_time < kIdleShutdownThreshold) continue;

        // Condition (6): always keep a minimum number of S0 machines per CPU type.
        if(CountS0Machines(view.cpu) <= kMinS0MachinesPerCPU) continue;

        // All conditions met — issue shutdown.
        Machine_SetState(machine_id, S1);   // S1: standby (low power, fast wake)
        view.state_change_pending = true;
        machines_shut_down++;
        shut_this_call++;
        SimOutput("ShutdownIdleMachines(): machine " + to_string(machine_id)
                  + " idle since " + to_string(view.last_active_time)
                  + " — transitioning to S1 at " + to_string(now), 2);
    }
}

// ---------------------------------------------------------------------------
// TryPlaceTask
//
// Placement direction is SLA-aware:
//   SLA0/SLA1 → iterate fast→slow (sorted_machines forward)
//               Ensures tight-SLA tasks land on the fastest available machine.
//   SLA2/SLA3 → iterate slow→fast (sorted_machines backward)
//               Passively reserves fast machines for high-priority work.
//
// Within each direction we take the first feasible machine (no score search)
// because the ordering itself encodes the preference.
//
// If no machine is feasible and IsHardwareCompatible returns false, the task
// is permanently dropped.  Otherwise the caller should enqueue it for retry.
//
// NEW: If placement fails due to capacity (not hardware), we proactively wake
// a sleeping machine so capacity will be available when the task is retried.
//
// Shadow state is updated synchronously on placement so subsequent calls
// within the same burst see accurate occupancy data.
// ---------------------------------------------------------------------------
bool Scheduler::TryPlaceTask(TaskId_t task_id) {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return false;

    TaskState &task = task_it->second;
    if(task.placement_failed || task.assigned || task.completed) return false;

    const CPUType_t  required_cpu = task.required_cpu;
    const VMType_t   required_vm  = task.required_vm;
    const Priority_t priority     = task.assigned_priority;
    const SLAType_t  required_sla = task.required_sla;

    // SLA0/1 prefer fast machines; SLA2/3 prefer slow machines to leave
    // fast machines free for time-sensitive work.
    const bool prefer_fast = (required_sla == SLA0 || required_sla == SLA1);
    const int  n           = static_cast<int>(sorted_machines.size());

    for(int idx = 0; idx < n; idx++) {
        const int         i          = prefer_fast ? idx : (n - 1 - idx);
        const MachineId_t machine_id = sorted_machines[i];

        VMId_t     vm_id       = VMId_t(-1);
        const bool has_reuse   = HasReusableVM(machine_id, required_vm, required_cpu, vm_id);
        const bool creating_vm = !has_reuse;

        if(!IsMachineFeasible(task_id, machine_id, creating_vm)) continue;

        // -- Create VM if needed ------------------------------------------
        if(creating_vm) {
            vm_id = VM_Create(required_vm, required_cpu);
            VM_Attach(vm_id, machine_id);
            vm_states[vm_id] = VMState{
                vm_id, machine_id, required_vm, required_cpu,
                VM_MEMORY_OVERHEAD
            };
            machine_to_vms[machine_id].push_back(vm_id);
            machine_views[machine_id].tracked_memory_used += VM_MEMORY_OVERHEAD;
            vms_created++;
        }

        // -- Assign task to VM --------------------------------------------
        VM_AddTask(vm_id, task_id, priority);
        vm_states[vm_id].tracked_memory_footprint += task.required_memory;
        machine_views[machine_id].tracked_memory_used += task.required_memory;
        machine_views[machine_id].active_task_count++;
        task.assigned    = true;
        task.assigned_vm = vm_id;
        successful_placements++;
        return true;
    }

    // No machine was feasible.  Permanently drop only if no machine could
    // ever host this task regardless of load.
    if(!IsHardwareCompatible(task_id)) {
        task.placement_failed = true;
        failed_task_ids.push_back(task_id);
        placement_failures++;
        SimOutput("TryPlaceTask(): task " + to_string(task_id)
                  + " has no compatible hardware — dropped", 0);
        return false;
    }

    // Capacity-constrained — caller will re-enqueue.  Wake commands are NOT
    // issued here; they are issued by ProcessRetryQueue as a single post-pass
    // step to guarantee at most one wake per CPU type per scheduling event,
    // regardless of how many tasks in the batch failed placement.
    return false;
}

// ---------------------------------------------------------------------------
// EnqueueRetry / ProcessRetryQueue
// ---------------------------------------------------------------------------
void Scheduler::EnqueueRetry(TaskId_t task_id) {
    retry_queue.push_back({task_id});
    retry_enqueues++;
}

// Processes up to kRetryBatchCap entries from the front of the retry queue.
// Tasks that succeed or are hardware-incompatible are removed permanently.
// All others are re-appended — they stay in the queue until a machine frees up.
//
// Wake policy: after the placement batch we collect every CPU type that still
// has unplaced tasks and issue AT MOST ONE Machine_SetState(S0) per CPU type.
// Doing this after the batch — rather than inside TryPlaceTask — means the
// number of wake commands is bounded by the number of distinct CPU types in
// the cluster (typically 2-4), not by the length of the retry queue.  This
// prevents the thundering-herd where N queued tasks of the same CPU type each
// trigger an independent wake, causing N machines to arrive at S0 in the same
// sim tick and each re-enter this function.
void Scheduler::ProcessRetryQueue(Time_t now) {
    const size_t batch = min(retry_queue.size(), static_cast<size_t>(kRetryBatchCap));

    // Track which (cpu_type, need_gpu) pairs still have unplaced tasks after
    // this pass so we can issue targeted wake commands below.
    // Encoded as (CPUType_t << 1 | gpu_bit) to fit in a plain int set.
    unordered_set<int> needs_capacity;

    for(size_t i = 0; i < batch; i++) {
        const RetryEntry entry = retry_queue.front();
        retry_queue.pop_front();

        const auto task_it = task_states.find(entry.task_id);
        if(task_it == task_states.end()) continue;

        const TaskState &task = task_it->second;
        if(task.placement_failed || task.completed || task.assigned) continue;

        retry_attempts++;

        if(TryPlaceTask(entry.task_id)) continue;

        // TryPlaceTask may have just set placement_failed (hardware-incompatible).
        if(task_it->second.placement_failed) continue;

        // Capacity-constrained — re-enqueue and record the need.
        task_it->second.retry_count++;
        if(task_it->second.retry_count == 50) {
            SimOutput("ProcessRetryQueue(): task " + to_string(entry.task_id)
                      + " has missed 50 placements at time " + to_string(now)
                      + " — still retrying", 1);
        }
        retry_queue.push_back(entry);

        // Record CPU+GPU key so we wake at most one machine per type below.
        const int key = (static_cast<int>(task.required_cpu) << 1)
                      | (task.gpu_capable ? 1 : 0);
        needs_capacity.insert(key);
    }

    // Post-pass: for each distinct CPU type that still has unplaced tasks,
    // wake exactly one sleeping machine (if any exists).  Because we do this
    // after the loop — not inside TryPlaceTask — the total number of new S0
    // commands issued per ProcessRetryQueue call is ≤ |needs_capacity|, which
    // is bounded by the number of distinct CPU+GPU combinations in the cluster.
    for(const int key : needs_capacity) {
        const CPUType_t cpu      = static_cast<CPUType_t>(key >> 1);
        const bool      need_gpu = (key & 1) != 0;
        WakeCompatibleMachine(cpu, need_gpu, now);
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void Scheduler::Init() {
    const unsigned total = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total machines = " + to_string(total), 3);
    SimOutput("Scheduler::Init(): Initializing", 1);

    // Reset all state.
    task_states.clear();
    vm_states.clear();
    machine_to_vms.clear();
    machine_views.clear();
    machines.clear();
    sorted_machines.clear();
    retry_queue.clear();
    failed_task_ids.clear();

    tasks_seen            = 0;
    tasks_completed       = 0;
    successful_placements = 0;
    retry_enqueues        = 0;
    retry_attempts        = 0;
    placement_failures    = 0;
    vms_created           = 0;
    machines_shut_down    = 0;
    machines_woken        = 0;

    InitializeMachineViews();

    // Wake every machine immediately.  Energy is not a concern for this
    // configuration — all capacity must be available before tasks arrive.
    for(const auto machine_id : machines) {
        auto &view = machine_views[machine_id];
        if(view.s_state != S0) {
            Machine_SetState(machine_id, S0);
            view.state_change_pending = true;
        }
    }

    SimOutput("Scheduler::Init(): all machines issued S0 wake command", 2);
}

// ---------------------------------------------------------------------------
// HandleMachineWake
// Called by StateChangeComplete whenever a machine finishes transitioning.
// Clears state_change_pending and drains the retry queue so tasks waiting on
// this machine are placed immediately.
//
// NOTE: This is called for BOTH wake (→S0) and sleep (→S1/S2/S3) completions.
// We inspect info.s_state to distinguish the two cases.  For sleep completions
// we clear state_change_pending and do nothing further; for S0 completions we
// drain the retry queue as before.
// ---------------------------------------------------------------------------
void Scheduler::HandleMachineWake(Time_t time, MachineId_t machine_id) {
    auto machine_it = machine_views.find(machine_id);
    if(machine_it == machine_views.end()) return;

    MachineStateView &machine = machine_it->second;
    machine.state_change_pending = false;

    const MachineInfo_t info = Machine_GetInfo(machine_id);
    machine.s_state             = info.s_state;
    machine.tracked_memory_used = info.memory_used;
    machine.active_task_count   = info.active_tasks;

    if(info.s_state != S0) {
        // Sleep transition completed normally.
        SimOutput("Scheduler::HandleMachineWake(): machine "
                  + to_string(machine_id) + " settled into state "
                  + to_string(info.s_state) + " at time " + to_string(time), 2);
        return;
    }

    // Machine reached S0 — it is freshly idle.
    machine.last_active_time = time;

    SimOutput("Scheduler::HandleMachineWake(): machine "
              + to_string(machine_id) + " is S0 at time "
              + to_string(time) + " — draining retry queue", 2);
    ProcessRetryQueue(time);
}

// ---------------------------------------------------------------------------
// MigrationComplete
// Migration is not initiated by this scheduler.  The callback is implemented
// to satisfy the interface in case the simulator invokes it anyway.
// ---------------------------------------------------------------------------
void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    (void)time;
    (void)vm_id;
}

// ---------------------------------------------------------------------------
// NewTask
// Registers task state and attempts immediate placement.
// NO RefreshMachineStatesFromSimulator here — shadow state accumulated by
// previous placements must not be overwritten by stale simulator data during
// a burst (tasks arriving 1000 time units apart cannot propagate to the
// simulator between arrivals).
// ---------------------------------------------------------------------------
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    (void)now;

    const CPUType_t  required_cpu = RequiredCPUType(task_id);
    const VMType_t   required_vm  = RequiredVMType(task_id);
    const SLAType_t  required_sla = RequiredSLA(task_id);
    const Priority_t priority     = PriorityFromSLA(required_sla);

    SetTaskPriority(task_id, priority);

    task_states[task_id] = TaskState{
        task_id,
        required_cpu,
        required_vm,
        required_sla,
        GetTaskMemory(task_id),
        IsTaskGPUCapable(task_id),
        priority,
        false,       // assigned
        false,       // completed
        VMId_t(-1),
        0,           // retry_count
        false        // placement_failed
    };

    tasks_seen++;

    if(!TryPlaceTask(task_id)) {
        EnqueueRetry(task_id);
    }
}

// ---------------------------------------------------------------------------
// PeriodicCheck
// The only place that syncs shadow state from the simulator.  All placement
// work is driven from here between bursts.
//
// Order matters:
//   1. Refresh — pull ground truth from simulator.
//   2. Retry   — place any waiting tasks (filling up machines before deciding
//                to shut any down).
//   3. Shutdown — only after the retry queue is empty, safely park idle
//                 machines.
// ---------------------------------------------------------------------------
void Scheduler::PeriodicCheck(Time_t now) {
    RefreshMachineStatesFromSimulator();
    ProcessRetryQueue(now);
    ShutdownIdleMachines(now);
}

// ---------------------------------------------------------------------------
// SLAWarn
// An SLA warning fires when a task is approaching its deadline.  With the
// load-cap placement strategy there is no migration to trigger; we use the
// warning only to attempt immediate placement of any queued tasks.
// ---------------------------------------------------------------------------
void Scheduler::SLAWarn(Time_t now, TaskId_t task_id) {
    (void)task_id;
    ProcessRetryQueue(now);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void Scheduler::Shutdown(Time_t time) {
    cout << "Scheduler report" << endl;
    cout << "Tasks seen:            " << tasks_seen            << endl;
    cout << "Tasks completed:       " << tasks_completed       << endl;
    cout << "Successful placements: " << successful_placements << endl;
    cout << "Retry enqueues:        " << retry_enqueues        << endl;
    cout << "Retry attempts:        " << retry_attempts        << endl;
    cout << "Placement failures:    " << placement_failures    << endl;
    cout << "VMs created:           " << vms_created           << endl;
    cout << "Machines shut down:    " << machines_shut_down    << endl;
    cout << "Machines woken:        " << machines_woken        << endl;
    cout << "Failed task IDs: ";
    if(failed_task_ids.empty()) {
        cout << "none";
    } else {
        for(size_t i = 0; i < failed_task_ids.size(); i++) {
            if(i > 0) cout << ",";
            cout << failed_task_ids[i];
        }
    }
    cout << endl;
    unsigned long long total_retry_misses = 0;
    for(const auto &kv : task_states) {
        total_retry_misses += kv.second.retry_count;
    }
    cout << "Total retry misses:    " << total_retry_misses << endl;
    cout << endl;

    for(auto &kv : vm_states) {
        const VMState &vm = kv.second;
        const auto machine_it = machine_views.find(vm.machine_id);
        if(machine_it == machine_views.end()) continue;
        if(machine_it->second.s_state != S0) continue;  // ← add this
        VM_Shutdown(kv.first);
    }
    SimOutput("SimulationComplete(): Finished at time " + to_string(time), 4);
}

// ---------------------------------------------------------------------------
// TaskComplete
// Updates shadow state synchronously — no simulator refresh, which would
// overwrite the decrements before the simulator commits the next placement.
// After updating shadow state, immediately attempts to place any queued tasks
// that were waiting for this capacity.
//
// NEW: When a machine's active_task_count drops to zero we record the current
// time as last_active_time so ShutdownIdleMachines can measure the idle
// duration accurately.
// ---------------------------------------------------------------------------
void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): task " + to_string(task_id)
              + " complete at " + to_string(now), 4);

    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return;
    if(task_it->second.completed) return;

    TaskState &task = task_it->second;
    task.completed = true;
    tasks_completed++;

    if(!task.assigned) return;

    const VMId_t vm_id = task.assigned_vm;
    const auto vm_it = vm_states.find(vm_id);
    if(vm_it == vm_states.end()) {
        task.assigned    = false;
        task.assigned_vm = VMId_t(-1);
        return;
    }

    VMState &vm = vm_it->second;

    // Decrement VM memory footprint.
    if(vm.tracked_memory_footprint >= task.required_memory) {
        vm.tracked_memory_footprint -= task.required_memory;
    } else {
        vm.tracked_memory_footprint = 0;
    }

    // Decrement machine shadow state.
    const auto machine_it = machine_views.find(vm.machine_id);
    if(machine_it != machine_views.end()) {
        MachineStateView &machine = machine_it->second;
        if(machine.active_task_count > 0) machine.active_task_count--;
        if(machine.tracked_memory_used >= task.required_memory) {
            machine.tracked_memory_used -= task.required_memory;
        } else {
            machine.tracked_memory_used = 0;
        }

        // Record the time this machine became idle so the shutdown heuristic
        // can measure how long it has been idle.
        if(machine.active_task_count == 0) {
            machine.last_active_time = now;
        }
    }

    task.assigned    = false;
    task.assigned_vm = VMId_t(-1);

    // Immediately attempt to place tasks that were waiting for this capacity.
    if(!retry_queue.empty()) {
        ProcessRetryQueue(now);
    }
}


// ---------------------------------------------------------------------------
// Public interface — thin wrappers
// ---------------------------------------------------------------------------

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): task " + to_string(task_id)
              + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): task " + to_string(task_id)
              + " at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): machine " + to_string(machine_id)
              + " at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationDone(): VM " + to_string(vm_id)
              + " at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    SimOutput("SchedulerCheck(): at time " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): at time " + to_string(time), 4);
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    Scheduler.SLAWarn(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    Scheduler.HandleMachineWake(time, machine_id);
}