//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <algorithm>
#include <utility>

static bool migrating = false;
static unsigned active_machines = 0;

static Priority_t GetPriorityForTask(TaskId_t task_id) {
    switch (RequiredSLA(task_id)) {
        case SLA0:
            return HIGH_PRIORITY;
        case SLA1:
            return MID_PRIORITY;
        case SLA2:
        case SLA3:
        default:
            return LOW_PRIORITY;
    }
}

static double GetMaxLoadFactorForTask(TaskId_t task_id) {
    switch (RequiredSLA(task_id)) {
        case SLA0:
            return 2.0;
        case SLA1:
            return 4.0;
        case SLA2:
            return 8.0;
        case SLA3:
        default:
            return 12.0;
    }
}

void Scheduler::PrintMachineBuckets() const {
    for (unsigned cpu = 0; cpu < CPU_TYPE_COUNT; cpu++) {
        string non_gpu_message = "CPU " + to_string(cpu) + " non-GPU machines:";
        for (MachineId_t machine_id : non_gpu_machines_by_cpu[cpu]) {
            non_gpu_message += " " + to_string(machine_id);
        }
        SimOutput(non_gpu_message, 2);

        string gpu_message = "CPU " + to_string(cpu) + " GPU machines:";
        for (MachineId_t machine_id : gpu_machines_by_cpu[cpu]) {
            gpu_message += " " + to_string(machine_id);
        }
        SimOutput(gpu_message, 2);
    }
}

float Scheduler::GetEfficiencyScore(MachineId_t machine_id) const {
    MachineInfo_t info = Machine_GetInfo(machine_id);
    return static_cast<float>(info.performance[P0]) / info.p_states[P0];

}

void Scheduler::SortByEfficiency(vector<MachineId_t>& machines) {
    std::sort(machines.begin(), machines.end(),
        [this](MachineId_t a, MachineId_t b) {
            return GetEfficiencyScore(a) > GetEfficiencyScore(b);
        });
}

pair<const vector<MachineId_t>*, const vector<MachineId_t>*> Scheduler::GetCandidateBuckets(TaskId_t task_id) const {
    TaskInfo_t info = GetTaskInfo(task_id);
    unsigned cpu_index = static_cast<unsigned>(info.required_cpu);

    if (info.gpu_capable) {
        return {&gpu_machines_by_cpu[cpu_index], &non_gpu_machines_by_cpu[cpu_index]};
    } else {
        return {&non_gpu_machines_by_cpu[cpu_index], &gpu_machines_by_cpu[cpu_index]};
    }
}

bool Scheduler::CanHostTask(MachineId_t machine_id, TaskId_t task_id) const {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    TaskInfo_t task_info = GetTaskInfo(task_id);

    if (machine_transitioning[machine_id] || machine_info.s_state != S0) {
        return false;
    }

    unsigned free_memory = machine_info.memory_size - machine_info.memory_used;
    unsigned required_memory = task_info.required_memory + VM_MEMORY_OVERHEAD;
    if (free_memory < required_memory) {
        return false;
    }

    double load_factor = static_cast<double>(machine_info.active_tasks)
                       / max(1u, machine_info.num_cpus);
    return load_factor < GetMaxLoadFactorForTask(task_id);
}

MachineId_t Scheduler::FindWakeableMachine(TaskId_t task_id) const {
    auto candidate_buckets = GetCandidateBuckets(task_id);
    const vector<MachineId_t>* primary_candidates = candidate_buckets.first;
    const vector<MachineId_t>* fallback_candidates = candidate_buckets.second;
    TaskInfo_t task_info = GetTaskInfo(task_id);
    unsigned required_memory = task_info.required_memory + VM_MEMORY_OVERHEAD;

    for (MachineId_t machine_id : *primary_candidates) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        unsigned free_memory = machine_info.memory_size - machine_info.memory_used;
        if (!machine_transitioning[machine_id] && machine_info.s_state != S0 && free_memory >= required_memory) {
            return machine_id;
        }
    }

    for (MachineId_t machine_id : *fallback_candidates) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        unsigned free_memory = machine_info.memory_size - machine_info.memory_used;
        if (!machine_transitioning[machine_id] && machine_info.s_state != S0 && free_memory >= required_memory) {
            return machine_id;
        }
    }

    SimOutput("No wakeable machine found for task " + to_string(task_id)
              + " required_mem=" + to_string(required_memory), 1);
    return Machine_GetTotal();
}

