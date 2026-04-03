//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <limits>

void Scheduler::InitializeMachineViews() {
    machines.clear();
    machine_views.clear();

    const unsigned total_machines = Machine_GetTotal();
    for(unsigned i = 0; i < total_machines; i++) {
        const MachineId_t machine_id = MachineId_t(i);
        const MachineInfo_t info = Machine_GetInfo(machine_id);

        machines.push_back(machine_id);
        machine_views[machine_id] = MachineStateView{
            machine_id,
            info.cpu,
            info.memory_size,
            info.memory_used,
            info.gpus,
            info.s_state,
            {},
            info.active_tasks,
            false
        };
    }
}

void Scheduler::RefreshMachineStatesFromSimulator() {
    for(auto machine_id : machines) {
        const MachineInfo_t info = Machine_GetInfo(machine_id);
        auto &view = machine_views[machine_id];
        view.s_state = info.s_state;
        view.tracked_memory_used = info.memory_used;
        view.active_task_count = info.active_tasks;
    }
}

/*
* Helper method to convert from SLA type to a priority level. 
*/
Priority_t Scheduler::PriorityFromSLA(SLAType_t sla) const {
    switch(sla) {
        case SLA0:
            return HIGH_PRIORITY;
        case SLA1:
            return MID_PRIORITY;
        case SLA2:
            return LOW_PRIORITY;
        case SLA3:
        default:
            return LOW_PRIORITY;
    }
}


/*
* Helper method to calculate additional memory needed for task placement if new VM.
*/
unsigned Scheduler::AdditionalPlacementMemory(TaskId_t task_id, bool creating_vm) const {
    unsigned extra = GetTaskMemory(task_id);
    if(creating_vm) {
        extra += VM_MEMORY_OVERHEAD;
    }
    return extra;
}

/* Logic for if a machine can accommodate a task */
bool Scheduler::IsMachineFeasible(TaskId_t task_id, MachineId_t machine_id, bool creating_vm) const {
    const auto machine_it = machine_views.find(machine_id);
    if(machine_it == machine_views.end()) {
        return false;
    }

    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) {
        return false;
    }

    const MachineStateView &machine = machine_it->second;
    const TaskState &task = task_it->second;

    if(machine.s_state != S0) {
        return false;
    }

    if(machine.cpu != task.required_cpu) {
        return false;
    }

    if(machine.memory_capacity == 0) {
        return false;
    }

    const unsigned projected_used = machine.tracked_memory_used + AdditionalPlacementMemory(task_id, creating_vm);
    const double projected_utilization = static_cast<double>(projected_used) / static_cast<double>(machine.memory_capacity);
    return projected_utilization < kCapacityCap;
}

bool Scheduler::HasReusableVM(MachineId_t machine_id, VMType_t required_vm, CPUType_t required_cpu, VMId_t &vm_id) const {
    vm_id = VMId_t(-1);
    for(const auto &kv : vm_states) {
        const VMState &vm_state = kv.second;
        if(vm_state.machine_id == machine_id && vm_state.cpu == required_cpu && vm_state.vm_type == required_vm && !vm_state.migrating) {
            vm_id = kv.first;
            return true;
        }
    }
    return false;
}

/*
* Mark task as at risk if only has 20% or less of its window before SLA violation.
*/
bool Scheduler::IsTaskAtRisk(TaskId_t task_id, Time_t now) const {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) {
        return false;
    }

    const TaskState &state = task_it->second;
    if(state.completed || !state.assigned || state.required_sla == SLA3) {
        return false;
    }

    const TaskInfo_t info = GetTaskInfo(task_id);
    if(info.completed || info.target_completion <= info.arrival) {
        return false;
    }

    if(now >= info.target_completion) {
        return true;
    }

    const uint64_t window = info.target_completion - info.arrival;
    const uint64_t remaining = info.target_completion - now;
    return (remaining * 100ULL) <= (window * 20ULL);
}

void Scheduler::RefreshProtectedMachines(Time_t now) {
    protected_machines.clear();

    for(const auto &kv : task_states) {
        const TaskId_t task_id = kv.first;
        const TaskState &state = kv.second;
        if(!state.assigned || state.completed) {
            continue;
        }

        if(!IsTaskAtRisk(task_id, now)) {
            continue;
        }

        const auto vm_it = vm_states.find(state.assigned_vm);
        if(vm_it == vm_states.end()) {
            continue;
        }

        protected_machines.insert(vm_it->second.machine_id);
        at_risk_tasks_detected++;
    }
}

