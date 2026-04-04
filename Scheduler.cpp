//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

// =============================================================================
// SECTION 1 — Machine view initialisation
// =============================================================================

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
            info.performance[P0],
            info.memory_size,
            info.memory_used,
            info.gpus,
            info.s_state,
            info.active_tasks,
            false,        // state_change_pending
            info.s_state, // target_state
            1             // idle_since = 1 (idle at init; 0 = active sentinel)
        };
    }

    sorted_machines = machines;
    sort(sorted_machines.begin(), sorted_machines.end(),
         [this](MachineId_t a, MachineId_t b) {
             return machine_views[a].mips_p0 > machine_views[b].mips_p0;
         });
}

void Scheduler::ClassifyMachinesIntoTiers() {
    const unsigned total = static_cast<unsigned>(machines.size());
    if(total == 0) return;

    for(unsigned i = 0; i < total; i++) {
        const MachineId_t machine_id = sorted_machines[i];
        auto &view = machine_views[machine_id];
        machine_tier[machine_id] = MachineTier::Running;
        if(view.s_state != S0) {
            Machine_SetState(machine_id, S0);
            view.state_change_pending = true;
            view.target_state         = S0;
        }
    }
    SimOutput("ClassifyMachinesIntoTiers(): all "
              + to_string(total) + " machines targeted S0", 2);
}


// =============================================================================
// SECTION 2 — Shadow state sync
// =============================================================================

// Only s_state is refreshed from the simulator.  tracked_memory_used and
// active_task_count are maintained exclusively by TryPlaceTask and TaskComplete
// to avoid stale-overwrite bugs when PeriodicCheck and StateChangeComplete
// fire at the same simulation timestamp.
void Scheduler::RefreshMachineStatesFromSimulator() {
    for(const auto machine_id : machines) {
        const MachineInfo_t info = Machine_GetInfo(machine_id);
        machine_views[machine_id].s_state = info.s_state;
    }
}


// =============================================================================
// SECTION 3 — Placement helpers
// =============================================================================

Priority_t Scheduler::PriorityFromSLA(SLAType_t sla) const {
    switch(sla) {
        case SLA0:  return HIGH_PRIORITY;
        case SLA1:  return MID_PRIORITY;
        case SLA2:  return LOW_PRIORITY;
        case SLA3:
        default:    return LOW_PRIORITY;
    }
}

unsigned Scheduler::AdditionalPlacementMemory(TaskId_t task_id, bool creating_vm) const {
    const auto it = task_states.find(task_id);
    if(it == task_states.end()) return 0;
    unsigned extra = it->second.required_memory;
    if(creating_vm) extra += VM_MEMORY_OVERHEAD;
    return extra;
}

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

bool Scheduler::IsMachineFeasible(TaskId_t task_id, MachineId_t machine_id,
                                   bool creating_vm) const {
    const auto machine_it = machine_views.find(machine_id);
    if(machine_it == machine_views.end()) return false;
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return false;

    const MachineStateView &machine = machine_it->second;
    const TaskState        &task    = task_it->second;

    if(machine.s_state != S0)                    return false;
    if(machine.state_change_pending)             return false;
    if(machine.cpu != task.required_cpu)         return false;
    if(task.gpu_capable && !machine.gpu_enabled) return false;
    if(machine.memory_capacity == 0)             return false;

    const unsigned projected_mem = machine.tracked_memory_used
                                 + AdditionalPlacementMemory(task_id, creating_vm);
    if(projected_mem >= machine.memory_capacity) return false;

    if(machine.num_cores == 0) return false;
    const double projected_load = static_cast<double>(machine.active_task_count + 1)
                                / static_cast<double>(machine.num_cores);
    return projected_load <= kMaxLoadPerCore;
}

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

// O(M_t): true if any S0 non-pending machine of this type has a free core.
bool Scheduler::HasCapacityForType(CPUType_t cpu, bool gpu) const {
    for(const auto machine_id : machines) {
        const auto it = machine_views.find(machine_id);
        if(it == machine_views.end()) continue;
        const MachineStateView &m = it->second;
        if(m.s_state != S0 || m.state_change_pending) continue;
        if(m.cpu != cpu)                             continue;
        if(gpu && !m.gpu_enabled)                    continue;
        if(m.num_cores == 0)                         continue;
        if(m.active_task_count < m.num_cores)        return true;
    }
    return false;
}