MachineId_t Scheduler::FindFeasibleMachine(TaskId_t task_id) const {
    auto candidate_buckets = GetCandidateBuckets(task_id);
    const vector<MachineId_t>* primary_candidates = candidate_buckets.first;
    const vector<MachineId_t>* fallback_candidates = candidate_buckets.second;

    for (MachineId_t machine_id : *primary_candidates) {
        if (CanHostTask(machine_id, task_id)) {
            return machine_id;
        }
    }

    for (MachineId_t machine_id : *fallback_candidates) {
        if (CanHostTask(machine_id, task_id)) {
            return machine_id;
        }
    }

    return Machine_GetTotal();
}

bool Scheduler::VM_IsFeasible(VMId_t vm_id, TaskId_t task_id) const {
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    TaskInfo_t task_info = GetTaskInfo(task_id);

    return (task_info.required_vm == vm_info.vm_type);
}

VMId_t Scheduler::FindFeasibleVM(MachineId_t machine_id, TaskId_t task_id) const {

    for (VMId_t id : machine_to_vms[machine_id]) {
        if (VM_IsFeasible(id, task_id)) {
            return id;
        }
    }

    return vms.size();
}

void Scheduler::RetryWaitingTasks(Time_t now) {
    vector<TaskId_t> pending = waiting_tasks;
    waiting_tasks.clear();

    for (TaskId_t task_id : pending) {
        if (IsTaskCompleted(task_id)) {
            continue;
        }
        if (task_to_vm[task_id] != static_cast<VMId_t>(-1)) {
            // task is assigned to a VM
            continue;
        }
        NewTask(now, task_id);
    }
}

