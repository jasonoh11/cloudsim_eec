//
//  Task.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/19/24.
//

#ifndef Task_hpp
#define Task_hpp

#include "Interfaces.h"
#include "Internal_Interfaces.h"

class Task {
public:
    Task(uint64_t inst, Time_t arr, Time_t trgt, VMType_t vm, SLAType_t sla, CPUType_t cpu, bool gpu, unsigned mem, TaskClass_t task_class, TaskId_t id);
    TaskInfo_t GetInfo();
    
    TaskId_t GetId()                                            { return taskId; }
 
    Priority_t GetPriority()                                    { return priority; }
    bool IsCompleted()                                          { return remaining == 0; }
    bool IsGPUCapable()                                         { return gpuCapable; }
    bool IsSLAViolated()                                        { return (slaType != SLA3) && completed && (completion > target); }
    CPUType_t GetCPUType()                                      { return cpuType; }
    unsigned GetMemory()                                        { return memory; }
    unsigned long long GetInstructions()                        { return instructions; }
    unsigned long long GetRemainingInstructions()               { return remaining; }
    SLAType_t GetSLAType()                                      { return slaType; }
    VMType_t GetVMType()                                        { return vmType; }
    void Reset()                                                { remaining = instructions; }
    void SetCompleted()                                         { completed = true; completion = Now(); }
    void SetPriority(Priority_t pr)                             { priority = pr; }
    void SetRemainingInstructions(unsigned long long r_instr)   { remaining = r_instr; SimOutput("Task::SetRemainingInstructions for task " + to_string(taskId) + " Remaining instruction " + to_string(r_instr), 4);}
    void CompletionReport();

private:
    unsigned long long instructions;
    unsigned long long remaining;
    Priority_t priority;
    
    Time_t arrival;
    Time_t completion;
    Time_t target;

    bool completed;

    CPUType_t cpuType;
    bool gpuCapable;
    unsigned memory;
    SLAType_t slaType;
    VMType_t vmType;

    TaskId_t taskId;
};
#endif /* Task_hpp */