// O(M_t): total free core slots across all S0 non-pending machines of this type.
unsigned Scheduler::FreeSlotCountForType(CPUType_t cpu, bool gpu) const {
    unsigned free = 0;
    for(const auto machine_id : machines) {
        const auto it = machine_views.find(machine_id);
        if(it == machine_views.end()) continue;
        const MachineStateView &m = it->second;
        if(m.s_state != S0 || m.state_change_pending) continue;
        if(m.cpu != cpu)                              continue;
        if(gpu && !m.gpu_enabled)                     continue;
        if(m.num_cores == 0)                          continue;
        if(m.active_task_count < m.num_cores)
            free += m.num_cores - m.active_task_count;
    }
    return free;
}


// =============================================================================
// SECTION 4 — E-Eco tier controller
// =============================================================================

unsigned Scheduler::ComputeDesiredRunning() const {
    unsigned total_active  = 0;
    unsigned total_cores   = 0;
    unsigned running_count = 0;

    for(const auto machine_id : machines) {
        const auto tier_it = machine_tier.find(machine_id);
        if(tier_it == machine_tier.end()) continue;
        if(tier_it->second != MachineTier::Running) continue;

        const auto view_it = machine_views.find(machine_id);
        if(view_it == machine_views.end()) continue;

        total_active  += view_it->second.active_task_count;
        total_cores   += view_it->second.num_cores;
        running_count++;
    }

    if(running_count == 0) return kMinRunningMachines;

    const unsigned avg_cores = max(1u, total_cores / running_count);
    const double capacity_per_machine =
        kTargetLoadPerMachine * static_cast<double>(avg_cores);
    if(capacity_per_machine <= 0.0) return kMinRunningMachines;

    // Include queued SLA0/1 tasks in demand so desired_running stays
    // appropriately high while high-priority work is pending.
    const unsigned total_demand = total_active + queued_sla0_count + queued_sla1_count;
    const unsigned desired = static_cast<unsigned>(
        ceil(static_cast<double>(total_demand) / capacity_per_machine));

    return max(kMinRunningMachines, desired);
}

unsigned Scheduler::ComputeDesiredIntermediate(unsigned running_count) const {
    const unsigned desired = static_cast<unsigned>(
        ceil(static_cast<double>(running_count) * kIntermediateBufferRatio));
    return max(kMinIntermediateMachines, desired);
}

void Scheduler::AdjustTiers(Time_t now) {
    unsigned current_running      = 0;
    unsigned current_intermediate = 0;

    for(const auto machine_id : machines) {
        const auto it = machine_tier.find(machine_id);
        if(it == machine_tier.end()) continue;
        switch(it->second) {
            case MachineTier::Running:      current_running++;      break;
            case MachineTier::Intermediate: current_intermediate++; break;
            default: break;
        }
    }

    const unsigned desired_running = min(
        ComputeDesiredRunning(),
        static_cast<unsigned>(machines.size()));
    const unsigned desired_intermediate = ComputeDesiredIntermediate(desired_running);

    // Promote Intermediate → Running (fast machines first).
    if(current_running < desired_running) {
        for(const auto machine_id : sorted_machines) {
            if(current_running >= desired_running) break;

            const auto tier_it = machine_tier.find(machine_id);
            if(tier_it == machine_tier.end()) continue;
            if(tier_it->second != MachineTier::Intermediate) continue;

            auto &view = machine_views[machine_id];
            if(view.state_change_pending) continue;

            Machine_SetState(machine_id, S0);
            view.state_change_pending = true;
            view.target_state         = S0;
            machine_tier[machine_id]  = MachineTier::Running;
            current_running++;
            tier_promotions++;
            SimOutput("AdjustTiers(): promoted machine " + to_string(machine_id)
                      + " Intermediate→Running", 2);
        }
    }

    // Demote idle Running → Intermediate.
    // Guards: warmup elapsed, retry queue empty, floor, idle duration.
    const bool warmup_done = (now > kWarmupPeriod);
    const bool queue_empty = !AnyRetryQueued();

    if(warmup_done && queue_empty && current_running > desired_running) {
        for(int i = static_cast<int>(sorted_machines.size()) - 1; i >= 0; i--) {
            if(current_running <= desired_running)     break;
            if(current_running <= kMinRunningMachines) break;

            const MachineId_t machine_id = sorted_machines[i];
            const auto tier_it = machine_tier.find(machine_id);
            if(tier_it == machine_tier.end()) continue;
            if(tier_it->second != MachineTier::Running) continue;

            auto &view = machine_views[machine_id];
            if(view.active_task_count > 0) continue;
            if(view.state_change_pending)  continue;
            if(view.idle_since == 0)       continue;
            if((now - view.idle_since) < kMinIdleBeforeDemotion) continue;

            Machine_SetState(machine_id, S1);
            view.state_change_pending = true;
            view.target_state         = S1;
            machine_tier[machine_id]  = MachineTier::Intermediate;
            current_running--;
            current_intermediate++;
            tier_demotions++;
            SimOutput("AdjustTiers(): demoted machine " + to_string(machine_id)
                      + " Running→Intermediate (idle "
                      + to_string(now - view.idle_since) + " tu)", 2);
        }
    }

    (void)current_intermediate;
    (void)desired_intermediate;
}

