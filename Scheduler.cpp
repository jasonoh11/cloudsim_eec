//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <algorithm>

static bool migrating = false;
static unsigned active_machines = 0;

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

    SimOutput("Score " + to_string(GetEfficiencyScore(MachineId_t(0))), 1);
    SimOutput("Score " + to_string(GetEfficiencyScore(MachineId_t(2))), 1);
    PrintMachineBuckets();

}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
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
    VM_AddTask(vms[0], task_id, Priority_t(0));
    // Priority_t priority = (task_id == 0 || task_id == 64)? HIGH_PRIORITY : MID_PRIORITY;
    // if(migrating) {
    //     VM_AddTask(vms[0], task_id, priority);
    // }
    // else {
    //     VM_AddTask(vms[task_id % active_machines], task_id, priority);
    // }// Skeleton code, you need to change it according to your algorithm
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 3);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 3);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 3);
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
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 3);
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
}