bool Scheduler::FindBestMigrationTarget(VMId_t vm_id, MachineId_t &target_machine_id) const {
    const auto vm_it = vm_states.find(vm_id);
    if(vm_it == vm_states.end()) {
        return false;
    }

    const VMState &vm_state = vm_it->second;
    const auto source_machine_it = machine_views.find(vm_state.machine_id);
    if(source_machine_it == machine_views.end()) {
        return false;
    }

    const unsigned migration_footprint = vm_state.tracked_memory_footprint;
    if(migration_footprint == 0) {
        return false;
    }

    double best_utilization = std::numeric_limits<double>::max();
    bool found = false;
    MachineId_t best_machine = vm_state.machine_id;

    for(const auto machine_id : machines) {
        if(machine_id == vm_state.machine_id) {
            continue;
        }

        const auto machine_it = machine_views.find(machine_id);
        if(machine_it == machine_views.end()) {
            continue;
        }

        const MachineStateView &machine = machine_it->second;
        if(machine.s_state != S0 || machine.cpu != vm_state.cpu || machine.memory_capacity == 0) {
            continue;
        }

        const unsigned projected_used = machine.tracked_memory_used + migration_footprint;
        const double projected_utilization = static_cast<double>(projected_used) / static_cast<double>(machine.memory_capacity);
        if(projected_utilization >= kCapacityCap) {
            continue;
        }

        // Prefer targets that are not currently protected to avoid shifting pressure onto hot hosts.
        const bool target_protected = (protected_machines.find(machine_id) != protected_machines.end());
        const bool best_protected = found ? (protected_machines.find(best_machine) != protected_machines.end()) : true;
        if(found && target_protected != best_protected) {
            if(target_protected) {
                continue;
            }
        }

        if(!found || projected_utilization < best_utilization || (projected_utilization == best_utilization && machine_id < best_machine)) {
            found = true;
            best_utilization = projected_utilization;
            best_machine = machine_id;
        }
    }

    if(!found) {
        return false;
    }

    target_machine_id = best_machine;
    return true;
}

bool Scheduler::TryMigrateAtRiskTask(TaskId_t task_id, Time_t now, bool allow_wake) {
    auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) {
        return false;
    }

    TaskState &task = task_it->second;
    if(task.completed || !task.assigned || task.required_sla != SLA0) {
        return false;
    }

    if(!IsTaskAtRisk(task_id, now)) {
        return false;
    }

    auto vm_it = vm_states.find(task.assigned_vm);
    if(vm_it == vm_states.end()) {
        return false;
    }

    VMState &vm_state = vm_it->second;
    if(vm_state.migrating) {
        return false;
    }

    const auto cooldown_it = vm_last_migration_time.find(vm_state.vm_id);
    if(cooldown_it != vm_last_migration_time.end() && now < cooldown_it->second + kMigrationCooldown) {
        return false;
    }

    MachineId_t target_machine_id = vm_state.machine_id;
    if(FindBestMigrationTarget(vm_state.vm_id, target_machine_id)) {
        VM_Migrate(vm_state.vm_id, target_machine_id);
        vm_state.migrating = true;
        vm_last_migration_time[vm_state.vm_id] = now;
        migrations++;
        return true;
    }

    if(!allow_wake) {
        return false;
    }

    // If no active target exists, wake a compatible standby machine to absorb imminent SLA pressure.
    for(const auto machine_id : machines) {
        auto machine_it = machine_views.find(machine_id);
        if(machine_it == machine_views.end()) {
            continue;
        }

        MachineStateView &machine = machine_it->second;
        if(machine.cpu != vm_state.cpu || machine.s_state == S0) {
            continue;
        }

        Machine_SetState(machine_id, S0);
        machine.state_change_pending = true;
        wakeups++;
        return true;
    }

    return false;
}

void Scheduler::ProcessAtRiskMigrations(Time_t now) {
    unsigned migrations_this_tick = 0;
    for(const auto &kv : task_states) {
        if(migrations_this_tick >= kMaxMigrationsPerTick) {
            break;
        }

        const TaskId_t task_id = kv.first;
        if(TryMigrateAtRiskTask(task_id, now, true)) {
            migrations_this_tick++;
        }
    }
}