// Immediate reactive promotion for SLA0/SLA1 task placement failures.
//
// KEY OPTIMISATION: Before scanning for an Intermediate machine to promote,
// check whether any machine of the required type is already transitioning
// toward S0 (state_change_pending && target_state == S0).  If one is already
// on its way, it will drain the typed queue when it confirms S0 — issuing
// another Machine_SetState is unnecessary and adds noise.
//
// This deduplication is general: in any burst where N tasks fail placement
// for the same hardware type, only the first task needs to trigger a promotion.
// Subsequent tasks see the pending transition and return immediately, costing
// O(M_t) instead of O(M_t) × N.
void Scheduler::PromoteForTask(TaskId_t task_id) {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return;

    const TaskState &task = task_it->second;
    if(task.required_sla != SLA0 && task.required_sla != SLA1) return;

    // Check: is a machine of this type already being promoted to S0?
    for(const auto machine_id : sorted_machines) {
        const auto view_it = machine_views.find(machine_id);
        if(view_it == machine_views.end()) continue;
        const MachineStateView &view = view_it->second;
        if(view.cpu != task.required_cpu)             continue;
        if(task.gpu_capable && !view.gpu_enabled)     continue;
        if(view.state_change_pending && view.target_state == S0) {
            // Already a machine of this type heading to S0.
            // It will drain the typed queue on confirmation.
            return;
        }
    }

    // No machine is already transitioning — find an Intermediate one to promote.
    for(const auto machine_id : sorted_machines) {
        const auto tier_it = machine_tier.find(machine_id);
        if(tier_it == machine_tier.end()) continue;
        if(tier_it->second != MachineTier::Intermediate) continue;

        auto &view = machine_views[machine_id];
        if(view.state_change_pending)             continue;
        if(view.cpu != task.required_cpu)         continue;
        if(task.gpu_capable && !view.gpu_enabled) continue;

        Machine_SetState(machine_id, S0);
        view.state_change_pending = true;
        view.target_state         = S0;
        machine_tier[machine_id]  = MachineTier::Running;
        tier_promotions++;
        SimOutput("PromoteForTask(): Intermediate→Running machine "
                  + to_string(machine_id) + " for SLA"
                  + to_string(static_cast<int>(task.required_sla))
                  + " task " + to_string(task_id), 2);
        return;
    }

    // No Intermediate machine available and none already transitioning.
    // AdjustTiers' desired_running calculation will promote on next tick
    // if demand warrants it (queued SLA0/1 counts feed ComputeDesiredRunning).
    SimOutput("PromoteForTask(): no Intermediate machine available for task "
              + to_string(task_id), 3);
}


// =============================================================================
// SECTION 5 — Placement and typed retry queues
// =============================================================================

