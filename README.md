# User-Space Thermal-Aware Process Scheduling Framework

## Overview

This repository contains all source code developed for the capstone project *"Research and Development of a User-Space Thermal-Aware Process Scheduling Framework"* — a Linux framework for thermally informed process scheduling that operates entirely in user space, without any kernel modification.

The project has two parts:

1. **Energy characterization** — three workloads measuring how computation type affects core power draw, using Intel RAPL hardware counters.
2. **Thermal scheduling framework** — six progressively capable schedulers that monitor per-core temperatures and intervene before hardware throttling occurs.

All schedulers use standard Linux system calls (`sched_setaffinity()`, `SIGSTOP`, `SIGCONT`) and read temperature and process information from `/proc` and `lm-sensors`. No kernel patches or custom modules are required.

---

## Repository Structure

```
hasmik-manukyan-capstone/
│
├── Workloads
│   ├── int_power.c          # Integer arithmetic workload (ALU-bound)
│   ├── float_power.c        # Floating-point arithmetic workload (FPU-bound)
│   └── io_power.c           # I/O-bound file operations workload
│
└── Thermal Schedulers
    ├── thermal.c            # Version 1 — Core Migration Scheduler
    ├── thermal_pause.c      # Version 2 — Pause and Resume Scheduler
    ├── thermal_balance.c    # Version 3 — Hot/Cold Task Switcher
    ├── thermal_smart.c      # Version 4 — Auto-Detecting Scheduler
    ├── thermal_multi.c      # Version 5 — Multi-Core Multi-Process Scheduler
    └── thermal_isolated.c   # Version 6 — Isolated Core Scheduler
```

---

## Workloads

### Integer workload (`int_power.c`)
Performs continuous 64-bit integer multiplication in an infinite loop. Exercises the ALU at maximum rate. A configurable sleep interval (in microseconds) is passed as a command-line argument.

### Floating-point workload (`float_power.c`)
Performs the same loop structure using double-precision floating-point values, exercising the FPU instead of the ALU. Results are written to a `volatile` sink to prevent compiler elimination.

### I/O-bound workload (`io_power.c`)
Repeatedly opens, writes, closes, reopens, and reads a temporary file in an infinite loop. The CPU spends most of its time waiting on disk operations, keeping core utilisation and temperature significantly lower than the CPU-bound workloads. Serves as the **cold task** in hot/cold pairing.

---

## Thermal Schedulers

Each version was built by identifying a specific limitation in the previous one.

| Version | File | Key mechanism | Trigger |
|---|---|---|---|
| 1 | `thermal.c` | Process migration to cooler core | 85 °C |
| 2 | `thermal_pause.c` | Pause the hot process in place | 90 °C / resume at 60 °C |
| 3 | `thermal_balance.c` | Hot/cold task pairing, manual classification | 90 °C / swap at 60 °C |
| 4 | `thermal_smart.c` | Hot/cold pairing with automatic classification | 75 °C / swap at 60 °C |
| 5 | `thermal_multi.c` | Multi-core, multi-process hot/cold pairing | 90 °C / swap at 60 °C |
| 6 | `thermal_isolated.c` | Isolated physical core via `isolcpus` | 90 °C / resume at 75 °C |

---

## Requirements

- Linux (tested on Ubuntu 22.04)
- Intel CPU with RAPL support
- `lm-sensors` with `coretemp` driver — `sudo apt install lm-sensors`
- `perf` tool — `sudo apt install linux-tools-common linux-tools-generic`
- `stress-ng` — `sudo apt install stress-ng`
- `gcc` compiler
- Root or sudo access

---

## Building

All files are single-file C programs. Compile with `-O0` to disable optimisation and prevent loop elimination:

```bash
gcc -O0 -o int_power int_power.c
gcc -O0 -o float_power float_power.c
gcc -O0 -o io_power io_power.c

gcc -O0 -o thermal thermal.c
gcc -O0 -o thermal_pause thermal_pause.c
gcc -O0 -o thermal_balance thermal_balance.c
gcc -O0 -o thermal_smart thermal_smart.c
gcc -O0 -o thermal_multi thermal_multi.c
gcc -O0 -o thermal_isolated thermal_isolated.c
```

---

## Monitoring Temperature

Monitor per-core temperatures in a separate terminal window while running any scheduler:

```bash
watch -n 1 "sensors | grep -A 6 coretemp"
```

Or read directly from the thermal zone:

```bash
cat /sys/class/thermal/thermal_zone8/temp
```

---

## Setting the CPU Governor

All energy measurements should be done with the `performance` governor to fix the CPU at maximum frequency:

```bash
# Set performance governor
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Set powersave governor (to compare)
echo powersave | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

Check available cores and their layout:

```bash
lscpu -e
```

---

## Running Energy Measurements

### Using the custom workloads

```bash
sudo perf stat -e power/energy-cores/ taskset -c 0 ./int_power
sudo perf stat -e power/energy-cores/ taskset -c 0 ./float_power
sudo perf stat -e power/energy-cores/ taskset -c 0 ./io_power
```

### Using stress-ng for validation

```bash
# Integer workload — pins 1 worker to core 0, runs 64-bit integer arithmetic for 60 seconds
sudo perf stat -e power/energy-cores/ taskset -c 0 stress-ng --cpu 1 --cpu-method int64 -t 60s

# Floating-point workload
sudo perf stat -e power/energy-cores/ taskset -c 0 stress-ng --cpu 1 --cpu-method float -t 20s
```

The flags explained:
- `taskset -c 0` — pin to core 0
- `--cpu 1` — one worker thread
- `--cpu-method int64` — 64-bit integer arithmetic in a tight loop
- `-t 60s` — run for 60 seconds

### Idle baseline

```bash
sudo perf stat -e power/energy-cores/ sleep 20
```

---

## Running the Schedulers

### Version 1 — Core Migration (`thermal.c`)

```bash
gcc -o thermal thermal.c

# With stress-ng
stress-ng --cpu 4 --taskset 0-3 -t 60s &
sudo ./thermal 1 $(pgrep -o stress-ng)

# With custom workload
./int_power 0 &
sudo ./thermal 1 $(pgrep -o int_power)
```

---

### Version 2 — Pause and Resume (`thermal_pause.c`)

```bash
gcc -o thermal_pause thermal_pause.c
taskset -c 0 ./int_power 0 &
sudo ./thermal_pause 1 $(pgrep -o int_power)
```

---

### Version 3 — Hot/Cold Task Switcher (`thermal_balance.c`)

```bash
gcc -o thermal_balance thermal_balance.c
./int_power 0 &
./io_power &
sudo ./thermal_balance 0 $(pgrep int_power) $(pgrep io_power)
```

---

### Version 4 — Auto-Detecting Scheduler (`thermal_smart.c`)

```bash
gcc -o thermal_smart thermal_smart.c
./int_power 0 &
./io_power &
sudo ./thermal_smart $(pgrep int_power) $(pgrep io_power)
```

---

### Version 5 — Multi-Core Multi-Process Scheduler (`thermal_multi.c`)

Manages multiple hot/cold pairs across all cores simultaneously. Auto-detects which processes are hot and which are cold by measuring CPU usage over a two-second window. Creates one hot/cold pair per core and manages each independently.

```bash
gcc -o thermal_multi thermal_multi.c

# Launch 4 hot and 4 cold tasks
./int_power 0 &
./int_power 0 &
./int_power 0 &
./int_power 0 &
./io_power &
./io_power &
./io_power &
./io_power &

sudo ./thermal_multi $(pgrep int_power) $(pgrep io_power)

# Stop all tasks when done
pkill int_power
pkill io_power
```

---

### Version 6 — Isolated Core Scheduler (`thermal_isolated.c`)

This version requires isolating physical cores via the Linux boot parameter `isolcpus=3,7` before running. This removes logical CPUs 3 and 7 from the kernel's general scheduler, giving the framework exclusive ownership of one physical core.

#### Step 1 — Configure GRUB

```bash
# Back up GRUB config
sudo cp /etc/default/grub /etc/default/grub.backup

# Edit GRUB
sudo nano /etc/default/grub
```

Find the line `GRUB_CMDLINE_LINUX_DEFAULT` and update it to:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash isolcpus=3,7"
```

Save: `Ctrl+O` → `Enter` → `Ctrl+X`

```bash
# Apply and reboot
sudo update-grub
sudo reboot
```

#### Step 2 — Run the scheduler

```bash
gcc -o thermal_isolated thermal_isolated.c

./int_power 0 &
./int_power 0 &
./io_power &
./io_power &

sudo ./thermal_isolated $(pgrep int_power) $(pgrep io_power)
```

---

## Key Results

- CPU-bound workloads draw **32–36% more core power** than I/O-bound workloads (14.4 W vs 10.6 W average), confirming workload type as a meaningful scheduling signal.
- Without scheduling, sustained CPU-bound load drove cores to **100 °C TjMax** on the test hardware.
- With schedulers active, cores consistently stayed at **95–97 °C**, with the framework intervening before hardware throttling.
- All six schedulers triggered reliably under sustained load. The auto-detecting scheduler (75 °C threshold) intervened earliest and most frequently.
