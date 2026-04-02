//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

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
        case SLA1:
            return HIGH_PRIORITY;
        case SLA2:
            return MID_PRIORITY;
        case SLA3:
        default:
            return LOW_PRIORITY;
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
    retry_queue.clear();
    failed_task_ids.clear();

    //RESET SCHEDULER REPORTING COUNTERS
    tasks_seen = 0;
    successful_placements = 0;
    retry_enqueues = 0;
    retry_attempts = 0;
    placement_failures = 0;
    vms_created = 0;
    migrations = 0;
    wakeups = 0;

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
        VMId_t(-1),
        0,
        false
    };

    for(const auto machine_id : machines) {
        const MachineInfo_t info = Machine_GetInfo(machine_id);
        if(info.s_state != S0 || info.cpu != required_cpu) {
            continue;
        }

        VMId_t vm_id = VMId_t(-1);
        for(const auto &kv : vm_states) {
            if(kv.second.machine_id == machine_id && kv.second.cpu == required_cpu && kv.second.vm_type == required_vm && !kv.second.migrating) {
                vm_id = kv.first;
                break;
            }
        }

        if(vm_id == VMId_t(-1)) {
            vm_id = VM_Create(required_vm, required_cpu);
            VM_Attach(vm_id, machine_id);
            vm_states[vm_id] = VMState{vm_id, machine_id, required_vm, required_cpu, false, {}, VM_MEMORY_OVERHEAD};
            machine_views[machine_id].active_vms.insert(vm_id);
            vms_created++;
        }

        VM_AddTask(vm_id, task_id, priority);
        task_to_vm[task_id] = vm_id;
        vm_states[vm_id].active_tasks.insert(task_id);
        task_states[task_id].assigned = true;
        task_states[task_id].assigned_vm = vm_id;
        tasks_seen++;
        successful_placements++;
        return;
    }

    retry_queue.push_back({task_id});
    retry_enqueues++;
    tasks_seen++;
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    (void)now;
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
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
    (void)time;
    (void)task_id;
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}