bool Scheduler::TryPlaceTask(TaskId_t task_id) {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return false;

    TaskState &task = task_it->second;
    if(task.placement_failed || task.assigned || task.completed) return false;

    const CPUType_t  required_cpu = task.required_cpu;
    const VMType_t   required_vm  = task.required_vm;
    const Priority_t priority     = task.assigned_priority;
    const SLAType_t  required_sla = task.required_sla;

    const bool prefer_fast = (required_sla == SLA0 || required_sla == SLA1);
    const int  n           = static_cast<int>(sorted_machines.size());

    for(int idx = 0; idx < n; idx++) {
        const int         i          = prefer_fast ? idx : (n - 1 - idx);
        const MachineId_t machine_id = sorted_machines[i];

        VMId_t     vm_id       = VMId_t(-1);
        const bool has_reuse   = HasReusableVM(machine_id, required_vm, required_cpu, vm_id);
        const bool creating_vm = !has_reuse;

        if(!IsMachineFeasible(task_id, machine_id, creating_vm)) continue;

        if(creating_vm) {
            vm_id = VM_Create(required_vm, required_cpu);
            VM_Attach(vm_id, machine_id);
            vm_states[vm_id] = VMState{
                vm_id, machine_id, required_vm, required_cpu, VM_MEMORY_OVERHEAD
            };
            machine_to_vms[machine_id].push_back(vm_id);
            machine_views[machine_id].tracked_memory_used += VM_MEMORY_OVERHEAD;
            vms_created++;
        }

        VM_AddTask(vm_id, task_id, priority);
        vm_states[vm_id].tracked_memory_footprint    += task.required_memory;
        machine_views[machine_id].tracked_memory_used += task.required_memory;
        machine_views[machine_id].idle_since           = 0;
        machine_views[machine_id].active_task_count++;
        task.assigned    = true;
        task.assigned_vm = vm_id;
        successful_placements++;
        return true;
    }

    if(!IsHardwareCompatible(task_id)) {
        task.placement_failed = true;
        failed_task_ids.push_back(task_id);
        placement_failures++;
        SimOutput("TryPlaceTask(): task " + to_string(task_id)
                  + " has no compatible hardware — permanently dropped", 0);
    }
    return false;
}

void Scheduler::EnqueueRetry(TaskId_t task_id) {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return;

    const TaskState &task = task_it->second;
    const uint32_t key = HardwareKey(task.required_cpu, task.gpu_capable);

    typed_retry_queues[key].push_back({task_id});
    total_queued++;
    retry_enqueues++;

    switch(task.required_sla) {
        case SLA0: queued_sla0_count++; break;
        case SLA1: queued_sla1_count++; break;
        default:   break;
    }
}

void Scheduler::DecrementQueuedSLACount(TaskId_t task_id) {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) return;
    switch(task_it->second.required_sla) {
        case SLA0: if(queued_sla0_count > 0) queued_sla0_count--; break;
        case SLA1: if(queued_sla1_count > 0) queued_sla1_count--; break;
        default:   break;
    }
}

// Drain the typed queue for (cpu, gpu) up to max_attempts entries.
//
// Pre-check: HasCapacityForType is O(M_t).  When all machines of this type
// are saturated it exits immediately without touching the queue — turning
// what was O(batch × M_t) wasted work into O(M_t).
//
// Loop bound: max_attempts = FreeSlotCountForType ensures we never attempt
// more placements than there are slots to fill, even accounting for the fact
// that a single freed slot may enable multiple tasks (e.g. after a large
// memory task completes, multiple small tasks may fit).
void Scheduler::ProcessRetryQueueForType(Time_t now, CPUType_t cpu, bool gpu,
                                          unsigned max_attempts) {
    if(!HasCapacityForType(cpu, gpu)) return;

    const uint32_t key = HardwareKey(cpu, gpu);
    const auto it = typed_retry_queues.find(key);
    if(it == typed_retry_queues.end()) return;

    auto &queue = it->second;
    if(queue.empty()) return;

    const size_t batch  = min(queue.size(), static_cast<size_t>(max_attempts));
    unsigned     placed = 0;

    for(size_t i = 0; i < batch; i++) {
        if(queue.empty())          break;
        if(placed >= max_attempts) break;

        const RetryEntry entry = queue.front();
        queue.pop_front();

        const auto task_it = task_states.find(entry.task_id);
        if(task_it == task_states.end()) {
            if(total_queued > 0) total_queued--;
            continue;
        }

        const TaskState &task = task_it->second;

        if(task.placement_failed || task.completed || task.assigned) {
            DecrementQueuedSLACount(entry.task_id);
            if(total_queued > 0) total_queued--;
            continue;
        }

        retry_attempts++;

        if(TryPlaceTask(entry.task_id)) {
            DecrementQueuedSLACount(entry.task_id);
            if(total_queued > 0) total_queued--;
            placed++;
            continue;
        }

        if(task_it->second.placement_failed) {
            DecrementQueuedSLACount(entry.task_id);
            if(total_queued > 0) total_queued--;
            continue;
        }

        // Still capacity-constrained (e.g. memory fits no machine) — re-enqueue.
        task_it->second.retry_count++;
        if(task_it->second.retry_count == 50) {
            SimOutput("ProcessRetryQueue(): task " + to_string(entry.task_id)
                      + " has missed 50 placements at time " + to_string(now)
                      + " — still retrying", 1);
        }
        queue.push_back(entry);
    }
}

