//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Interfaces.h"

class Scheduler {
public:
    Scheduler() {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void HandleMachineWake(Time_t time, MachineId_t machine_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void SLAWarn(Time_t now, TaskId_t task_id);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);

private:

    enum class MachineTier { Running, Intermediate, SwitchedOff };

    // -----------------------------------------------------------------------
    // Per-machine shadow state.
    // tracked_memory_used and active_task_count are maintained by
    // TryPlaceTask/TaskComplete only — never overwritten from simulator.
    // s_state is refreshed from simulator in PeriodicCheck only.
    // idle_since: 0 = machine is active; T = idle since time T; 1 = idle at init.
    // -----------------------------------------------------------------------
    struct MachineStateView {
        MachineId_t    machine_id;
        CPUType_t      cpu;
        unsigned       num_cores;
        unsigned       mips_p0;
        unsigned       memory_capacity;
        unsigned       tracked_memory_used;
        bool           gpu_enabled;
        MachineState_t s_state;
        unsigned       active_task_count;
        bool           state_change_pending;
        MachineState_t target_state;
        Time_t         idle_since;
    };

    struct VMState {
        VMId_t      vm_id;
        MachineId_t machine_id;
        VMType_t    vm_type;
        CPUType_t   cpu;
        unsigned    tracked_memory_footprint;
    };

    struct TaskState {
        TaskId_t   task_id;
        CPUType_t  required_cpu;
        VMType_t   required_vm;
        SLAType_t  required_sla;
        unsigned   required_memory;
        bool       gpu_capable;
        Priority_t assigned_priority;
        bool       assigned;
        bool       completed;
        VMId_t     assigned_vm;
        unsigned   retry_count;
        bool       placement_failed;
    };

    struct RetryEntry {
        TaskId_t task_id;
    };

    // -----------------------------------------------------------------------
    // Tuning constants
    // -----------------------------------------------------------------------

    static constexpr double   kMaxLoadPerCore          = 1.0;
    static constexpr double   kTargetLoadPerMachine    = 0.70;
    static constexpr double   kIntermediateBufferRatio = 0.40;
    static constexpr unsigned kMinRunningMachines      = 4;
    static constexpr unsigned kMinIntermediateMachines = 2;

    // No demotion fires before this simulation time — prevents demoting
    // all machines at t=0 before any load has been established.
    static constexpr Time_t   kWarmupPeriod            = 3000000;

    // A machine must be continuously idle for this long before AdjustTiers
    // may demote it.  Set conservatively high so that demotions only happen
    // during genuine extended quiet periods, not inter-burst lulls.
    //
    // At 2M time units this was too aggressive: any workload with burst
    // inter-arrival gaps longer than 2M triggered demotion then immediate
    // re-promotion on the next burst — oscillation.  10M outlasts most
    // burst cycles while still allowing demotion during true quiet periods.
    //
    // Future tuning: lower this carefully after verifying SLA targets hold.
    static constexpr Time_t   kMinIdleBeforeDemotion   = 10000000;

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------

    void       InitializeMachineViews();
    void       ClassifyMachinesIntoTiers();
    void       RefreshMachineStatesFromSimulator();

    Priority_t PriorityFromSLA(SLAType_t sla) const;
    unsigned   AdditionalPlacementMemory(TaskId_t task_id, bool creating_vm) const;
    bool       IsHardwareCompatible(TaskId_t task_id) const;
    bool       IsMachineFeasible(TaskId_t task_id, MachineId_t machine_id,
                                  bool creating_vm) const;
    bool       HasReusableVM(MachineId_t machine_id, VMType_t vm_type,
                              CPUType_t cpu, VMId_t &out_vm_id) const;
    bool       HasCapacityForType(CPUType_t cpu, bool gpu) const;
    unsigned   FreeSlotCountForType(CPUType_t cpu, bool gpu) const;

    unsigned ComputeDesiredRunning() const;
    unsigned ComputeDesiredIntermediate(unsigned running_count) const;
    void     AdjustTiers(Time_t now);
    void     PromoteForTask(TaskId_t task_id);

    bool TryPlaceTask(TaskId_t task_id);

    static uint32_t HardwareKey(CPUType_t cpu, bool gpu) {
        return (static_cast<uint32_t>(cpu) << 1) | (gpu ? 1u : 0u);
    }

    void EnqueueRetry(TaskId_t task_id);
    void DecrementQueuedSLACount(TaskId_t task_id);
    void ProcessRetryQueueForType(Time_t now, CPUType_t cpu, bool gpu,
                                   unsigned max_attempts);
    void ProcessAllRetryQueues(Time_t now);
    bool AnyRetryQueued() const { return total_queued > 0; }

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------

    vector<MachineId_t> machines;
    vector<MachineId_t> sorted_machines;

    unordered_map<MachineId_t, MachineStateView>    machine_views;
    unordered_map<MachineId_t, MachineTier>         machine_tier;
    unordered_map<VMId_t,      VMState>             vm_states;
    unordered_map<MachineId_t, vector<VMId_t>>      machine_to_vms;
    unordered_map<TaskId_t,    TaskState>            task_states;

    unordered_map<uint32_t, deque<RetryEntry>> typed_retry_queues;
    unsigned total_queued      = 0;
    unsigned queued_sla0_count = 0;
    unsigned queued_sla1_count = 0;

    // -----------------------------------------------------------------------
    // Reporting counters
    // -----------------------------------------------------------------------
    unsigned tasks_seen            = 0;
    unsigned tasks_completed       = 0;
    unsigned successful_placements = 0;
    unsigned retry_enqueues        = 0;
    unsigned retry_attempts        = 0;
    unsigned placement_failures    = 0;
    unsigned vms_created           = 0;
    unsigned tier_promotions       = 0;
    unsigned tier_demotions        = 0;
    vector<TaskId_t> failed_task_ids;
};

#endif /* Scheduler_hpp */