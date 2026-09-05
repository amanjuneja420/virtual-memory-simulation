# Virtual Memory Simulator (Pure Demand Paging)

![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)

A robust, multi-process Operating System simulator built entirely in C. This project simulates an OS **Virtual Memory Management System** utilizing **Pure Demand Paging** and a **Local Least Recently Used (LRU)** page replacement algorithm.

It heavily utilizes advanced systems programming concepts including Inter-Process Communication (IPC) via **Shared Memory** and **Message Queues**, Process Synchronization using **POSIX Signals**, and multi-process orchestration via `fork()` and `execlp()`.

---

## Features

- **Pure Demand Paging**: Pages are loaded into physical frames only when explicitly requested (causing a Page Fault), optimizing initial memory footprint.
- **Local LRU Page Replacement**: When physical memory is full, the MMU intelligently evicts the least recently used page belonging to the specific faulting process.
- **Robust IPC Networking**: Implements isolated communication channels between the Scheduler, MMU, and user Processes using asynchronous Message Queues.
- **Hardware Emulation**: Mimics real hardware constraints, trapping **Segmentation Faults** (illegal memory access) and gracefully terminating offending processes without crashing the master system.
- **Automated Garbage Collection**: Implements strict cleanup routines to destroy all kernel-level IPC structures (`shmctl`, `msgctl`) upon simulation completion, preventing OS memory leaks.

---

## Architecture

The simulator is composed of four distinct, concurrently running modules:

1. **Master (`Master.c`)**: The orchestrator. It initializes all Shared Memory blocks and Message Queues, pre-allocates a proportional number of physical frames to prevent immediate starvation, and spawns all child modules.
2. **Memory Management Unit (`MMU.c`)**: The bridge between virtual and physical memory. It intercepts page requests, updates the global Page Table, handles Page Faults using LRU replacement, and tracks global access timestamps.
3. **Scheduler (`sched.c`)**: A First-Come-First-Serve (FCFS) CPU scheduler. It manages a Ready Queue of processes waiting for execution and coordinates wake-up calls using `SIGUSR1` interrupts.
4. **Process (`process.c`)**: Simulates user applications. It traverses a randomly generated reference string of virtual pages, requesting access from the MMU and entering a suspended `pause()` state while waiting for Page Faults to resolve.

---

## IPC Mechanisms Used

| Mechanism                               | Purpose in Simulator                                                                                                                                                                                    |
| :-------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Shared Memory** (`shmget`, `shmat`)   | Stores the global `Page Table`, `Free Frame List`, and process limit configurations. Allows the MMU and Master to instantly read/write massive data structures without message overhead.                |
| **Message Queues** (`msgget`, `msgsnd`) | Used for control signals. **MQ1** acts as the Scheduler's Ready Queue. **MQ2** allows the MMU to report status updates to the Scheduler. **MQ3** allows Processes to request translations from the MMU. |
| **Signals** (`signal`, `kill`, `pause`) | Used for process synchronization. Instead of busy-waiting, processes consume 0 CPU cycles via `pause()` until awakened by a `SIGUSR1` interrupt from the Scheduler.                                     |

---

## Getting Started

### Prerequisites

- A Linux environment (or WSL on Windows).
- `gcc` compiler and `make`.
- `xterm` (Optional, used by Master to spawn the MMU in a separate graphical terminal window for easier debugging).

### Compilation

The project includes a `Makefile` for automated compilation.

```bash
cd virtual-memory-simulation
make
```

### Execution

Run the compiled `master` executable:

```bash
./master
```

You will be prompted to define the scale of the simulation:

```text
Enter the total number of processes: 5
Enter Virtual address space (Max pages per process): 10
Enter physical address space (Total frames): 20
```

_(Note: The Master program will randomly generate page access strings for each process, including an intentional 5% probability of generating illegal bounds to test Segmentation Fault handling)._