// Catch-all: drain all non-empty typed queues bounded by their free slot count.
// Called from PeriodicCheck and SLAWarn.
void Scheduler::ProcessAllRetryQueues(Time_t now) {
    vector<uint32_t> keys;
    keys.reserve(typed_retry_queues.size());
    for(const auto &kv : typed_retry_queues) {
        if(!kv.second.empty()) keys.push_back(kv.first);
    }
    for(const uint32_t key : keys) {
        const CPUType_t cpu = static_cast<CPUType_t>(key >> 1);
        const bool      gpu = (key & 1u) != 0;
        const unsigned  free = FreeSlotCountForType(cpu, gpu);
        if(free == 0) continue;
        ProcessRetryQueueForType(now, cpu, gpu, free);
    }
}


// =============================================================================
// SECTION 6 — Scheduler lifecycle
// =============================================================================

void Scheduler::Init() {
    const unsigned total = Machine_GetTotal();
    SimOutput("Scheduler::Init(): total machines = " + to_string(total), 3);
    SimOutput("Scheduler::Init(): initializing E-Eco scheduler", 1);

    task_states.clear();
    vm_states.clear();
    machine_to_vms.clear();
    machine_views.clear();
    machine_tier.clear();
    machines.clear();
    sorted_machines.clear();
    typed_retry_queues.clear();
    failed_task_ids.clear();

    total_queued          = 0;
    tasks_seen            = 0;
    tasks_completed       = 0;
    successful_placements = 0;
    retry_enqueues        = 0;
    retry_attempts        = 0;
    placement_failures    = 0;
    vms_created           = 0;
    tier_promotions       = 0;
    tier_demotions        = 0;
    queued_sla0_count     = 0;
    queued_sla1_count     = 0;

    InitializeMachineViews();
    ClassifyMachinesIntoTiers();
}

