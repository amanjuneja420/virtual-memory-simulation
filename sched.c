/**
 * @file sched.c
 * @brief Implements the Scheduler module for the Virtual Memory Simulator.
 * 
 * Acts as a simple First-Come-First-Serve (FCFS) scheduler. It coordinates 
 * the execution of processes by waiting for them in a Ready Queue, signaling
 * them to execute, and waiting for the MMU to confirm status updates.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Message queue structure for the Ready Queue (Process -> Scheduler).
 */
struct msgbuf {
    long m_type;
    struct message_ {
        int pid;
    } message;
};

/**
 * @brief Message queue structure for MMU feedback (MMU -> Scheduler).
 */
struct schedMsg {
    long m_type;
    int message; // 1 = Page Fault Handled, 2 = Process Terminated
};

int main(int argc, char *argv[]) {
    // Validate command line arguments
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <MQ1_id> <MQ2_id> <total_processes>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int MQ1_id = atoi(argv[1]);
    int MQ2_id = atoi(argv[2]);
    int k = atoi(argv[3]);

    struct msgbuf msg;
    struct schedMsg schMsg;

    int completed_processes = 0;

    // Main scheduling loop: run until all processes have terminated.
    while (completed_processes < k) {

        // Block until a process is available in the Ready Queue (MQ1).
        msgrcv(MQ1_id, &msg, sizeof(msg.message), 1, 0);

        // Dispatch the process by sending SIGUSR1.
        kill(msg.message.pid, SIGUSR1);

        // Block until the MMU responds with the process's status (MQ2).
        msgrcv(MQ2_id, &schMsg, sizeof(schMsg.message), 1, 0);

        if (schMsg.message == 1) {
            // Type 1: Page fault was handled. Re-enqueue the process.
            msg.m_type = 1;
            msg.message.pid = msg.message.pid; 
            msgsnd(MQ1_id, &msg, sizeof(msg.message), 0);
        } else {
            // Type 2: Process successfully terminated or encountered an invalid reference.
            ++completed_processes;
        }
    }

    // All processes completed. Notify the Master process via SIGUSR1.
    kill(getppid(), SIGUSR1);

    // Enter dormant state until Master terminates the module.
    pause();
    
    return EXIT_SUCCESS;
}
