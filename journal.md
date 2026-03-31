Goal - Implement P-Mapper:
    Sort servers by energy consumption
    Assign tasks to the lowest energy server
    If a machine is fully utilized, move on to the next
    When a workload completes, try to steal tasks from servers w higher energy consumption


Design considerations:
    - Will need a data structure to store the machines in order of energy consumption
    - How should we decide when to create and attach VMs? Lazy could lead to high latency. pre attaching could lead to high energy consumption
    - Need to schedule tasks on compatible VMs and HW, may help to partition machines by their specs in order to quickly find compatible machines


Implementation plan:
    - Build a data structure that stores machines in order on energy consumption
    - Implement task arrival logic -> assign to lowest energy compatible machine
    - Implement task completion logic -> may want to start with simple "if empty, shutdown" before dealing w any migration
    - Optimize for efficiency


Steps:
    Trying to print machine info for each machine on initialization so I can start building data structure
    Not sure if I should do this in InitScheduler() or Scheduler.Init(). First choice seems more natural according to spec but don't have access
    to machines vector. Would be helpful if we had a Machine_GetAll() but we don't. May have to do this in Scheduler.Init() and return whatever data structure,
    or have it global and populate it

    Created a helper to get all machine ids, can now print some machine info, however it's trying to iterate through 16 machines even though the input file specifies only 2
    It seems that 16 is hardcoded in initialization right now, which is confusing since I'd assume the input file parsing and initialization would be done for us

    ^ Update: Parsing is done for us, but 16 is hardcoded and should be changed. I set it to Machine_GetTotal() instead

    Got first simulation complete run with simple input file, assign 4 VMs to each machine, just assign every task to VM 0
    Had to change SchedulerCheck and Scheduler.NewTask()


Data Structure Design:
    - Maintain lists of machines, a list for each type
    - Periodically sort this list by energy efficiency
    - Machines should maintain which VMs are attached


sort machines by energy usage
on task arrival -> get cheapest machine
get list of machine's VMs, if one has good space and compatible, assign
If no compatible VMs with available space:
    if good amt of space on machine, create new VM, assign
    else: move on to next machine


I've now added a vector for every CPU / GPU partition. For example, we have X86 + GPU, X86 + No GPU, ARM + GPU, ARM + No GPU, etc
On initialization, we add each machine to the correct partition and sort them based on energy efficiency. Right now, I'm using MIPS[P0] / p_states[p0] as a proxy for energy efficiency

