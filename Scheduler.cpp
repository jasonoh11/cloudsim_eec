//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace {

constexpr double kOverloadThreshold = 0.80;
constexpr double kUnderloadThreshold = 0.25;
constexpr double kMigrationTriggerThreshold = 1.00;
constexpr unsigned kMinReadyMachinesPerCpu = 3;
constexpr unsigned kMinReadyPreferredMachinesPerCpu = 8;
constexpr unsigned kMinReadyGpuMachinesPerCpu = 4;
constexpr Time_t kProtectionWindow = 5'000'000;
constexpr Time_t kIdleSleepDelay = 2'000'000;
constexpr Time_t kPreferredIdleSleepDelay = 10'000'000;
constexpr Time_t kEnergyDebugInterval = 5'000'000;
constexpr bool kEnableIdleSleep = true;
constexpr bool kEnableUnderloadEvacuation = false;
constexpr unsigned kPreferredTierReserveDivisor = 4;

uint64_t MachineCapacityScore(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    unsigned base_mips = machine_info.performance.empty() ? 0 : machine_info.performance[0];
    return static_cast<uint64_t>(machine_info.num_cpus) * base_mips;
}

unsigned EstimatedVmMemory(VMId_t vm_id) {
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    unsigned total_memory = VM_MEMORY_OVERHEAD;
    for(TaskId_t task_id : vm_info.active_tasks) {
        total_memory += GetTaskMemory(task_id);
    }
    return total_memory;
}

double MachineLoadScore(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    double task_pressure = static_cast<double>(machine_info.active_tasks) /
                           std::max(1u, machine_info.num_cpus);
    double memory_pressure = static_cast<double>(machine_info.memory_used) /
                             std::max(1u, machine_info.memory_size);
    return std::max(task_pressure, memory_pressure);
}

double ProjectedLoadAfterAddingVm(MachineId_t machine_id, VMId_t vm_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    unsigned vm_memory = EstimatedVmMemory(vm_id);
    double projected_tasks = static_cast<double>(machine_info.active_tasks + vm_info.active_tasks.size()) /
                             std::max(1u, machine_info.num_cpus);
    double projected_memory = static_cast<double>(machine_info.memory_used + vm_memory) /
                              std::max(1u, machine_info.memory_size);
    return std::max(projected_tasks, projected_memory);
}

double ProjectedLoadAfterRemovingVm(MachineId_t machine_id, VMId_t vm_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    unsigned vm_memory = EstimatedVmMemory(vm_id);
    unsigned remaining_tasks = machine_info.active_tasks > vm_info.active_tasks.size() ?
                               machine_info.active_tasks - static_cast<unsigned>(vm_info.active_tasks.size()) : 0;
    unsigned remaining_memory = machine_info.memory_used > vm_memory ?
                                machine_info.memory_used - vm_memory : 0;
    double task_pressure = static_cast<double>(remaining_tasks) /
                           std::max(1u, machine_info.num_cpus);
    double memory_pressure = static_cast<double>(remaining_memory) /
                             std::max(1u, machine_info.memory_size);
    return std::max(task_pressure, memory_pressure);
}

