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

    // -----------------------------------------------------------------------
    // E-Eco tier membership.
    //
    //   Running      — machine is S0 and accepting tasks.
    //   Intermediate — machine is in S1 hot standby; fast to promote.
    //   SwitchedOff  — machine is in S5; cold; promoted only under pressure.
    //
    // TryPlaceTask does not consult tier directly — IsMachineFeasible's
    // s_state == S0 && !state_change_pending check enforces tier boundaries.
    // -----------------------------------------------------------------------
    enum class MachineTier { Running, Intermediate, SwitchedOff };

    // -----------------------------------------------------------------------
    // Per-machine shadow state.
    //
    // tracked_memory_used and active_task_count are maintained exclusively by
    // TryPlaceTask (increments) and TaskComplete (decrements).  They are NOT
    // overwritten by RefreshMachineStatesFromSimulator because the simulator's
    // view lags behind in-flight placements, and overwriting causes
    // over-placement crashes when PeriodicCheck and StateChangeComplete fire
    // at the same simulation timestamp.
    //
    // s_state IS refreshed from the simulator in PeriodicCheck so that power
    // transitions are reflected for IsMachineFeasible.
    // -----------------------------------------------------------------------
    struct MachineStateView {
        MachineId_t    machine_id;
        CPUType_t      cpu;
        unsigned       num_cores;
        unsigned       mips_p0;             // MIPS at P0; used for ordering
        unsigned       memory_capacity;
        unsigned       tracked_memory_used;
        bool           gpu_enabled;
        MachineState_t s_state;
        unsigned       active_task_count;
        bool           state_change_pending; // true between Machine_SetState and StateChangeComplete
        MachineState_t target_state;         // state last commanded via Machine_SetState
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
        unsigned   retry_count;      // diagnostic only
        bool       placement_failed; // set only when no compatible hardware exists
    };

    struct RetryEntry {
        TaskId_t task_id;
    };

    // -----------------------------------------------------------------------
    // Tuning constants
    // -----------------------------------------------------------------------

    // SLA-tiered load caps: maximum active_tasks / num_cores per machine.
    static constexpr double kLoadCapSLA0 = 1.0;
    static constexpr double kLoadCapSLA1 = 2.0;
    static constexpr double kLoadCapSLA2 = 4.0;
    static constexpr double kLoadCapSLA3 = 8.0;

    // E-Eco tier sizing.
    static constexpr double   kTargetLoadPerMachine        = 0.70;
    static constexpr double   kIntermediateBufferRatio     = 0.40;
    static constexpr unsigned kMinRunningMachines          = 2;
    static constexpr unsigned kMinIntermediateMachines     = 1;
    static constexpr double   kInitialRunningFraction      = 0.60;
    static constexpr double   kInitialIntermediateFraction = 0.25;

    // Queue-aware promotion bias.
    // For every kQueueBiasTasksPerMachine SLA0 tasks queued, AdjustTiers
    // adds one extra machine to desired_running.  SLA1 uses 2× the divisor.
    static constexpr unsigned kQueueBiasTasksPerMachine = 8;

    // Maximum retry-queue entries processed per ProcessRetryQueue() call.
    static constexpr unsigned kRetryBatchCap = 1000;

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------

    void       InitializeMachineViews();
    void       ClassifyMachinesIntoTiers();
    void       RefreshMachineStatesFromSimulator();

    Priority_t PriorityFromSLA(SLAType_t sla) const;
    double     MaxLoadForSLA(SLAType_t sla) const;
    unsigned   AdditionalPlacementMemory(TaskId_t task_id, bool creating_vm) const;
    bool       IsHardwareCompatible(TaskId_t task_id) const;
    bool       IsMachineFeasible(TaskId_t task_id, MachineId_t machine_id,
                                  bool creating_vm) const;
    bool       HasReusableVM(MachineId_t machine_id, VMType_t vm_type,
                              CPUType_t cpu, VMId_t &out_vm_id) const;

    unsigned ComputeDesiredRunning() const;
    unsigned ComputeDesiredIntermediate(unsigned running_count) const;
    void     AdjustTiers(Time_t now);
    void     PromoteForTask(TaskId_t task_id);

    bool TryPlaceTask(TaskId_t task_id);
    void EnqueueRetry(TaskId_t task_id);
    void DecrementQueuedSLACount(TaskId_t task_id);
    void ProcessRetryQueue(Time_t now);

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------

    vector<MachineId_t> machines;
    vector<MachineId_t> sorted_machines; // fastest → slowest by mips_p0

    unordered_map<MachineId_t, MachineStateView>  machine_views;
    unordered_map<MachineId_t, MachineTier>       machine_tier;
    unordered_map<VMId_t,      VMState>           vm_states;
    unordered_map<MachineId_t, vector<VMId_t>>    machine_to_vms;
    unordered_map<TaskId_t,    TaskState>          task_states;

    deque<RetryEntry> retry_queue;

    // Incremented in EnqueueRetry, decremented when task permanently exits queue.
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