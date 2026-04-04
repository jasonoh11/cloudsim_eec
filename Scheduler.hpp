//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

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
    // Per-machine shadow state.  tracked_memory_used and active_task_count are
    // updated manually on every placement and completion so they remain correct
    // within a burst even when the simulator hasn't propagated events yet.
    // RefreshMachineStatesFromSimulator() is the only site that overwrites them
    // from the simulator and is called only from PeriodicCheck.
    // -----------------------------------------------------------------------
    struct MachineStateView {
        MachineId_t    machine_id;
        CPUType_t      cpu;
        unsigned       num_cores;
        unsigned       mips_p0;             // MIPS at P0, used for machine ordering
        unsigned       memory_capacity;
        unsigned       tracked_memory_used;
        bool           gpu_enabled;
        MachineState_t s_state;
        unsigned       active_task_count;
        bool           state_change_pending; // true while waiting for S0 after Machine_SetState
        Time_t         last_active_time;     // sim time when active_task_count last became 0
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

    // Max tasks-per-core ratio allowed for each SLA tier.
    // SLA0 never oversubscribes; SLA3 can be heavily packed.
    static constexpr double   kLoadCapSLA0        = 1.0;
    static constexpr double   kLoadCapSLA1        = 2.0;
    static constexpr double   kLoadCapSLA2        = 4.0;
    static constexpr double   kLoadCapSLA3        = 8.0;

    // Max retry-queue entries processed per ProcessRetryQueue call.
    // Prevents any single PeriodicCheck or TaskComplete from monopolising CPU.
    static constexpr unsigned kRetryBatchCap      = 1000;

    // -----------------------------------------------------------------------
    // Idle-shutdown policy constants.
    //
    // kIdleShutdownThreshold: how long (sim time units) a machine must be
    //   continuously idle before it is eligible for shutdown.  5 000 000 units
    //   is deliberately long (~5 simulated seconds) so that transient lulls
    //   between bursts do not trigger unnecessary wake/sleep cycles.
    //
    // kMinS0Machines: the cluster always keeps at least this many S0 machines
    //   per CPU type so there is always somewhere to land an arriving task
    //   without paying a wake-up latency penalty.  Conservative: 2 per type.
    //
    // kShutdownBatchCap: maximum machines shut down per PeriodicCheck call.
    //   Limits the blast radius if the heuristic misfires.
    // -----------------------------------------------------------------------
    static constexpr Time_t   kIdleShutdownThreshold = 2000000000ULL;  // 5 000 000 time units
    static constexpr unsigned kMinS0MachinesPerCPU   = 6;  // keep more machines warm as buffer
    static constexpr unsigned kShutdownBatchCap      = 1;

    // -----------------------------------------------------------------------
    // Private methods
    // -----------------------------------------------------------------------
    void       InitializeMachineViews();
    void       RefreshMachineStatesFromSimulator();
    Priority_t PriorityFromSLA(SLAType_t sla) const;
    double     MaxLoadForSLA(SLAType_t sla) const;
    unsigned   AdditionalPlacementMemory(TaskId_t task_id, bool creating_vm) const;
    bool       IsHardwareCompatible(TaskId_t task_id) const;
    bool       IsMachineFeasible(TaskId_t task_id, MachineId_t machine_id, bool creating_vm) const;
    bool       HasReusableVM(MachineId_t machine_id, VMType_t vm_type,
                             CPUType_t cpu, VMId_t &out_vm_id) const;
    bool       TryPlaceTask(TaskId_t task_id);
    void       EnqueueRetry(TaskId_t task_id);
    void       ProcessRetryQueue(Time_t now);

    // Idle-shutdown helpers.
    void       ShutdownIdleMachines(Time_t now);
    bool       WakeCompatibleMachine(CPUType_t cpu, bool need_gpu, Time_t now);
    unsigned   CountS0Machines(CPUType_t cpu) const;

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    vector<MachineId_t> machines;
    // Sorted fastest → slowest by mips_p0.  Used forward for SLA0/1 (prefer
    // fast machines) and backward for SLA2/3 (leave fast machines free).
    vector<MachineId_t> sorted_machines;

    unordered_map<MachineId_t, MachineStateView>   machine_views;
    unordered_map<VMId_t,      VMState>            vm_states;
    unordered_map<MachineId_t, vector<VMId_t>>     machine_to_vms;
    unordered_map<TaskId_t,    TaskState>           task_states;

    deque<RetryEntry> retry_queue;

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
    unsigned machines_shut_down    = 0;   // new: tracks idle shutdowns
    unsigned machines_woken        = 0;   // new: tracks demand-driven wakes
    vector<TaskId_t> failed_task_ids;
};

#endif /* Scheduler_hpp */