//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <vector>

#include "Interfaces.h"

class Scheduler {
public:
    Scheduler()                 {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
private:
    static const unsigned CPU_TYPE_COUNT = 4;
    float GetEfficiencyScore(MachineId_t) const;
    void SortByEfficiency(vector<MachineId_t>&);
    void PrintMachineBuckets() const;

    vector<VMId_t> vms;
    vector<MachineId_t> machines;
    vector<MachineId_t> gpu_machines_by_cpu[CPU_TYPE_COUNT];
    vector<MachineId_t> non_gpu_machines_by_cpu[CPU_TYPE_COUNT];
};



#endif /* Scheduler_hpp */