void Scheduler::DumpStalledTasks(Time_t now) const {
    SimOutput("Stall diagnostic triggered at time " + to_string(now), 0);

    unsigned printed = 0;
    for (TaskId_t task_id = 0; task_id < GetNumTasks() && printed < 10; task_id++) {
        TaskInfo_t task_info = GetTaskInfo(task_id);
        if (task_info.completed) {
            continue;
        }

        string message = "Stalled task id=" + to_string(task_id)
                       + " arrival=" + to_string(task_info.arrival)
                       + " remaining=" + to_string(task_info.remaining_instructions)
                       + " target_completion=" + to_string(task_info.target_completion)
                       + " priority=" + to_string(task_info.priority)
                       + " required_vm=" + to_string(task_info.required_vm)
                       + " required_cpu=" + to_string(task_info.required_cpu);

        VMId_t mapped_vm = task_to_vm[task_id];
        if (mapped_vm == static_cast<VMId_t>(-1)) {
            message += " mapped_vm=NONE";
            SimOutput(message, 0);
            printed++;
            continue;
        }

        message += " mapped_vm=" + to_string(mapped_vm);

        bool vm_recorded = false;
        MachineId_t recorded_machine = Machine_GetTotal();
        for (MachineId_t machine_id = 0; machine_id < machine_to_vms.size(); machine_id++) {
            const auto& vm_list = machine_to_vms[machine_id];
            if (find(vm_list.begin(), vm_list.end(), mapped_vm) != vm_list.end()) {
                vm_recorded = true;
                recorded_machine = machine_id;
                break;
            }
        }

        message += vm_recorded
                 ? " recorded_machine=" + to_string(recorded_machine)
                 : " recorded_machine=NONE";

        try {
            VMInfo_t vm_info = VM_GetInfo(mapped_vm);
            bool task_present = find(vm_info.active_tasks.begin(),
                                     vm_info.active_tasks.end(),
                                     task_id) != vm_info.active_tasks.end();

            message += " vm_machine=" + to_string(vm_info.machine_id)
                    + " vm_tasks=" + to_string(vm_info.active_tasks.size())
                    + " task_present=" + to_string(task_present);

            MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);
            message += " machine_state=" + to_string(machine_info.s_state)
                    + " machine_active_tasks=" + to_string(machine_info.active_tasks)
                    + " machine_active_vms=" + to_string(machine_info.active_vms);
        } catch (const exception& ex) {
            message += " vm_info_error=" + string(ex.what());
        } catch (...) {
            message += " vm_info_error=unknown";
        }

        SimOutput(message, 0);
        printed++;
    }

    if (printed == 0) {
        SimOutput("Stall diagnostic found no unfinished tasks.", 0);
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

    active_machines = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(active_machines), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    machine_to_vms.resize(active_machines);
    task_to_vm.resize(GetNumTasks(), static_cast<VMId_t>(-1));
    machine_transitioning.resize(active_machines, false);
    machine_waking.resize(active_machines, false);


    for(unsigned i = 0; i < active_machines; i++) {
        MachineId_t machine_id = MachineId_t(i);
        machines.push_back(machine_id);

        MachineInfo_t info = Machine_GetInfo(machine_id);
        unsigned cpu_index = static_cast<unsigned>(info.cpu);

        if (info.gpus) {
            gpu_machines_by_cpu[cpu_index].push_back(machine_id);
        } else {
            non_gpu_machines_by_cpu[cpu_index].push_back(machine_id);
        }
    }

    for(unsigned cpu = 0; cpu < CPU_TYPE_COUNT; cpu++){
        SortByEfficiency(non_gpu_machines_by_cpu[cpu]);
        SortByEfficiency(gpu_machines_by_cpu[cpu]);
    }

    PrintMachineBuckets();

}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::HandleMachineWake(Time_t time, MachineId_t machine_id) {
    machine_transitioning[machine_id] = false;

    bool was_waking = machine_waking[machine_id];
    machine_waking[machine_id] = false;

    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if (machine_info.s_state != S0) {
        return;
    }

    if (was_waking) {
        RetryWaitingTasks(time);
    }
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
    // Priority_t priority = (task_id == 0 || task_id == 64)? HIGH_PRIORITY : MID_PRIORITY;
    // if(migrating) {
    //     VM_AddTask(vms[0], task_id, priority);
    // }
    // else {
    //     VM_AddTask(vms[task_id % active_machines], task_id, priority);
    // }// Skeleton code, you need to change it according to your algorithm
    TaskInfo_t task_info = GetTaskInfo(task_id);

    auto candidate_buckets = GetCandidateBuckets(task_id);
    const vector<MachineId_t>* primary_candidates = candidate_buckets.first;
    const vector<MachineId_t>* fallback_candidates = candidate_buckets.second;

    string primary_message = "Primary candidates:";
    for (MachineId_t id : *primary_candidates) {
        primary_message += " " + to_string(id);
    }
    SimOutput(primary_message, 3);

    string fallback_message = "Fallback candidates:";
    for (MachineId_t id : *fallback_candidates) {
        fallback_message += " " + to_string(id);
    }
    SimOutput(fallback_message, 3);

    MachineId_t selected_machine = FindFeasibleMachine(task_id);
    if (selected_machine == Machine_GetTotal()) {
        MachineId_t sleeping_machine = FindWakeableMachine(task_id);
        if (sleeping_machine == Machine_GetTotal()) {
            if (find(waiting_tasks.begin(), waiting_tasks.end(), task_id) == waiting_tasks.end()) {
                waiting_tasks.push_back(task_id);
            }
            SimOutput("No wakeable machine found", 1);
        } else {
            SimOutput("No feasible machine found, waking machine " + to_string(sleeping_machine), 1);
            if (find(waiting_tasks.begin(), waiting_tasks.end(), task_id) == waiting_tasks.end()) {
                waiting_tasks.push_back(task_id);
            }
            if (!machine_transitioning[sleeping_machine]) {
                machine_transitioning[sleeping_machine] = true;
                machine_waking[sleeping_machine] = true;
                Machine_SetState(sleeping_machine, S0);
            }
            SimOutput("Waking machine: " + to_string(sleeping_machine), 1);
        }
    } else {
        SimOutput("Selected machine: " + to_string(selected_machine), 3);
        VMId_t selected_vm = FindFeasibleVM(selected_machine, task_id);
        if (selected_vm == vms.size()){
            selected_vm = VM_Create(task_info.required_vm, task_info.required_cpu);
            VM_Attach(selected_vm, selected_machine);
            vms.push_back(selected_vm);
            machine_to_vms[selected_machine].push_back(selected_vm);
        }
        VM_AddTask(selected_vm, task_id, GetPriorityForTask(task_id));
        task_to_vm[task_id] = selected_vm;
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    static Time_t last_heartbeat = 0;
    static unsigned stagnant_heartbeats = 0;
    static unsigned last_completed_tasks = 0;
    const Time_t heartbeat_interval = 50000000;
    if (now - last_heartbeat >= heartbeat_interval) {
        double completion_pct = 0.0;
        if (GetNumTasks() > 0) {
            completion_pct = 100.0 * completed_tasks / GetNumTasks();
        }
        unsigned awake_machines = 0;
        unsigned sleeping_machines = 0;
        for (MachineId_t machine_id = 0; machine_id < active_machines; machine_id++) {
            MachineInfo_t machine_info = Machine_GetInfo(machine_id);
            if (machine_info.s_state == S0) {
                awake_machines++;
            } else {
                sleeping_machines++;
            }
        }

        SimOutput("Heartbeat: simulation time " + to_string(now)
                  + " completed " + to_string(completed_tasks)
                  + "/" + to_string(GetNumTasks())
                  + " tasks (" + to_string(completion_pct) + "%)"
                  + " waiting=" + to_string(waiting_tasks.size())
                  + " awake=" + to_string(awake_machines)
                  + " sleeping=" + to_string(sleeping_machines), 0);

        if (completed_tasks == last_completed_tasks) {
            stagnant_heartbeats++;
        } else {
            stagnant_heartbeats = 0;
        }

        if (stagnant_heartbeats == 5) {
            DumpStalledTasks(now);
        }

        unsigned sample_count = min(static_cast<unsigned>(waiting_tasks.size()), 3u);
        for (unsigned i = 0; i < sample_count; i++) {
            TaskId_t task_id = waiting_tasks[i];
            TaskInfo_t task_info = GetTaskInfo(task_id);
            unsigned required_memory = task_info.required_memory + VM_MEMORY_OVERHEAD;
            SimOutput("Waiting task: id=" + to_string(task_id)
                      + " cpu=" + to_string(task_info.required_cpu)
                      + " vm=" + to_string(task_info.required_vm)
                      + " gpu=" + to_string(task_info.gpu_capable)
                      + " required_mem=" + to_string(required_memory), 0);

            auto candidate_buckets = GetCandidateBuckets(task_id);
            const vector<MachineId_t>* primary_candidates = candidate_buckets.first;
            const vector<MachineId_t>* fallback_candidates = candidate_buckets.second;

            unsigned printed = 0;
            for (MachineId_t machine_id : *primary_candidates) {
                MachineInfo_t machine_info = Machine_GetInfo(machine_id);
                unsigned free_memory = machine_info.memory_size - machine_info.memory_used;
                SimOutput("  primary machine=" + to_string(machine_id)
                          + " state=" + to_string(machine_info.s_state)
                          + " free_mem=" + to_string(free_memory), 0);
                printed++;
                if (printed == 3) {
                    break;
                }
            }

            if (printed == 0) {
                for (MachineId_t machine_id : *fallback_candidates) {
                    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
                    unsigned free_memory = machine_info.memory_size - machine_info.memory_used;
                    SimOutput("  fallback machine=" + to_string(machine_id)
                              + " state=" + to_string(machine_info.s_state)
                              + " free_mem=" + to_string(free_memory), 0);
                    printed++;
                    if (printed == 3) {
                        break;
                    }
                }
            }
        }
        last_completed_tasks = completed_tasks;
        last_heartbeat = now;
    }
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for (MachineId_t machine_id = 0; machine_id < machine_to_vms.size(); machine_id++) {
        for (VMId_t vm_id : machine_to_vms[machine_id]) {
            VM_Shutdown(vm_id);
        }
    }
    SimOutput("SimulationComplete(): Finished!", 3);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 3);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 3);
    completed_tasks++;

    VMId_t vm = task_to_vm[task_id];
    task_to_vm[task_id] = static_cast<VMId_t>(-1);

    RetryWaitingTasks(now);
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 3);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 3);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 3);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 3);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
    // static unsigned counts = 0;
    // counts++;
    // if(counts == 10) {
    //     migrating = true;
    //     VM_Migrate(1, 9);
    // }
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 3);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
    Scheduler.HandleMachineWake(time, machine_id);
}