bool Scheduler::TryPlaceTask(TaskId_t task_id) {
    const auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) {
        return false;
    }

    if(task_it->second.placement_failed || task_it->second.assigned || task_it->second.completed) {
        return false;
    }

    const CPUType_t required_cpu = task_it->second.required_cpu;
    const VMType_t required_vm = task_it->second.required_vm;
    const Priority_t priority = task_it->second.assigned_priority;

    // More frequent refresh keeps placement decisions aligned with simulator-side changes.
    RefreshMachineStatesFromSimulator();

    bool skipped_any_protected_machine = false;
    auto try_pass = [&](bool allow_protected_hosts) -> bool {
        for(const auto machine_id : machines) {
            if(!allow_protected_hosts && protected_machines.find(machine_id) != protected_machines.end()) {
                skipped_any_protected_machine = true;
                protected_machine_skips++;
                continue;
            }

        VMId_t vm_id = VMId_t(-1);
        const bool has_reusable_vm = HasReusableVM(machine_id, required_vm, required_cpu, vm_id);
        const bool creating_vm = !has_reusable_vm;

        if(!IsMachineFeasible(task_id, machine_id, creating_vm)) {
            continue;
        }

        if(!has_reusable_vm) {
            vm_id = VM_Create(required_vm, required_cpu);
            VM_Attach(vm_id, machine_id);
            vm_states[vm_id] = VMState{vm_id, machine_id, required_vm, required_cpu, false, {}, VM_MEMORY_OVERHEAD};
            machine_views[machine_id].active_vms.insert(vm_id);
            machine_views[machine_id].tracked_memory_used += VM_MEMORY_OVERHEAD;
            vms_created++;
        }

        VM_AddTask(vm_id, task_id, priority);
        task_to_vm[task_id] = vm_id;
        vm_states[vm_id].active_tasks.insert(task_id);
        vm_states[vm_id].tracked_memory_footprint += task_it->second.required_memory;
        machine_views[machine_id].tracked_memory_used += task_it->second.required_memory;
        machine_views[machine_id].active_task_count++;
        task_it->second.assigned = true;
        task_it->second.assigned_vm = vm_id;
        successful_placements++;
            return true;
        }
        return false;
    };

    if(try_pass(false)) {
        return true;
    }

    // If all candidates were protected, allow only top-priority classes to spill over.
    if(skipped_any_protected_machine && priority == HIGH_PRIORITY) {
        if(try_pass(true)) {
            return true;
        }
    }

    return false;
}

void Scheduler::EnqueueRetry(TaskId_t task_id) {
    retry_queue.push_back({task_id});
    retry_enqueues++;
}

