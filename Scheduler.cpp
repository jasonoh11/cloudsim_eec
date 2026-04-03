//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <algorithm>
#include <limits>

namespace {

constexpr double kOverloadThreshold = 0.80;
constexpr double kUnderloadThreshold = 0.25;
constexpr unsigned kMinReadyMachinesPerCpu = 3;
constexpr Time_t kProtectionWindow = 5'000'000;
constexpr bool kEnableIdleSleep = false;
constexpr unsigned kPreferredTierReserveDivisor = 4;

uint64_t MachineCapacityScore(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    unsigned base_mips = machine_info.performance.empty() ? 0 : machine_info.performance[0];
    return static_cast<uint64_t>(machine_info.num_cpus) * base_mips;
}

bool IsPreferredPerformanceTier(const unordered_map<CPUType_t, uint64_t> & best_capacity_by_cpu,
                               TaskId_t task_id,
                               MachineId_t machine_id) {
    SLAType_t sla = RequiredSLA(task_id);
    if(sla != SLA0 && sla != SLA1) {
        return true;
    }

    auto best_it = best_capacity_by_cpu.find(RequiredCPUType(task_id));
    uint64_t best_score = (best_it == best_capacity_by_cpu.end()) ? 0 : best_it->second;
    uint64_t machine_score = MachineCapacityScore(machine_id);
    if(best_score == 0) {
        return true;
    }

    // Treat the top tier for a CPU family as the preferred landing zone for tighter SLAs.
    return machine_score * 100 >= best_score * 75;
}

bool MachineIsPreferredForCpu(const unordered_map<CPUType_t, uint64_t> & best_capacity_by_cpu,
                              CPUType_t cpu_type,
                              MachineId_t machine_id) {
    auto best_it = best_capacity_by_cpu.find(cpu_type);
    uint64_t best_score = (best_it == best_capacity_by_cpu.end()) ? 0 : best_it->second;
    if(best_score == 0) {
        return true;
    }
    return MachineCapacityScore(machine_id) * 100 >= best_score * 75;
}

unsigned CountPreferredMachinesForCpu(const vector<MachineId_t> & machines,
                                      const unordered_map<CPUType_t, uint64_t> & best_capacity_by_cpu,
                                      CPUType_t cpu_type,
                                      bool hosting_sla0_only,
                                      const unordered_map<MachineId_t, array<unsigned, NUM_SLAS>> & machine_sla_vm_counts) {
    unsigned count = 0;
    for(MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        if(machine_info.cpu != cpu_type || !MachineIsPreferredForCpu(best_capacity_by_cpu, cpu_type, machine_id)) {
            continue;
        }
        if(hosting_sla0_only) {
            auto it = machine_sla_vm_counts.find(machine_id);
            if(it == machine_sla_vm_counts.end() || it->second[SLA0] == 0) {
                continue;
            }
        }
        count++;
    }
    return count;
}

bool ShouldReservePreferredCapacityForSla1(const vector<MachineId_t> & machines,
                                           const unordered_map<CPUType_t, uint64_t> & best_capacity_by_cpu,
                                           const unordered_map<MachineId_t, array<unsigned, NUM_SLAS>> & machine_sla_vm_counts,
                                           TaskId_t task_id,
                                           MachineId_t machine_id) {
    if(RequiredSLA(task_id) != SLA0 ||
       !MachineIsPreferredForCpu(best_capacity_by_cpu, RequiredCPUType(task_id), machine_id)) {
        return false;
    }

    auto counts_it = machine_sla_vm_counts.find(machine_id);
    bool machine_already_hosts_sla0 = counts_it != machine_sla_vm_counts.end() &&
                                      counts_it->second[SLA0] > 0;
    if(machine_already_hosts_sla0) {
        return false;
    }

    unsigned preferred_machines = CountPreferredMachinesForCpu(machines,
                                                               best_capacity_by_cpu,
                                                               RequiredCPUType(task_id),
                                                               false,
                                                               machine_sla_vm_counts);
    if(preferred_machines <= 1) {
        return false;
    }

    unsigned reserved_for_non_sla0 = max(1u, preferred_machines / kPreferredTierReserveDivisor);
    unsigned sla0_machines = CountPreferredMachinesForCpu(machines,
                                                          best_capacity_by_cpu,
                                                          RequiredCPUType(task_id),
                                                          true,
                                                          machine_sla_vm_counts);
    return sla0_machines + reserved_for_non_sla0 >= preferred_machines;
}

bool ShouldAvoidMixedSlaMachine(const unordered_map<MachineId_t, array<unsigned, NUM_SLAS>> & machine_sla_vm_counts,
                                TaskId_t task_id,
                                MachineId_t machine_id) {
    auto counts_it = machine_sla_vm_counts.find(machine_id);
    if(counts_it == machine_sla_vm_counts.end()) {
        return false;
    }

    SLAType_t task_sla = RequiredSLA(task_id);
    if(task_sla == SLA0) {
        return counts_it->second[SLA1] > 0;
    }
    if(task_sla == SLA1) {
        return counts_it->second[SLA0] > 0;
    }
    return false;
}

int PlacementBucket(const vector<MachineId_t> & machines,
                    const unordered_map<CPUType_t, uint64_t> & best_capacity_by_cpu,
                    const unordered_map<MachineId_t, array<unsigned, NUM_SLAS>> & machine_sla_vm_counts,
                    TaskId_t task_id,
                    MachineId_t machine_id,
                    bool is_overloaded,
                    bool is_protected) {
    int bucket = 0;
    if(is_protected) {
        bucket += 4;
    } else if(is_overloaded) {
        bucket += 2;
    }
    if(!IsPreferredPerformanceTier(best_capacity_by_cpu, task_id, machine_id)) {
        bucket += 1;
    }
    if(ShouldReservePreferredCapacityForSla1(machines,
                                             best_capacity_by_cpu,
                                             machine_sla_vm_counts,
                                             task_id,
                                             machine_id)) {
        bucket += 3;
    }
    if(ShouldAvoidMixedSlaMachine(machine_sla_vm_counts, task_id, machine_id)) {
        bucket += 3;
    }
    return bucket;
}

Priority_t PriorityForTask(TaskId_t task_id) {
    switch(RequiredSLA(task_id)) {
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

bool IsMachineReady(MachineId_t machine_id) {
    return Machine_GetInfo(machine_id).s_state == S0;
}

bool HasMachineMemoryForTask(MachineId_t machine_id, TaskId_t task_id, bool creating_vm) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    unsigned needed_memory = GetTaskMemory(task_id) + (creating_vm ? VM_MEMORY_OVERHEAD : 0);
    return machine_info.memory_used + needed_memory <= machine_info.memory_size;
}

double ProjectedLoad(MachineId_t machine_id, TaskId_t task_id, bool creating_vm) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    double projected_tasks = static_cast<double>(machine_info.active_tasks + 1) /
                             std::max(1u, machine_info.num_cpus);
    unsigned projected_memory = machine_info.memory_used + GetTaskMemory(task_id) +
                                (creating_vm ? VM_MEMORY_OVERHEAD : 0);
    double memory_pressure = static_cast<double>(projected_memory) /
                             std::max(1u, machine_info.memory_size);
    return std::max(projected_tasks, memory_pressure);
}

bool CanVmHostTask(VMId_t vm_id,
                   TaskId_t task_id,
                   const unordered_map<VMId_t, SLAType_t> & vm_slas,
                   MachineId_t & machine_id_out,
                   double & projected_load_out) {
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    auto vm_sla_it = vm_slas.find(vm_id);
    if(vm_info.cpu != RequiredCPUType(task_id) ||
       vm_info.vm_type != RequiredVMType(task_id) ||
       (vm_sla_it != vm_slas.end() && vm_sla_it->second != RequiredSLA(task_id)) ||
       !IsMachineReady(vm_info.machine_id) ||
       !HasMachineMemoryForTask(vm_info.machine_id, task_id, false)) {
        return false;
    }

    machine_id_out = vm_info.machine_id;
    projected_load_out = ProjectedLoad(vm_info.machine_id, task_id, false);
    return true;
}

}  // namespace

