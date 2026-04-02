//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <deque>
#include <set>
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
    void TaskComplete(Time_t now, TaskId_t task_id);

private:
    struct MachineStateView {
        MachineId_t machine_id;
        CPUType_t cpu;
        unsigned memory_capacity;
        unsigned tracked_memory_used;
        bool gpu_enabled;
        MachineState_t s_state;
        unordered_set<VMId_t> active_vms;
        unsigned active_task_count;
        bool state_change_pending;
    };

    struct VMState {
        VMId_t vm_id;
        MachineId_t machine_id;
        VMType_t vm_type;
        CPUType_t cpu;
        bool migrating;
        unordered_set<TaskId_t> active_tasks;
        unsigned tracked_memory_footprint;
    };

    struct TaskState {
        TaskId_t task_id;
        CPUType_t required_cpu;
        VMType_t required_vm;
        SLAType_t required_sla;
        unsigned required_memory;
        bool gpu_capable;
        Priority_t assigned_priority;
        bool assigned;
        VMId_t assigned_vm;
        unsigned retry_count;
        bool placement_failed;
    };

    struct RetryEntry {
        TaskId_t task_id;
    };

    void InitializeMachineViews();
    void RefreshMachineStatesFromSimulator();

    vector<MachineId_t> machines;

    unordered_map<MachineId_t, MachineStateView> machine_views;
    unordered_map<VMId_t, VMState> vm_states;
    unordered_map<TaskId_t, TaskState> task_states;
    unordered_map<TaskId_t, VMId_t> task_to_vm;

    deque<RetryEntry> retry_queue;

    // Reporting counters (filled in later phases).
    unsigned tasks_seen = 0;
    unsigned successful_placements = 0;
    unsigned retry_enqueues = 0;
    unsigned retry_attempts = 0;
    unsigned placement_failures = 0;
    unsigned vms_created = 0;
    unsigned migrations = 0;
    unsigned wakeups = 0;
    vector<TaskId_t> failed_task_ids;
};



#endif /* Scheduler_hpp */