bool ShouldReservePreferredCapacityForSla1(SLAType_t task_sla,
                                           bool machine_is_preferred,
                                           bool machine_already_hosts_sla0,
                                           unsigned preferred_machines,
                                           unsigned preferred_sla0_machines) {
    if(task_sla != SLA0 || !machine_is_preferred) {
        return false;
    }
    if(machine_already_hosts_sla0) {
        return false;
    }
    if(preferred_machines <= 1) {
        return false;
    }

    unsigned reserved_for_non_sla0 = max(1u, preferred_machines / kPreferredTierReserveDivisor);
    return preferred_sla0_machines + reserved_for_non_sla0 >= preferred_machines;
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

int PlacementBucket(bool machine_is_preferred,
                    bool reserve_preferred_capacity,
                    bool avoid_mixed_sla_machine,
                    bool is_overloaded,
                    bool is_protected) {
    int bucket = 0;
    if(is_protected) {
        bucket += 4;
    } else if(is_overloaded) {
        bucket += 2;
    }
    if(!machine_is_preferred) {
        bucket += 1;
    }
    if(reserve_preferred_capacity) {
        bucket += 3;
    }
    if(avoid_mixed_sla_machine) {
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

bool IsPreferredMachineForCpu(const unordered_map<CPUType_t, uint64_t> & best_capacity_by_cpu,
                              const MachineInfo_t & machine_info) {
    auto best_it = best_capacity_by_cpu.find(machine_info.cpu);
    uint64_t best_score = (best_it == best_capacity_by_cpu.end()) ? 0 : best_it->second;
    if(best_score == 0) {
        return true;
    }
    unsigned base_mips = machine_info.performance.empty() ? 0 : machine_info.performance[0];
    uint64_t machine_score = static_cast<uint64_t>(machine_info.num_cpus) * base_mips;
    return machine_score * 100 >= best_score * 75;
}

const char * CpuTypeName(CPUType_t cpu_type) {
    switch(cpu_type) {
        case X86:
            return "X86";
        case ARM:
            return "ARM";
        case POWER:
            return "POWER";
        case RISCV:
            return "RISCV";
        default:
            return "UNKNOWN";
    }
}

const char * MachineStateName(MachineState_t state) {
    switch(state) {
        case S0:
            return "S0";
        case S0i1:
            return "S0i1";
        case S1:
            return "S1";
        case S2:
            return "S2";
        case S3:
            return "S3";
        case S4:
            return "S4";
        case S5:
            return "S5";
        default:
            return "UNKNOWN";
    }
}

}  // namespace

Scheduler::HostState Scheduler::ClassifyMachine(MachineId_t machine_id) const {
    double load_score = MachineLoadScore(machine_id);

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
    CPUType_t required_cpu = RequiredCPUType(task_id);
    VMType_t required_vm = RequiredVMType(task_id);
    SLAType_t required_sla = RequiredSLA(task_id);
    unsigned required_memory = GetTaskMemory(task_id);
    VMId_t best_existing_vm = numeric_limits<VMId_t>::max();
    int best_existing_bucket = numeric_limits<int>::max();
    double best_existing_load = numeric_limits<double>::max();
    Time_t now = Now();
    unordered_map<MachineId_t, MachineInfo_t> machine_infos;
    unordered_map<MachineId_t, bool> machine_is_preferred;
    machine_infos.reserve(machines.size());
    machine_is_preferred.reserve(machines.size());

    unsigned preferred_machines = 0;
    unsigned preferred_sla0_machines = 0;
    for(MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        machine_infos.emplace(machine_id, machine_info);

        bool is_preferred = true;
        auto best_it = best_capacity_by_cpu.find(required_cpu);
        uint64_t best_score = (best_it == best_capacity_by_cpu.end()) ? 0 : best_it->second;
        if((required_sla == SLA0 || required_sla == SLA1) &&
           machine_info.cpu == required_cpu &&
           best_score != 0) {
            unsigned base_mips = machine_info.performance.empty() ? 0 : machine_info.performance[0];
            uint64_t machine_score = static_cast<uint64_t>(machine_info.num_cpus) * base_mips;
            is_preferred = machine_score * 100 >= best_score * 75;
        }
        machine_is_preferred[machine_id] = is_preferred;

        if(machine_info.cpu == required_cpu && is_preferred) {
            preferred_machines++;
            auto counts_it = machine_sla_vm_counts.find(machine_id);
            if(counts_it != machine_sla_vm_counts.end() && counts_it->second[SLA0] > 0) {
                preferred_sla0_machines++;
            }
        }
    }

    auto has_machine_memory_for_task = [&](MachineId_t machine_id, bool creating_vm) {
        const MachineInfo_t & machine_info = machine_infos.at(machine_id);
        unsigned needed_memory = required_memory + (creating_vm ? VM_MEMORY_OVERHEAD : 0);
        return machine_info.memory_used + needed_memory <= machine_info.memory_size;
    };

    auto projected_load = [&](MachineId_t machine_id, bool creating_vm) {
        const MachineInfo_t & machine_info = machine_infos.at(machine_id);
        double projected_tasks = static_cast<double>(machine_info.active_tasks + 1) /
                                 std::max(1u, machine_info.num_cpus);
        unsigned projected_memory = machine_info.memory_used + required_memory +
                                    (creating_vm ? VM_MEMORY_OVERHEAD : 0);
        double memory_pressure = static_cast<double>(projected_memory) /
                                 std::max(1u, machine_info.memory_size);
        return std::max(projected_tasks, memory_pressure);
    };

    auto placement_bucket_for_machine = [&](MachineId_t machine_id) {
        auto counts_it = machine_sla_vm_counts.find(machine_id);
        bool machine_already_hosts_sla0 = counts_it != machine_sla_vm_counts.end() &&
                                          counts_it->second[SLA0] > 0;
        bool reserve_preferred_capacity = ShouldReservePreferredCapacityForSla1(required_sla,
                                                                               machine_is_preferred.at(machine_id),
                                                                               machine_already_hosts_sla0,
                                                                               preferred_machines,
                                                                               preferred_sla0_machines);
        bool avoid_mixed = ShouldAvoidMixedSlaMachine(machine_sla_vm_counts, task_id, machine_id);
        return PlacementBucket(machine_is_preferred.at(machine_id),
                               reserve_preferred_capacity,
                               avoid_mixed,
                               host_states[machine_id] == HostState::OVERLOADED,
                               IsMachineProtected(machine_id, now));
    };

    for(VMId_t vm_id : vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        auto vm_sla_it = vm_slas.find(vm_id);
        if(vm_info.cpu != required_cpu ||
           vm_info.vm_type != required_vm ||
           (vm_sla_it != vm_slas.end() && vm_sla_it->second != required_sla) ||
           migrating_vms.count(vm_id) != 0) {
            continue;
        }
        MachineId_t machine_id = vm_info.machine_id;
        const MachineInfo_t & machine_info = machine_infos.at(machine_id);
        if(pending_state_changes.count(machine_id) != 0 ||
           machine_info.s_state != S0 ||
           !has_machine_memory_for_task(machine_id, false)) {
            continue;
        }
        double vm_projected_load = projected_load(machine_id, false);

        int bucket = placement_bucket_for_machine(machine_id);
        if(vm_projected_load <= kOverloadThreshold) {
            bucket = max(0, bucket - 1);
        }

        if(bucket < best_existing_bucket ||
           (bucket == best_existing_bucket && vm_projected_load < best_existing_load)) {
            best_existing_bucket = bucket;
            best_existing_load = vm_projected_load;
            best_existing_vm = vm_id;
        }
    }

    MachineId_t best_machine = numeric_limits<MachineId_t>::max();
    int best_machine_bucket = numeric_limits<int>::max();
    double best_machine_load = numeric_limits<double>::max();

    for(MachineId_t machine_id : machines) {
        const MachineInfo_t & machine_info = machine_infos.at(machine_id);
        if(pending_state_changes.count(machine_id) != 0 ||
           machine_info.cpu != required_cpu ||
           machine_info.s_state != S0) {
            continue;
        }
        if(!has_machine_memory_for_task(machine_id, true)) {
            SimOutput("Scheduler::NewTask(): Skipping machine " + to_string(machine_id) +
                      " for task " + to_string(task_id) + " due to memory pressure", 3);
            continue;
        }

        double machine_projected_load = projected_load(machine_id, true);
        int bucket = placement_bucket_for_machine(machine_id);
        if(machine_projected_load <= kOverloadThreshold) {
            bucket = max(0, bucket - 1);
        }

        if(bucket < best_machine_bucket ||
           (bucket == best_machine_bucket && machine_projected_load < best_machine_load)) {
            best_machine_bucket = bucket;
            best_machine_load = machine_projected_load;
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
        VMInfo_t vm_info = VM_GetInfo(best_existing_vm);
        MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);
        if(pending_state_changes.count(vm_info.machine_id) != 0 ||
           machine_info.s_state != S0) {
            SimOutput("Scheduler::NewTask(): existing VM " + to_string(best_existing_vm) +
                      " became unavailable before placement on machine " +
                      to_string(vm_info.machine_id), 1);
            use_existing_vm = false;
        }
    }

    if(use_existing_vm) {
        VMInfo_t vm_info = VM_GetInfo(best_existing_vm);
        idle_since.erase(vm_info.machine_id);
        VM_AddTask(best_existing_vm, task_id, priority);
        task_to_vm[task_id] = best_existing_vm;
        SimOutput("Scheduler::NewTask(): Added task " + to_string(task_id) +
                  " to existing VM " + to_string(best_existing_vm) +
                  " bucket=" + to_string(best_existing_bucket) +
                  " projected_load=" + to_string(best_existing_load), 2);
        return true;
    }

    if(best_machine != numeric_limits<MachineId_t>::max()) {
        MachineInfo_t latest_machine_info = Machine_GetInfo(best_machine);
        if(pending_state_changes.count(best_machine) != 0 ||
           latest_machine_info.s_state != S0) {
            SimOutput("Scheduler::NewTask(): machine " + to_string(best_machine) +
                      " became unavailable before VM attach for task " +
                      to_string(task_id), 1);
            return false;
        }
        idle_since.erase(best_machine);
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

    idle_since.erase(best_machine);
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

unsigned Scheduler::CountReadyPreferredMachines(CPUType_t cpu_type, MachineId_t exclude_machine) const {
    unsigned ready_count = 0;
    for(MachineId_t candidate : machines) {
        if(candidate == exclude_machine) {
            continue;
        }
        if(pending_state_changes.count(candidate) != 0) {
            continue;
        }

        MachineInfo_t machine_info = Machine_GetInfo(candidate);
        if(machine_info.cpu == cpu_type &&
           machine_info.s_state == S0 &&
           IsPreferredMachineForCpu(best_capacity_by_cpu, machine_info)) {
            ready_count++;
        }
    }
    return ready_count;
}

unsigned Scheduler::CountReadyGpuMachines(CPUType_t cpu_type, MachineId_t exclude_machine) const {
    unsigned ready_count = 0;
    for(MachineId_t candidate : machines) {
        if(candidate == exclude_machine) {
            continue;
        }
        if(pending_state_changes.count(candidate) != 0) {
            continue;
        }

        MachineInfo_t machine_info = Machine_GetInfo(candidate);
        if(machine_info.cpu == cpu_type &&
           machine_info.s_state == S0 &&
           machine_info.gpus) {
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
        idle_since.erase(machine_id);
        return;
    }
    if(pending_state_changes.count(machine_id) != 0) {
        idle_since.erase(machine_id);
        return;
    }
    if(IsMachineProtected(machine_id, now)) {
        idle_since.erase(machine_id);
        return;
    }
    for(const auto & destination_entry : migration_destinations) {
        if(destination_entry.second == machine_id) {
            idle_since.erase(machine_id);
            return;
        }
    }

    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    bool is_preferred = IsPreferredMachineForCpu(best_capacity_by_cpu, machine_info);
    if(machine_info.s_state != S0 || machine_info.active_tasks != 0 || machine_info.active_vms != 0) {
        idle_since.erase(machine_id);
        return;
    }

    if(machine_info.gpus &&
       CountReadyGpuMachines(machine_info.cpu, machine_id) < kMinReadyGpuMachinesPerCpu) {
        return;
    }

    if(is_preferred) {
        if(CountReadyPreferredMachines(machine_info.cpu, machine_id) < kMinReadyPreferredMachinesPerCpu) {
            return;
        }
    } else if(CountReadyMachines(machine_info.cpu, machine_id) < kMinReadyMachinesPerCpu) {
        return;
    }

    auto idle_it = idle_since.find(machine_id);
    if(idle_it == idle_since.end()) {
        idle_since[machine_id] = now;
        return;
    }
    Time_t idle_delay = is_preferred ? kPreferredIdleSleepDelay : kIdleSleepDelay;
    if(now - idle_it->second < idle_delay) {
        return;
    }

    idle_since.erase(machine_id);
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

bool Scheduler::TryRelieveMachineOverload(MachineId_t best_source, Time_t now) {
    if(!migrating_vms.empty()) {
        return false;
    }

    if(best_source == numeric_limits<MachineId_t>::max() ||
       IsMachineProtected(best_source, now)) {
        return false;
    }

    double best_source_load = MachineLoadScore(best_source);

    VMId_t best_vm = numeric_limits<VMId_t>::max();
    MachineId_t best_destination = numeric_limits<MachineId_t>::max();
    double best_reduced_load = best_source_load;
    double best_destination_load = numeric_limits<double>::max();
    unsigned best_vm_memory = numeric_limits<unsigned>::max();

    for(VMId_t vm_id : vms) {
        if(migrating_vms.count(vm_id) != 0) {
            continue;
        }

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if(vm_info.machine_id != best_source || vm_info.active_tasks.empty()) {
            continue;
        }

        double reduced_source_load = ProjectedLoadAfterRemovingVm(best_source, vm_id);
        if(reduced_source_load >= best_source_load) {
            continue;
        }

        unsigned vm_memory = EstimatedVmMemory(vm_id);
        MachineId_t candidate_destination = numeric_limits<MachineId_t>::max();
        double candidate_destination_load = numeric_limits<double>::max();

        for(MachineId_t machine_id : machines) {
            if(machine_id == best_source) {
                continue;
            }

            MachineInfo_t machine_info = Machine_GetInfo(machine_id);
            if(machine_info.cpu != vm_info.cpu ||
               !IsMachineReady(machine_id) ||
               host_states[machine_id] == HostState::OVERLOADED ||
               IsMachineProtected(machine_id, now) ||
               machine_info.memory_used + vm_memory > machine_info.memory_size) {
                continue;
            }

            double projected_destination_load = ProjectedLoadAfterAddingVm(machine_id, vm_id);
            if(projected_destination_load >= kOverloadThreshold) {
                continue;
            }

            if(projected_destination_load < candidate_destination_load) {
                candidate_destination_load = projected_destination_load;
                candidate_destination = machine_id;
            }
        }

        if(candidate_destination == numeric_limits<MachineId_t>::max()) {
            continue;
        }

        if(reduced_source_load < best_reduced_load ||
           (reduced_source_load == best_reduced_load && candidate_destination_load < best_destination_load) ||
           (reduced_source_load == best_reduced_load && candidate_destination_load == best_destination_load &&
            vm_memory < best_vm_memory)) {
            best_reduced_load = reduced_source_load;
            best_destination_load = candidate_destination_load;
            best_vm_memory = vm_memory;
            best_vm = vm_id;
            best_destination = candidate_destination;
        }
    }

    if(best_vm == numeric_limits<VMId_t>::max()) {
        return false;
    }

    migrating_vms.insert(best_vm);
    migration_sources[best_vm] = best_source;
    migration_destinations[best_vm] = best_destination;
    VM_Migrate(best_vm, best_destination);
    SimOutput("Scheduler::MaybeRelieveOverload(): migrating VM " + to_string(best_vm) +
              " from machine " + to_string(best_source) +
              " to machine " + to_string(best_destination) +
              " source_load=" + to_string(best_source_load) +
              " reduced_load=" + to_string(best_reduced_load), 1);
    return true;
}

void Scheduler::MaybeRelieveOverload(Time_t now) {
    MachineId_t best_source = numeric_limits<MachineId_t>::max();
    double best_source_load = kMigrationTriggerThreshold;
    for(MachineId_t machine_id : machines) {
        double source_load = MachineLoadScore(machine_id);
        if(host_states[machine_id] != HostState::OVERLOADED ||
           source_load < kMigrationTriggerThreshold ||
           IsMachineProtected(machine_id, now)) {
            continue;
        }
        if(source_load > best_source_load) {
            best_source_load = source_load;
            best_source = machine_id;
        }
    }

    TryRelieveMachineOverload(best_source, now);
}

void Scheduler::MaybeEvacuateUnderload(Time_t now) {
    if(!kEnableUnderloadEvacuation) {
        return;
    }
    if(!migrating_vms.empty()) {
        return;
    }

    auto source_is_still_valid = [&](MachineId_t machine_id) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        return host_states[machine_id] == HostState::UNDERLOADED &&
               machine_info.active_vms > 0 &&
               machine_info.s_state == S0 &&
               !IsMachineProtected(machine_id, now);
    };

    if(evacuating_machine.has_value() && !source_is_still_valid(*evacuating_machine)) {
        evacuating_machine.reset();
    }

    if(!evacuating_machine.has_value()) {
        MachineId_t best_source = numeric_limits<MachineId_t>::max();
        double best_source_load = numeric_limits<double>::max();
        for(MachineId_t machine_id : machines) {
            if(!source_is_still_valid(machine_id)) {
                continue;
            }

            double source_load = MachineLoadScore(machine_id);
            if(source_load < best_source_load) {
                best_source_load = source_load;
                best_source = machine_id;
            }
        }

        if(best_source == numeric_limits<MachineId_t>::max()) {
            return;
        }
        evacuating_machine = best_source;
    }

    MachineId_t source = *evacuating_machine;
    vector<VMId_t> source_vms;
    for(VMId_t vm_id : vms) {
        if(migrating_vms.count(vm_id) != 0) {
            continue;
        }
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if(vm_info.machine_id == source && !vm_info.active_tasks.empty()) {
            source_vms.push_back(vm_id);
        }
    }

    if(source_vms.empty()) {
        evacuating_machine.reset();
        return;
    }

    sort(source_vms.begin(), source_vms.end(), [](VMId_t lhs, VMId_t rhs) {
        return EstimatedVmMemory(lhs) > EstimatedVmMemory(rhs);
    });

    unordered_map<MachineId_t, unsigned> simulated_tasks;
    unordered_map<MachineId_t, unsigned> simulated_memory;
    auto simulated_sla_counts = machine_sla_vm_counts;
    for(MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        simulated_tasks[machine_id] = machine_info.active_tasks;
        simulated_memory[machine_id] = machine_info.memory_used;
    }

    vector<pair<VMId_t, MachineId_t>> migration_plan;

    for(VMId_t vm_id : source_vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        SLAType_t vm_sla = vm_slas[vm_id];
        unsigned vm_memory = EstimatedVmMemory(vm_id);
        unsigned vm_task_count = static_cast<unsigned>(vm_info.active_tasks.size());

        MachineId_t best_destination = numeric_limits<MachineId_t>::max();
        int best_bucket = numeric_limits<int>::max();
        double best_destination_load = numeric_limits<double>::max();

        for(MachineId_t machine_id : machines) {
            if(machine_id == source ||
               pending_state_changes.count(machine_id) != 0 ||
               IsMachineProtected(machine_id, now)) {
                continue;
            }

            MachineInfo_t machine_info = Machine_GetInfo(machine_id);
            if(machine_info.cpu != vm_info.cpu ||
               machine_info.s_state != S0 ||
               host_states[machine_id] == HostState::OVERLOADED) {
                continue;
            }

            if(simulated_memory[machine_id] + vm_memory > machine_info.memory_size) {
                continue;
            }

            double projected_task_pressure = static_cast<double>(simulated_tasks[machine_id] + vm_task_count) /
                                             std::max(1u, machine_info.num_cpus);
            double projected_memory_pressure = static_cast<double>(simulated_memory[machine_id] + vm_memory) /
                                               std::max(1u, machine_info.memory_size);
            double projected_load = std::max(projected_task_pressure, projected_memory_pressure);
            if(projected_load >= kOverloadThreshold) {
                continue;
            }

            TaskId_t representative_task = vm_info.active_tasks.front();
            SLAType_t representative_sla = RequiredSLA(representative_task);
            bool machine_is_preferred = true;
            auto best_it = best_capacity_by_cpu.find(vm_info.cpu);
            uint64_t best_score = (best_it == best_capacity_by_cpu.end()) ? 0 : best_it->second;
            if((representative_sla == SLA0 || representative_sla == SLA1) && best_score != 0) {
                unsigned base_mips = machine_info.performance.empty() ? 0 : machine_info.performance[0];
                uint64_t machine_score = static_cast<uint64_t>(machine_info.num_cpus) * base_mips;
                machine_is_preferred = machine_score * 100 >= best_score * 75;
            }

            unsigned preferred_machines = 0;
            unsigned preferred_sla0_machines = 0;
            for(MachineId_t candidate : machines) {
                MachineInfo_t candidate_info = Machine_GetInfo(candidate);
                if(candidate_info.cpu != vm_info.cpu) {
                    continue;
                }
                unsigned candidate_mips = candidate_info.performance.empty() ? 0 : candidate_info.performance[0];
                uint64_t candidate_score = static_cast<uint64_t>(candidate_info.num_cpus) * candidate_mips;
                bool candidate_is_preferred = best_score == 0 || candidate_score * 100 >= best_score * 75;
                if(!candidate_is_preferred) {
                    continue;
                }
                preferred_machines++;
                auto counts_it = simulated_sla_counts.find(candidate);
                if(counts_it != simulated_sla_counts.end() && counts_it->second[SLA0] > 0) {
                    preferred_sla0_machines++;
                }
            }

            auto counts_it = simulated_sla_counts.find(machine_id);
            bool machine_already_hosts_sla0 = counts_it != simulated_sla_counts.end() &&
                                              counts_it->second[SLA0] > 0;
            int bucket = PlacementBucket(machine_is_preferred,
                                         ShouldReservePreferredCapacityForSla1(representative_sla,
                                                                              machine_is_preferred,
                                                                              machine_already_hosts_sla0,
                                                                              preferred_machines,
                                                                              preferred_sla0_machines),
                                         ShouldAvoidMixedSlaMachine(simulated_sla_counts,
                                                                    representative_task,
                                                                    machine_id),
                                         false,
                                         false);

            if(bucket < best_bucket ||
               (bucket == best_bucket && projected_load < best_destination_load)) {
                best_bucket = bucket;
                best_destination_load = projected_load;
                best_destination = machine_id;
            }
        }

        if(best_destination == numeric_limits<MachineId_t>::max()) {
            evacuating_machine.reset();
            return;
        }

        migration_plan.push_back({vm_id, best_destination});
        simulated_tasks[source] = simulated_tasks[source] > vm_task_count ?
                                  simulated_tasks[source] - vm_task_count : 0;
        simulated_memory[source] = simulated_memory[source] > vm_memory ?
                                   simulated_memory[source] - vm_memory : 0;
        if(simulated_sla_counts[source][vm_sla] > 0) {
            simulated_sla_counts[source][vm_sla]--;
        }
        simulated_tasks[best_destination] += vm_task_count;
        simulated_memory[best_destination] += vm_memory;
        simulated_sla_counts[best_destination][vm_sla]++;
    }

    if(migration_plan.empty()) {
        evacuating_machine.reset();
        return;
    }

    VMId_t vm_id = migration_plan.front().first;
    MachineId_t destination = migration_plan.front().second;
    migrating_vms.insert(vm_id);
    migration_sources[vm_id] = source;
    migration_destinations[vm_id] = destination;
    VM_Migrate(vm_id, destination);
    SimOutput("Scheduler::MaybeEvacuateUnderload(): evacuating machine " + to_string(source) +
              " by migrating VM " + to_string(vm_id) +
              " to machine " + to_string(destination) +
              " remaining_vms=" + to_string(source_vms.size()), 1);
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
    total_tasks = GetNumTasks();
    completed_tasks = 0;
    next_progress_percent = 10;
    max_target_completion = 0;
    next_time_progress_percent = 10;
    next_energy_debug_time = kEnergyDebugInterval;
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 3);
    SimOutput("Scheduler::Init(): Total number of tasks is " + to_string(total_tasks), 2);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    for(unsigned i = 0; i < total_tasks; i++) {
        TaskInfo_t task_info = GetTaskInfo(TaskId_t(i));
        max_target_completion = max(max_target_completion, task_info.target_completion);
    }
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
    auto source_it = migration_sources.find(vm_id);
    auto destination_it = migration_destinations.find(vm_id);
    auto vm_sla_it = vm_slas.find(vm_id);

    if(source_it != migration_sources.end() &&
       destination_it != migration_destinations.end() &&
       vm_sla_it != vm_slas.end()) {
        SLAType_t vm_sla = vm_sla_it->second;
        if(machine_sla_vm_counts[source_it->second][vm_sla] > 0) {
            machine_sla_vm_counts[source_it->second][vm_sla]--;
        }
        machine_sla_vm_counts[destination_it->second][vm_sla]++;
        idle_since.erase(destination_it->second);
    }

    migrating_vms.erase(vm_id);
    migration_sources.erase(vm_id);
    migration_destinations.erase(vm_id);

    if(find(vms.begin(), vms.end(), vm_id) != vms.end()) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if(vm_info.active_tasks.empty()) {
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
        }
    }

    RefreshMachineStates();
    MaybeRelieveOverload(time);
    MaybeEvacuateUnderload(time);
    RetryWaitingTasks(time);
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
    MaybeRelieveOverload(now);
    MaybeEvacuateUnderload(now);
    for(MachineId_t machine_id : machines) {
        MaybeSleepMachine(machine_id, now);
    }
    RetryWaitingTasks(now);
    if(max_target_completion > 0) {
        unsigned time_progress_percent = static_cast<unsigned>((now * 100) / max_target_completion);
        while(next_time_progress_percent <= 100 &&
              time_progress_percent >= next_time_progress_percent) {
            SimOutput("Scheduler::TimeProgress(): " + to_string(next_time_progress_percent) +
                      "% of simulated timeline (" + to_string(now) + "/" +
                      to_string(max_target_completion) + " us)", 0);
            next_time_progress_percent += 10;
        }
    }
    MaybePrintEnergyDebugSummary(now);
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
    if(completed_tasks < total_tasks) {
        completed_tasks++;
    }
    if(total_tasks > 0) {
        unsigned progress_percent = (completed_tasks * 100) / total_tasks;
        while(next_progress_percent <= 100 && progress_percent >= next_progress_percent) {
            SimOutput("Scheduler::Progress(): " + to_string(next_progress_percent) +
                      "% complete (" + to_string(completed_tasks) + "/" +
                      to_string(total_tasks) + " tasks) at time " + to_string(now), 0);
            next_progress_percent += 10;
        }
    }

    auto vm_it = task_to_vm.find(task_id);
    if(vm_it != task_to_vm.end()) {
        VMId_t vm_id = vm_it->second;
        task_to_vm.erase(vm_it);

        if(find(vms.begin(), vms.end(), vm_id) == vms.end()) {
            RetryWaitingTasks(now);
            SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) +
                      " completed after VM " + to_string(vm_id) +
                      " was already retired", 2);
            return;
        }

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if(vm_info.active_tasks.empty()) {
            if(migrating_vms.count(vm_id) != 0) {
                RetryWaitingTasks(now);
                SimOutput("Scheduler::TaskComplete(): VM " + to_string(vm_id) +
                          " is empty but still migrating, deferring shutdown", 2);
                return;
            }
            // Multiple task completions can arrive back-to-back for the same VM.
            // Once the VM is empty, clear every remaining stale task->VM mapping first.
            for(auto it = task_to_vm.begin(); it != task_to_vm.end(); ) {
                if(it->second == vm_id) {
                    it = task_to_vm.erase(it);
                } else {
                    ++it;
                }
            }
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

void Scheduler::MaybePrintEnergyDebugSummary(Time_t now) {
    if(now < next_energy_debug_time) {
        return;
    }
    next_energy_debug_time = now + kEnergyDebugInterval;

    unsigned s0_count = 0;
    unsigned s0i1_count = 0;
    unsigned deeper_sleep_count = 0;
    unsigned active_machine_count = 0;
    unsigned empty_s0_preferred = 0;
    unsigned empty_s0_nonpreferred = 0;
    unsigned overloaded_count = 0;
    unsigned normal_count = 0;
    unsigned underloaded_count = 0;

    struct HotMachine {
        double load = -1.0;
        MachineId_t machine_id = 0;
        MachineInfo_t info{};
    };
    vector<HotMachine> hottest;

    for(MachineId_t machine_id : machines) {
        MachineInfo_t info = Machine_GetInfo(machine_id);
        double load = MachineLoadScore(machine_id);

        if(info.s_state == S0) {
            s0_count++;
        } else if(info.s_state == S0i1) {
            s0i1_count++;
        } else {
            deeper_sleep_count++;
        }

        if(info.active_tasks > 0) {
            active_machine_count++;
        }

        if(info.s_state == S0 && info.active_tasks == 0 && info.active_vms == 0) {
            if(IsPreferredMachineForCpu(best_capacity_by_cpu, info)) {
                empty_s0_preferred++;
            } else {
                empty_s0_nonpreferred++;
            }
        }

        auto host_it = host_states.find(machine_id);
        if(host_it != host_states.end()) {
            switch(host_it->second) {
                case HostState::OVERLOADED:
                    overloaded_count++;
                    break;
                case HostState::NORMAL:
                    normal_count++;
                    break;
                case HostState::UNDERLOADED:
                    underloaded_count++;
                    break;
            }
        }

        hottest.push_back({load, machine_id, info});
    }

    sort(hottest.begin(), hottest.end(), [](const HotMachine & lhs, const HotMachine & rhs) {
        return lhs.load > rhs.load;
    });

    SimOutput("Scheduler::EnergyDebug(): time=" + to_string(now) +
              " energy=" + to_string(Machine_GetClusterEnergy()) +
              " waiting=" + to_string(waiting_tasks.size()) +
              " migrating=" + to_string(migrating_vms.size()) +
              " pending_state_changes=" + to_string(pending_state_changes.size()) +
              " S0=" + to_string(s0_count) +
              " S0i1=" + to_string(s0i1_count) +
              " deeper_sleep=" + to_string(deeper_sleep_count) +
              " active_machines=" + to_string(active_machine_count) +
              " overloaded=" + to_string(overloaded_count) +
              " normal=" + to_string(normal_count) +
              " underloaded=" + to_string(underloaded_count), 1);

    SimOutput("Scheduler::EnergyDebugIdle(): empty_S0_preferred=" + to_string(empty_s0_preferred) +
              " empty_S0_nonpreferred=" + to_string(empty_s0_nonpreferred) +
              " idle_tracking=" + to_string(idle_since.size()), 1);

    string hot_line = "Scheduler::EnergyDebugHot():";
    unsigned hot_limit = min<unsigned>(3, hottest.size());
    for(unsigned i = 0; i < hot_limit; i++) {
        const HotMachine & hot = hottest[i];
        hot_line += " m=" + to_string(hot.machine_id) +
                    "(" + CpuTypeName(hot.info.cpu) + "," + MachineStateName(hot.info.s_state) + ")" +
                    " load=" + to_string(hot.load) +
                    " tasks=" + to_string(hot.info.active_tasks) +
                    " vms=" + to_string(hot.info.active_vms) +
                    " mem=" + to_string(hot.info.memory_used) + "/" +
                    to_string(hot.info.memory_size);
    }
    SimOutput(hot_line, 1);
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

void Scheduler::HandleMemoryWarning(Time_t time, MachineId_t machine_id) {
    ProtectMachine(machine_id, time);
    RefreshMachineStates();

    if(TryRelieveMachineOverload(machine_id, time)) {
        SimOutput("Scheduler::HandleMemoryWarning(): triggered overload relief for machine " +
                  to_string(machine_id) + " at time " + to_string(time), 1);
    } else {
        SimOutput("Scheduler::HandleMemoryWarning(): no safe migration found for machine " +
                  to_string(machine_id) + " at time " + to_string(time), 2);
    }

    RetryWaitingTasks(time);
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
    Scheduler.HandleMemoryWarning(time, machine_id);
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