Scheduler::HostState Scheduler::ClassifyMachine(MachineId_t machine_id) const {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    double task_pressure = static_cast<double>(machine_info.active_tasks) /
                           std::max(1u, machine_info.num_cpus);
    double memory_pressure = static_cast<double>(machine_info.memory_used) /
                             std::max(1u, machine_info.memory_size);
    double load_score = std::max(task_pressure, memory_pressure);

    if(load_score >= kOverloadThreshold) {
        return HostState::OVERLOADED;
    }
    if(load_score <= kUnderloadThreshold) {
        return HostState::UNDERLOADED;
    }
    return HostState::NORMAL;
}

void Scheduler::RefreshMachineStates() {
    host_states.clear();
    for(MachineId_t machine_id : machines) {
        host_states[machine_id] = ClassifyMachine(machine_id);
    }
}

bool Scheduler::TryPlaceTask(TaskId_t task_id, bool log_failure) {
    RefreshMachineStates();
    Priority_t priority = PriorityForTask(task_id);
    VMId_t best_existing_vm = numeric_limits<VMId_t>::max();
    int best_existing_bucket = numeric_limits<int>::max();
    double best_existing_load = numeric_limits<double>::max();
    Time_t now = Now();

    for(VMId_t vm_id : vms) {
        MachineId_t machine_id = 0;
        double projected_load = 0.0;
        if(!CanVmHostTask(vm_id, task_id, vm_slas, machine_id, projected_load)) {
            continue;
        }

        int bucket = PlacementBucket(machines,
                                     best_capacity_by_cpu,
                                     machine_sla_vm_counts,
                                     task_id,
                                     machine_id,
                                     host_states[machine_id] == HostState::OVERLOADED,
                                     IsMachineProtected(machine_id, now));
        if(projected_load <= kOverloadThreshold) {
            bucket = max(0, bucket - 1);
        }

        if(bucket < best_existing_bucket ||
           (bucket == best_existing_bucket && projected_load < best_existing_load)) {
            best_existing_bucket = bucket;
            best_existing_load = projected_load;
            best_existing_vm = vm_id;
        }
    }

    MachineId_t best_machine = numeric_limits<MachineId_t>::max();
    int best_machine_bucket = numeric_limits<int>::max();
    double best_machine_load = numeric_limits<double>::max();

    for(MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        if(machine_info.cpu != RequiredCPUType(task_id) || !IsMachineReady(machine_id)) {
            continue;
        }
        if(!HasMachineMemoryForTask(machine_id, task_id, true)) {
            SimOutput("Scheduler::NewTask(): Skipping machine " + to_string(machine_id) +
                      " for task " + to_string(task_id) + " due to memory pressure", 3);
            continue;
        }

        double projected_load = ProjectedLoad(machine_id, task_id, true);
        int bucket = PlacementBucket(machines,
                                     best_capacity_by_cpu,
                                     machine_sla_vm_counts,
                                     task_id,
                                     machine_id,
                                     host_states[machine_id] == HostState::OVERLOADED,
                                     IsMachineProtected(machine_id, now));
        if(projected_load <= kOverloadThreshold) {
            bucket = max(0, bucket - 1);
        }

        if(bucket < best_machine_bucket ||
           (bucket == best_machine_bucket && projected_load < best_machine_load)) {
            best_machine_bucket = bucket;
            best_machine_load = projected_load;
            best_machine = machine_id;
        }
    }

    bool use_existing_vm = false;
    if(best_existing_vm != numeric_limits<VMId_t>::max()) {
        use_existing_vm = true;
        if(best_machine != numeric_limits<MachineId_t>::max()) {
            bool new_machine_is_better =
                (best_machine_bucket < best_existing_bucket) ||
                (best_machine_bucket == best_existing_bucket &&
                 best_machine_load < best_existing_load) ||
                (best_machine_bucket == best_existing_bucket &&
                 best_machine_load == best_existing_load &&
                 RequiredSLA(task_id) == SLA0);
            if(new_machine_is_better) {
                use_existing_vm = false;
            }
        }
    }

    if(use_existing_vm) {
        VM_AddTask(best_existing_vm, task_id, priority);
        task_to_vm[task_id] = best_existing_vm;
        SimOutput("Scheduler::NewTask(): Added task " + to_string(task_id) +
                  " to existing VM " + to_string(best_existing_vm) +
                  " bucket=" + to_string(best_existing_bucket) +
                  " projected_load=" + to_string(best_existing_load), 2);
        return true;
    }

    if(best_machine != numeric_limits<MachineId_t>::max()) {
        VMId_t vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));
        VM_Attach(vm_id, best_machine);
        vms.push_back(vm_id);
        vm_slas[vm_id] = RequiredSLA(task_id);
        machine_sla_vm_counts[best_machine][RequiredSLA(task_id)]++;
        VM_AddTask(vm_id, task_id, priority);
        task_to_vm[task_id] = vm_id;
        SimOutput("Scheduler::NewTask(): Created VM " + to_string(vm_id) +
                  " on machine " + to_string(best_machine) +
                  " for task " + to_string(task_id) +
                  " bucket=" + to_string(best_machine_bucket) +
                  " projected_load=" + to_string(best_machine_load), 1);
        return true;
    }

    if(log_failure) {
        TaskInfo_t task_info = GetTaskInfo(task_id);
        SimOutput("Scheduler::NewTask(): failed to place task " + to_string(task_id) +
                  " cpu=" + to_string(task_info.required_cpu) +
                  " vm=" + to_string(task_info.required_vm) +
                  " mem=" + to_string(task_info.required_memory), 2);
        for(MachineId_t machine_id : machines) {
            MachineInfo_t machine_info = Machine_GetInfo(machine_id);
            string reason;
            if(machine_info.cpu != RequiredCPUType(task_id)) {
                reason = "cpu_mismatch";
            } else if(!IsMachineReady(machine_id)) {
                reason = "not_ready_state=" + to_string(machine_info.s_state);
            } else if(!HasMachineMemoryForTask(machine_id, task_id, true)) {
                reason = "no_memory used=" + to_string(machine_info.memory_used) +
                         " size=" + to_string(machine_info.memory_size);
            } else if(host_states[machine_id] == HostState::OVERLOADED) {
                reason = "classified_overloaded";
            } else if(ProjectedLoad(machine_id, task_id, true) > kOverloadThreshold) {
                reason = "overload_threshold projected=" +
                         to_string(ProjectedLoad(machine_id, task_id, true));
            } else {
                reason = "candidate_unused";
            }

            SimOutput("Scheduler::NewTask(): machine " + to_string(machine_id) +
                      " reason=" + reason +
                      " active_tasks=" + to_string(machine_info.active_tasks) +
                      " active_vms=" + to_string(machine_info.active_vms), 3);
        }
    }

    return false;
}

