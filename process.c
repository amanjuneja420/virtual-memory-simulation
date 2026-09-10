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
 * @brief Blocks until SIGUSR1 is received, without the classic lost-wakeup race.
 *
 * A plain "if (signal_msg == 0) pause();" has a gap between the flag check
 * and the pause() call: if SIGUSR1 arrives in that gap, the wakeup is lost
 * and pause() sleeps forever. Blocking SIGUSR1 beforehand and waking via
 * sigsuspend() makes the check-then-sleep step atomic, so no wakeup can be
 * missed regardless of when the Scheduler's kill() lands.
 *
 * @param orig_set The signal mask to restore (with SIGUSR1 unblocked) while suspended.
 */
void waitForWakeup(sigset_t *orig_set) {
    while (signal_msg == 0) {
        sigsuspend(orig_set);
    }
    signal_msg = 0;
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

    // Clear any signal mask inherited across fork()/exec() from the Master
    // process first -- exec() preserves the caller's blocked-signal set, so
    // without this, a SIGUSR1 that Master happened to have blocked would
    // still be blocked here, and sigsuspend() below would never wake up.
    sigset_t empty_set;
    sigemptyset(&empty_set);
    sigprocmask(SIG_SETMASK, &empty_set, NULL);

    // Block SIGUSR1 so it can only ever be observed inside sigsuspend() below,
    // closing the race window between checking signal_msg and going to sleep.
    sigset_t block_set, orig_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block_set, &orig_set);

    // Enqueue process in the Ready Queue (MQ1)
    msgsnd(MQ1_id, &msg, sizeof(msg.message), 0);

    // Wait for the Scheduler to signal start of execution
    waitForWakeup(&orig_set);

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
            waitForWakeup(&orig_set);
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