void Scheduler::ProcessRetryQueue(Time_t now) {
    const size_t queue_snapshot_size = retry_queue.size();
    for(size_t i = 0; i < queue_snapshot_size; i++) {
        const RetryEntry entry = retry_queue.front();
        retry_queue.pop_front();

        auto task_it = task_states.find(entry.task_id);
        if(task_it == task_states.end()) {
            continue;
        }

        if(task_it->second.placement_failed || task_it->second.assigned) {
            continue;
        }

        retry_attempts++;
        if(TryPlaceTask(entry.task_id)) {
            continue;
        }

        task_it->second.retry_count++;
        if(task_it->second.retry_count == kMaxRetriesPerTask) {
            SimOutput("Scheduler::ProcessRetryQueue(): task " + to_string(entry.task_id) + " reached retry threshold at time " + to_string(now) + ", continuing retries", 2);
        }
        retry_queue.push_back(entry);
    }
}

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    // 
    const unsigned total_machines = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    //RESET SCHEDULER STATES
    task_states.clear();
    vm_states.clear();
    task_to_vm.clear();
    vm_last_migration_time.clear();
    retry_queue.clear();
    failed_task_ids.clear();

    //RESET SCHEDULER REPORTING COUNTERS
    tasks_seen = 0;
    tasks_completed = 0;
    successful_placements = 0;
    retry_enqueues = 0;
    retry_attempts = 0;
    placement_failures = 0;
    vms_created = 0;
    migrations = 0;
    wakeups = 0;
    at_risk_tasks_detected = 0;
    protected_machine_skips = 0;

    InitializeMachineViews();
    RefreshMachineStatesFromSimulator();

    SimOutput("Scheduler::Init(): machine state scaffolding initialized", 2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
    (void)time;
    auto vm_it = vm_states.find(vm_id);
    if(vm_it != vm_states.end()) {
        vm_it->second.migrating = false;
        const VMInfo_t vm_info = VM_GetInfo(vm_id);
        vm_it->second.machine_id = vm_info.machine_id;
    }

    RefreshMachineStatesFromSimulator();
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM, 
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired
    (void)now;

    // Temporary safe behavior before phase-3+ policy logic:
    // lazily create a compatible VM on the first S0 machine with matching CPU.
    const CPUType_t required_cpu = RequiredCPUType(task_id);
    const VMType_t required_vm = RequiredVMType(task_id);
    const SLAType_t required_sla = RequiredSLA(task_id);
    const Priority_t priority = PriorityFromSLA(required_sla);

    // Keep simulator task metadata aligned with scheduler-assigned priority.
    SetTaskPriority(task_id, priority);

    task_states[task_id] = TaskState{
        task_id,
        required_cpu,
        required_vm,
        required_sla,
        GetTaskMemory(task_id),
        IsTaskGPUCapable(task_id),
        priority,
        false,
        false,
        VMId_t(-1),
        0,
        false
    };

    tasks_seen++;

    RefreshMachineStatesFromSimulator();
    RefreshProtectedMachines(now);

    if(!TryPlaceTask(task_id)) {
        EnqueueRetry(task_id);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    // MVP phase 8: Refresh dynamic machine state before processing retries to reduce stale-state effects.
    RefreshMachineStatesFromSimulator();
    RefreshProtectedMachines(now);
    ProcessAtRiskMigrations(now);
    ProcessRetryQueue(now);
}

void Scheduler::SLAWarn(Time_t now, TaskId_t task_id) {
    RefreshMachineStatesFromSimulator();
    RefreshProtectedMachines(now);
    (void)TryMigrateAtRiskTask(task_id, now, true);
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    cout << "Scheduler report" << endl;
    cout << "Tasks seen: " << tasks_seen << endl;
    cout << "Tasks completed: " << tasks_completed << endl;
    cout << "Successful placements: " << successful_placements << endl;
    cout << "Retry enqueues: " << retry_enqueues << endl;
    cout << "Retry attempts: " << retry_attempts << endl;
    cout << "Placement failures: " << placement_failures << endl;
    cout << "VMs created: " << vms_created << endl;
    cout << "Migrations: " << migrations << endl;
    cout << "Wakeups: " << wakeups << endl;
    cout << "At-risk tasks detected: " << at_risk_tasks_detected << endl;
    cout << "Protected-machine skips: " << protected_machine_skips << endl;
    cout << "Failed task IDs: ";
    if(failed_task_ids.empty()) {
        cout << "none";
    } else {
        for(size_t i = 0; i < failed_task_ids.size(); i++) {
            if(i > 0) {
                cout << ",";
            }
            cout << failed_task_ids[i];
        }
    }
    cout << endl;

    for(auto & vm_entry: vm_states) {
        VM_Shutdown(vm_entry.first);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);

    RefreshMachineStatesFromSimulator();

    auto task_it = task_states.find(task_id);
    if(task_it == task_states.end()) {
        return;
    }

    if(task_it->second.completed) {
        return;
    }

    task_it->second.completed = true;
    tasks_completed++;

    if(!task_it->second.assigned) {
        return;
    }

    const VMId_t vm_id = task_it->second.assigned_vm;
    auto vm_it = vm_states.find(vm_id);
    if(vm_it == vm_states.end()) {
        task_it->second.assigned = false;
        task_it->second.assigned_vm = VMId_t(-1);
        task_to_vm.erase(task_id);
        return;
    }

    VMState &vm_state = vm_it->second;
    vm_state.active_tasks.erase(task_id);
    if(vm_state.tracked_memory_footprint >= task_it->second.required_memory) {
        vm_state.tracked_memory_footprint -= task_it->second.required_memory;
    } else {
        vm_state.tracked_memory_footprint = 0;
    }

    auto machine_it = machine_views.find(vm_state.machine_id);
    if(machine_it != machine_views.end()) {
        MachineStateView &machine = machine_it->second;
        if(machine.active_task_count > 0) {
            machine.active_task_count--;
        }
        if(machine.tracked_memory_used >= task_it->second.required_memory) {
            machine.tracked_memory_used -= task_it->second.required_memory;
        } else {
            machine.tracked_memory_used = 0;
        }
    }

    task_to_vm.erase(task_id);
    task_it->second.assigned = false;
    task_it->second.assigned_vm = VMId_t(-1);

    // MVP phase 7: Keep empty VMs alive for future reuse rather than shutdown.
    // Future post-MVP phases will implement consolidation/shutdown policy.
    // (VM reuse reduces VM creation churn during burst recovery.)

    RefreshMachineStatesFromSimulator();
    RefreshProtectedMachines(now);
}


// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    Scheduler.SLAWarn(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}