bool Scheduler::TryWakeMachineForTask(TaskId_t task_id) {
    MachineId_t best_machine = numeric_limits<MachineId_t>::max();
    double best_machine_load = numeric_limits<double>::max();

    for(MachineId_t machine_id : machines) {
        if(pending_state_changes.count(machine_id) != 0) {
            continue;
        }

        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        if(machine_info.cpu != RequiredCPUType(task_id) || machine_info.s_state == S0) {
            continue;
        }
        if(!HasMachineMemoryForTask(machine_id, task_id, true)) {
            continue;
        }

        double projected_load = ProjectedLoad(machine_id, task_id, true);
        if(projected_load < best_machine_load) {
            best_machine_load = projected_load;
            best_machine = machine_id;
        }
    }

    if(best_machine == numeric_limits<MachineId_t>::max()) {
        return false;
    }

    pending_state_changes.insert(best_machine);
    Machine_SetState(best_machine, S0);
    SimOutput("Scheduler::TryWakeMachineForTask(): waking machine " +
              to_string(best_machine) + " for task " + to_string(task_id), 1);
    return true;
}

unsigned Scheduler::CountReadyMachines(CPUType_t cpu_type, MachineId_t exclude_machine) const {
    unsigned ready_count = 0;
    for(MachineId_t candidate : machines) {
        if(candidate == exclude_machine) {
            continue;
        }
        if(pending_state_changes.count(candidate) != 0) {
            continue;
        }

        MachineInfo_t machine_info = Machine_GetInfo(candidate);
        if(machine_info.cpu == cpu_type && machine_info.s_state == S0) {
            ready_count++;
        }
    }
    return ready_count;
}

