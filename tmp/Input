
# Strong, general-purpose X86 cluster
machine class:
{
 Number of machines: 6
 CPU type: X86
 Number of cores: 8
 Memory: 32768
 S-States: [140, 110, 100, 80, 45, 15, 0]
 P-States: [14, 10, 7, 5]
 C-States: [14, 4, 2, 0]
 MIPS: [1200, 900, 650, 450]
 GPUs: no
}

# Balanced, energy-efficient ARM cluster
machine class:
{
 Number of machines: 4
 CPU type: ARM
 Number of cores: 8
 Memory: 16384
 S-States: [90, 70, 60, 45, 25, 8, 0]
 P-States: [9, 7, 5, 3]
 C-States: [9, 3, 1, 0]
 MIPS: [850, 650, 500, 300]
 GPUs: no
}

# High-performance, energy-intensive cluster
machine class:
{
 Number of machines: 3
 CPU type: POWER
 Number of cores: 16
 Memory: 65536
 S-States: [180, 150, 135, 105, 60, 20, 0]
 P-States: [18, 14, 10, 7]
 C-States: [18, 5, 2, 0]
 MIPS: [1600, 1200, 900, 600]
 GPUs: yes
}

# Slower, smaller cluster
machine class:
{
 Number of machines: 3
 CPU type: RISCV
 Number of cores: 4
 Memory: 8192
 S-States: [60, 48, 40, 30, 18, 6, 0]
 P-States: [6, 5, 4, 2]
 C-States: [6, 2, 1, 0]
 MIPS: [700, 550, 400, 250]
 GPUs: no
}


# Web and streaming
task class:
{
 Start time: 0
 End time: 2200000
 Inter arrival: 180000
 Expected runtime: 180000
 Memory: 512
 VM type: LINUX
 GPU enabled: no
 SLA type: SLA3
 CPU type: ARM
 Task type: WEB
 Seed: 101
}

task class:
{
 Start time: 0
 End time: 2200000
 Inter arrival: 200000
 Expected runtime: 450000
 Memory: 1024
 VM type: LINUX
 GPU enabled: no
 SLA type: SLA2
 CPU type: RISCV
 Task type: STREAM
 Seed: 102
}


task class:
{
 Start time: 400000
 End time: 2500000
 Inter arrival: 260000
 Expected runtime: 650000
 Memory: 1024
 VM type: LINUX
 GPU enabled: no
 SLA type: SLA3
 CPU type: POWER
 Task type: STREAM
 Seed: 103
}



# Quick, bursty traffic
task class:
{
 Start time: 2200000
 End time: 5000000
 Inter arrival: 16000
 Expected runtime: 150000
 Memory: 768
 VM type: WIN
 GPU enabled: no
 SLA type: SLA0
 CPU type: X86
 Task type: WEB
 Seed: 201
}

task class:
{
 Start time: 2400000
 End time: 5000000
 Inter arrival: 22000
 Expected runtime: 250000
 Memory: 1024
 VM type: LINUX_RT
 GPU enabled: no
 SLA type: SLA1
 CPU type: ARM
 Task type: STREAM
 Seed: 202
}


# High-intensity mixed workloads
task class:
{
 Start time: 3000000
 End time: 6500000
 Inter arrival: 90000
 Expected runtime: 4500000
 Memory: 8192
 VM type: AIX
 GPU enabled: yes
 SLA type: SLA1
 CPU type: POWER
 Task type: HPC
 Seed: 301
}

task class:
{
 Start time: 3200000
 End time: 7600000
 Inter arrival: 32000
 Expected runtime: 1200000
 Memory: 3072
 VM type: LINUX
 GPU enabled: yes
 SLA type: SLA2
 CPU type: POWER
 Task type: AI
 Seed: 302
}

task class:
{
 Start time: 3500000
 End time: 7600000
 Inter arrival: 26000
 Expected runtime: 320000
 Memory: 1536
 VM type: WIN
 GPU enabled: no
 SLA type: SLA1
 CPU type: X86
 Task type: CRYPTO
 Seed: 303
}

task class:
{
 Start time: 4000000
 End time: 7200000
 Inter arrival: 70000
 Expected runtime: 900000
 Memory: 3584
 VM type: LINUX
 GPU enabled: no
 SLA type: SLA2
 CPU type: RISCV
 Task type: WEB
 Seed: 304
}


# Web cooldown
task class:
{
 Start time: 9500000
 End time: 13500000
 Inter arrival: 160000
 Expected runtime: 2200000
 Memory: 2048
 VM type: LINUX
 GPU enabled: no
 SLA type: SLA3
 CPU type: X86
 Task type: WEB
 Seed: 401
}

# Final burst of streaming
task class:
{
 Start time: 11000000
 End time: 12800000
 Inter arrival: 20000
 Expected runtime: 200000
 Memory: 1024
 VM type: LINUX_RT
 GPU enabled: no
 SLA type: SLA0
 CPU type: ARM
 Task type: STREAM
 Seed: 501
}