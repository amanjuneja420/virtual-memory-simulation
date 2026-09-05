/**
 * @file process.c
 * @brief Simulates a user process in the Virtual Memory Simulator.
 * 
 * This module generates memory access requests based on a reference string
 * provided by the Master process. It communicates with the Scheduler (for 
 * state transitions) and the MMU (for page translation) using IPC mechanisms.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <unistd.h>

/// @brief Flag to track if the SIGUSR1 signal has been received.
int signal_msg = 0;

/**
 * @brief Signal handler for SIGUSR1.
 * 
 * Sets the signal_msg flag to 1 when the Scheduler signals the process
 * to resume execution, and re-registers the handler.
 * 
 * @param sig The signal number.
 */
void signalHandler(int sig) {
    if (sig == SIGUSR1) {
        signal_msg = 1; 
    }
    signal(SIGUSR1, signalHandler);
}

/**
 * @brief Message queue structure for communication with the Scheduler.
 */
struct msgbuf {
    long m_type;
    struct message_ {
        int pid;
    } message;
};

/**
 * @brief Message queue structure for communication with the MMU.
 */
struct pageFrame {
    long m_type;
    struct message {
        int pid;
        int data;
    } message;
};

int main(int argc, char *argv[]) {
    // Validate command line arguments
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <MQ1_id> <MQ3_id> <reference_string>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int MQ1_id = atoi(argv[1]);
    int MQ3_id = atoi(argv[2]);
    char *ref_str = argv[3];
    int process_id = getpid();

    // Prepare message to register with the Scheduler
    struct msgbuf msg;
    msg.m_type = 1;
    msg.message.pid = process_id;

    // Register signal handler for synchronization
    signal(SIGUSR1, signalHandler);

    // Enqueue process in the Ready Queue (MQ1)
    msgsnd(MQ1_id, &msg, sizeof(msg.message), 0);

    // Wait for the Scheduler to signal start of execution
    if (signal_msg == 0) {
        pause();
    }
    signal_msg = 0;

    struct pageFrame pgf_msg;

    // Parse the page reference string
    char *token = strtok(ref_str, ",");
    while (token != NULL) {
        int page_no = atoi(token);

        // Send page translation request to the MMU via MQ3
        pgf_msg.m_type = 1;
        pgf_msg.message.pid = process_id;
        pgf_msg.message.data = page_no;
        msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);

        // Await response from the MMU, filtering by this process's PID
        msgrcv(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), process_id, 0);

        if (pgf_msg.message.data >= 0) {
            // Page Hit: MMU successfully returned a frame number.
            token = strtok(NULL, ",");
        } else if (pgf_msg.message.data == -1) {
            // Page Fault: MMU is loading the page. Wait for Scheduler's signal.
            if (signal_msg == 0) {
                pause(); 
            }
            signal_msg = 0;
            // Page is now resident in memory, proceed to the next request.
            token = strtok(NULL, ","); 
        } else if (pgf_msg.message.data == -2) {
            // Invalid Page Reference: MMU sent a termination signal.
            exit(EXIT_SUCCESS); 
        } else {
            // Unrecognized response.
            perror("Invalid data received from MMU");
            exit(EXIT_FAILURE);
        }
    }

    // Execution complete. Send termination marker (-9) to the MMU.
    pgf_msg.m_type = 1;
    pgf_msg.message.pid = process_id;
    pgf_msg.message.data = -9;
    msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);

    return EXIT_SUCCESS;
}
