#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
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
    void HandleSLAWarning(Time_t time, TaskId_t task_id);
    void HandleMemoryWarning(Time_t time, MachineId_t machine_id);
    void StateChangeFinished(Time_t time, MachineId_t machine_id);
    void TaskComplete(Time_t now, TaskId_t task_id);
private:
    enum class HostState {
        OVERLOADED,
        NORMAL,
        UNDERLOADED
    };

    bool TryPlaceTask(TaskId_t task_id, bool log_failure);
    bool TryWakeMachineForTask(TaskId_t task_id);
    HostState ClassifyMachine(MachineId_t machine_id) const;
    void RefreshMachineStates();
    void ProtectMachine(MachineId_t machine_id, Time_t now);
    bool IsMachineProtected(MachineId_t machine_id, Time_t now) const;
    void MaybeSleepMachine(MachineId_t machine_id, Time_t now);
    void RetryWaitingTasks(Time_t now);
    unsigned CountReadyMachines(CPUType_t cpu_type, MachineId_t exclude_machine) const;
    unsigned CountReadyPreferredMachines(CPUType_t cpu_type, MachineId_t exclude_machine) const;
    unsigned CountReadyGpuMachines(CPUType_t cpu_type, MachineId_t exclude_machine) const;

    vector<VMId_t> vms;
    vector<MachineId_t> machines;
    vector<TaskId_t> waiting_tasks;
    unordered_map<TaskId_t, VMId_t> task_to_vm;
    unordered_map<VMId_t, SLAType_t> vm_slas;
    unordered_map<CPUType_t, uint64_t> best_capacity_by_cpu;
    unordered_map<MachineId_t, array<unsigned, NUM_SLAS>> machine_sla_vm_counts;
    unordered_set<MachineId_t> pending_state_changes;
    unordered_map<MachineId_t, HostState> host_states;
    unordered_map<MachineId_t, Time_t> protected_until;
    unordered_map<MachineId_t, Time_t> idle_since;
    unordered_map<MachineId_t, Time_t> low_power_since;
    unsigned total_tasks = 0;
    unsigned completed_tasks = 0;
    unsigned next_progress_percent = 10;
};



#endif /* Scheduler_hpp */
