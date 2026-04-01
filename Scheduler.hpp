//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <utility>
#include <vector>

#include "Interfaces.h"

class Scheduler {
public:
    Scheduler()                 {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void HandleMachineWake(Time_t time, MachineId_t machine_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
private:
    static const unsigned CPU_TYPE_COUNT = 4;
    float GetEfficiencyScore(MachineId_t) const;
    void SortByEfficiency(vector<MachineId_t>&);
    void PrintMachineBuckets() const;
    pair<const vector<MachineId_t>*, const vector<MachineId_t>*> GetCandidateBuckets(TaskId_t task_id) const;
    bool CanHostTask(MachineId_t machine_id, TaskId_t task_id) const;
    MachineId_t FindFeasibleMachine(TaskId_t task_id) const;
    MachineId_t FindWakeableMachine(TaskId_t task_id) const;
    bool VM_IsFeasible(VMId_t vm_id, TaskId_t task_id) const;
    VMId_t FindFeasibleVM(MachineId_t machine_id, TaskId_t task_id) const;
    void RetryWaitingTasks(Time_t now);

    vector<VMId_t> vms;
    vector<MachineId_t> machines;
    vector<MachineId_t> gpu_machines_by_cpu[CPU_TYPE_COUNT];
    vector<MachineId_t> non_gpu_machines_by_cpu[CPU_TYPE_COUNT];
    vector<vector<VMId_t>> machine_to_vms;
    vector<bool> vm_is_migrating;
    vector<VMId_t> task_to_vm;
    vector<bool> machine_waking;
    vector<TaskId_t> waiting_tasks;
};



#endif /* Scheduler_hpp */