void Scheduler::HandleMachineWake(Time_t time, MachineId_t machine_id) {
    auto machine_it = machine_views.find(machine_id);
    if(machine_it == machine_views.end()) return;

    MachineStateView &machine = machine_it->second;

    const MachineInfo_t info = Machine_GetInfo(machine_id);
    machine.s_state = info.s_state;

    if(info.s_state != machine.target_state) {
        SimOutput("HandleMachineWake(): machine " + to_string(machine_id)
                  + " still transitioning to S"
                  + to_string(machine.target_state), 3);
        return;
    }

    machine.state_change_pending = false;

    if(info.s_state == S0) {
        machine_tier[machine_id] = MachineTier::Running;
        if(machine.active_task_count == 0) {
            machine.idle_since = time;
        }
        SimOutput("HandleMachineWake(): machine " + to_string(machine_id)
                  + " confirmed S0 at time " + to_string(time), 2);

        // Drain typed queues for this machine's hardware class, bounded by
        // actual free slots across all machines of this type.
        const unsigned free_non_gpu = FreeSlotCountForType(machine.cpu, false);
        if(free_non_gpu > 0)
            ProcessRetryQueueForType(time, machine.cpu, false, free_non_gpu);

        if(machine.gpu_enabled) {
            const unsigned free_gpu = FreeSlotCountForType(machine.cpu, true);
            if(free_gpu > 0)
                ProcessRetryQueueForType(time, machine.cpu, true, free_gpu);
        }

    } else if(info.s_state == S1 || info.s_state == S2) {
        machine_tier[machine_id] = MachineTier::Intermediate;
        SimOutput("HandleMachineWake(): machine " + to_string(machine_id)
                  + " confirmed Intermediate at time " + to_string(time), 3);

    } else {
        machine_tier[machine_id] = MachineTier::SwitchedOff;
    }
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    (void)time;
    (void)vm_id;
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    (void)now;

    const CPUType_t  required_cpu = RequiredCPUType(task_id);
    const VMType_t   required_vm  = RequiredVMType(task_id);
    const SLAType_t  required_sla = RequiredSLA(task_id);
    const Priority_t priority     = PriorityFromSLA(required_sla);

    SetTaskPriority(task_id, priority);

    task_states[task_id] = TaskState{
        task_id, required_cpu, required_vm, required_sla,
        GetTaskMemory(task_id), IsTaskGPUCapable(task_id), priority,
        false, false, VMId_t(-1), 0, false
    };

    tasks_seen++;

    if(!TryPlaceTask(task_id)) {
        PromoteForTask(task_id);
        EnqueueRetry(task_id);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    RefreshMachineStatesFromSimulator();
    AdjustTiers(now);
    ProcessAllRetryQueues(now);
}

void Scheduler::SLAWarn(Time_t now, TaskId_t task_id) {
    (void)task_id;
    ProcessAllRetryQueues(now);
}

void Scheduler::Shutdown(Time_t time) {
    unsigned final_running = 0, final_intermediate = 0, final_off = 0;
    for(const auto machine_id : machines) {
        const auto it = machine_tier.find(machine_id);
        if(it == machine_tier.end()) continue;
        switch(it->second) {
            case MachineTier::Running:      final_running++;      break;
            case MachineTier::Intermediate: final_intermediate++; break;
            case MachineTier::SwitchedOff:  final_off++;          break;
        }
    }

    cout << "E-Eco Scheduler report" << endl;
    cout << "Tasks seen:              " << tasks_seen            << endl;
    cout << "Tasks completed:         " << tasks_completed       << endl;
    cout << "Successful placements:   " << successful_placements << endl;
    cout << "Retry enqueues:          " << retry_enqueues        << endl;
    cout << "Retry attempts:          " << retry_attempts        << endl;
    cout << "Placement failures:      " << placement_failures    << endl;
    cout << "VMs created:             " << vms_created           << endl;
    cout << "Tier promotions:         " << tier_promotions       << endl;
    cout << "Tier demotions:          " << tier_demotions        << endl;
    cout << "Final tier — Running: "    << final_running
         << " Intermediate: "           << final_intermediate
         << " Off: "                    << final_off             << endl;
    cout << "SLA0 tasks still queued: " << queued_sla0_count     << endl;
    cout << "SLA1 tasks still queued: " << queued_sla1_count     << endl;
    cout << "Total still queued:      " << total_queued          << endl;
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

    for(auto &kv : vm_states) {
        const VMState &vm = kv.second;
        const auto machine_it = machine_views.find(vm.machine_id);
        if(machine_it == machine_views.end()) continue;
        if(machine_it->second.s_state != S0) continue;
        VM_Shutdown(kv.first);
    }
    SimOutput("SimulationComplete(): finished at time " + to_string(time), 4);
}

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

    VMState    &vm            = vm_it->second;
    MachineId_t freed_machine = vm.machine_id;
    CPUType_t   freed_cpu     = X86;
    bool        freed_gpu     = false;

    if(vm.tracked_memory_footprint >= task.required_memory) {
        vm.tracked_memory_footprint -= task.required_memory;
    } else {
        vm.tracked_memory_footprint = 0;
    }

    const auto machine_it = machine_views.find(freed_machine);
    if(machine_it != machine_views.end()) {
        MachineStateView &machine = machine_it->second;
        freed_cpu = machine.cpu;
        freed_gpu = machine.gpu_enabled;

        if(machine.active_task_count > 0) machine.active_task_count--;
        if(machine.tracked_memory_used >= task.required_memory) {
            machine.tracked_memory_used -= task.required_memory;
        } else {
            machine.tracked_memory_used = 0;
        }
        if(machine.active_task_count == 0) {
            machine.idle_since = now;
        }
    }

    task.assigned    = false;
    task.assigned_vm = VMId_t(-1);

    if(AnyRetryQueued()) {
        const unsigned free_non_gpu = FreeSlotCountForType(freed_cpu, false);
        if(free_non_gpu > 0)
            ProcessRetryQueueForType(now, freed_cpu, false, free_non_gpu);

        if(freed_gpu) {
            const unsigned free_gpu = FreeSlotCountForType(freed_cpu, true);
            if(free_gpu > 0)
                ProcessRetryQueueForType(now, freed_cpu, true, free_gpu);
        }
    }
}


// =============================================================================
// SECTION 7 — Public interface
// =============================================================================

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): initializing", 4);
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