void Scheduler::MaybeSleepMachine(MachineId_t machine_id, Time_t now) {
    if(!kEnableIdleSleep) {
        return;
    }
    if(!waiting_tasks.empty()) {
        return;
    }
    if(pending_state_changes.count(machine_id) != 0) {
        return;
    }
    if(IsMachineProtected(machine_id, now)) {
        return;
    }

    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if(machine_info.s_state != S0 || machine_info.active_tasks != 0 || machine_info.active_vms != 0) {
        return;
    }
    if(CountReadyMachines(machine_info.cpu, machine_id) < (kMinReadyMachinesPerCpu - 1)) {
        return;
    }

    pending_state_changes.insert(machine_id);
    Machine_SetState(machine_id, S0i1);
    SimOutput("Scheduler::MaybeSleepMachine(): moving idle machine " +
              to_string(machine_id) + " to S0i1 at time " + to_string(now), 1);
}

void Scheduler::ProtectMachine(MachineId_t machine_id, Time_t now) {
    Time_t until = now + kProtectionWindow;
    auto it = protected_until.find(machine_id);
    if(it == protected_until.end() || it->second < until) {
        protected_until[machine_id] = until;
    }
}

bool Scheduler::IsMachineProtected(MachineId_t machine_id, Time_t now) const {
    auto it = protected_until.find(machine_id);
    return it != protected_until.end() && it->second > now;
}

void Scheduler::RetryWaitingTasks(Time_t now) {
    if(waiting_tasks.empty()) {
        return;
    }

    vector<TaskId_t> pending_tasks;
    pending_tasks.swap(waiting_tasks);
    for(TaskId_t task_id : pending_tasks) {
        if(IsTaskCompleted(task_id)) {
            continue;
        }
        if(TryPlaceTask(task_id, false)) {
            SimOutput("Scheduler::RetryWaitingTasks(): placed waiting task " +
                      to_string(task_id) + " at time " + to_string(now), 2);
        } else {
            waiting_tasks.push_back(task_id);
        }
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
    unsigned total_machines = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    for(unsigned i = 0; i < total_machines; i++) {
        machines.push_back(MachineId_t(i));
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        uint64_t machine_capacity = MachineCapacityScore(MachineId_t(i));
        auto it = best_capacity_by_cpu.find(info.cpu);
        if(it == best_capacity_by_cpu.end() || it->second < machine_capacity) {
            best_capacity_by_cpu[info.cpu] = machine_capacity;
        }
        machine_sla_vm_counts[MachineId_t(i)].fill(0);
        SimOutput("Scheduler::Init(): machine " + to_string(i) +
                  " cpu=" + to_string(info.cpu) +
                  " mem=" + to_string(info.memory_size) +
                  " cpus=" + to_string(info.num_cpus) +
                  " gpu=" + string(info.gpus ? "yes" : "no"), 2);
    }
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
    if(!TryPlaceTask(task_id, true)) {
        TryWakeMachineForTask(task_id);
        waiting_tasks.push_back(task_id);
        SimOutput("Scheduler::NewTask(): queued task " + to_string(task_id) +
                  " at time " + to_string(now) + " waiting_count=" +
                  to_string(waiting_tasks.size()), 1);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    RefreshMachineStates();
    for(MachineId_t machine_id : machines) {
        MaybeSleepMachine(machine_id, now);
    }
    RetryWaitingTasks(now);
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    auto vm_it = task_to_vm.find(task_id);
    if(vm_it != task_to_vm.end()) {
        VMId_t vm_id = vm_it->second;
        task_to_vm.erase(vm_it);

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if(vm_info.active_tasks.empty()) {
            auto vm_sla_it = vm_slas.find(vm_id);
            if(vm_sla_it != vm_slas.end()) {
                auto machine_counts_it = machine_sla_vm_counts.find(vm_info.machine_id);
                if(machine_counts_it != machine_sla_vm_counts.end() &&
                   machine_counts_it->second[vm_sla_it->second] > 0) {
                    machine_counts_it->second[vm_sla_it->second]--;
                }
            }
            VM_Shutdown(vm_id);
            vms.erase(remove(vms.begin(), vms.end(), vm_id), vms.end());
            vm_slas.erase(vm_id);
            SimOutput("Scheduler::TaskComplete(): Shut down empty VM " + to_string(vm_id), 2);
        }
    }

    RetryWaitingTasks(now);
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}

void Scheduler::StateChangeFinished(Time_t time, MachineId_t machine_id) {
    pending_state_changes.erase(machine_id);
    SimOutput("Scheduler::StateChangeFinished(): machine " + to_string(machine_id) +
              " completed state change at time " + to_string(time), 2);
    RetryWaitingTasks(time);
}

void Scheduler::HandleSLAWarning(Time_t time, TaskId_t task_id) {
    SetTaskPriority(task_id, HIGH_PRIORITY);

    auto vm_it = task_to_vm.find(task_id);
    if(vm_it == task_to_vm.end()) {
        SimOutput("Scheduler::HandleSLAWarning(): raised priority for task " +
                  to_string(task_id) + " at time " + to_string(time), 1);
        return;
    }

    VMInfo_t vm_info = VM_GetInfo(vm_it->second);
    ProtectMachine(vm_info.machine_id, time);
    SimOutput("Scheduler::HandleSLAWarning(): task " + to_string(task_id) +
              " warned at time " + to_string(time) +
              ", raised to HIGH priority and protected machine " +
              to_string(vm_info.machine_id), 1);

    TryWakeMachineForTask(task_id);
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
    Scheduler.HandleSLAWarning(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
    Scheduler.StateChangeFinished(time, machine_id);
}
