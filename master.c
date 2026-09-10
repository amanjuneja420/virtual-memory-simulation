/**
 * @file Master.c
 * @brief Main orchestration module for the Virtual Memory Simulator.
 * 
 * Responsible for initializing IPC resources (Shared Memory and Message Queues),
 * performing initial proportional frame allocations, spawning child modules
 * (Scheduler, MMU, and user Processes), and performing rigorous garbage 
 * collection upon simulation completion.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const int STR_SIZE = 5000;
const float PROB_ILLEGAL = 0.05; ///< Probability factor for invalid page generation.
int waitSched = 1;               ///< Semaphore-like flag for synchronization.

/**
 * @brief Represents an entry in the Page Table.
 */
typedef struct pageTable_ {
    int valid;    ///< 1 if resident in memory, 0 otherwise.
    int frame_no; ///< Physical frame number allocated.
} pageTable;

/**
 * @brief Maps a process to its maximum allowed virtual address space.
 */
typedef struct pageMap_ {
    int pid;      ///< Process ID.
    int num_page; ///< Total virtual pages required by the process.
} pageMap;

/**
 * @brief Signal handler for SIGUSR1.
 * 
 * Clears the synchronization flag to allow the Master process to proceed 
 * with cleanup routines once the Scheduler signals termination.
 * 
 * @param sig The signal number.
 */
void signalHandler(int sig) {
    if (sig == SIGUSR1) {
        waitSched = 0;
    }
}

int main() {
    int k, m, f;
    printf("Enter the total number of processes: ");
    if(scanf("%d", &k) != 1) return 1;
    printf("Enter Virtual address space (Max pages per process): ");
    if(scanf("%d", &m) != 1) return 1;
    printf("Enter physical address space (Total frames): ");
    if(scanf("%d", &f) != 1) return 1;

    // Register synchronization handler for process completion
    signal(SIGUSR1, signalHandler);

    // NOTE: SIGUSR1 is deliberately NOT blocked here yet. fork() copies the
    // caller's signal mask and execve() preserves it, so blocking SIGUSR1
    // before spawning sched/mmu/process would leak "blocked" into every
    // child, permanently breaking their own sigsuspend()-based wakeups. The
    // block happens further down, after all children are spawned, and only
    // affects Master's own wait.

    // =========================================================================
    // 1. IPC INITIALIZATION (Shared Memory & Message Queues)
    // =========================================================================
    
    // Shared Memory 1: Global Page Table Matrix
    key_t SM1_key = ftok("/tmp", 100);
    int SM1_id = shmget(SM1_key, k * m * sizeof(pageTable), IPC_CREAT | 0666);

    // Shared Memory 2: Physical Free Frame List
    key_t SM2_key = ftok("/tmp", 101);
    int SM2_id = shmget(SM2_key, f * sizeof(int), IPC_CREAT | 0666);

    // Shared Memory 3: Process Configuration Mapping
    key_t SM3_key = ftok("/tmp", 102);
    int SM3_id = shmget(SM3_key, k * sizeof(pageMap), IPC_CREAT | 0666);

    // Message Queue 1: Scheduler Ready Queue
    key_t MQ1_key = ftok("/tmp", 103);
    int MQ1_id = msgget(MQ1_key, IPC_CREAT | 0666);

    // Message Queue 2: MMU to Scheduler Control Channel
    key_t MQ2_key = ftok("/tmp", 104);
    int MQ2_id = msgget(MQ2_key, IPC_CREAT | 0666);

    // Message Queue 3: Process to MMU Translation Channel
    key_t MQ3_key = ftok("/tmp", 105);
    int MQ3_id = msgget(MQ3_key, IPC_CREAT | 0666);

    // Attach segments for Master-level initialization
    pageTable *page_table = (pageTable *)shmat(SM1_id, NULL, 0);
    int *free_frame_list = (int *)shmat(SM2_id, NULL, 0);
    pageMap *page_map = (pageMap *)shmat(SM3_id, NULL, 0);

    // Initialize all pages as non-resident (invalid)
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < m; j++) {
            page_table[i * m + j].valid = 0;
            page_table[i * m + j].frame_no = -1;
        }
    }

    // Initialize the physical memory pool as completely free
    for (int i = 0; i < f; i++) {
        free_frame_list[i] = 1; 
    }

    // Purge default mapping parameters
    for (int i = 0; i < k; i++) {
        page_map[i].pid = -1;
        page_map[i].num_page = -1;
    }

    // =========================================================================
    // 2. MODULE SPAWNING (Scheduler & MMU)
    // =========================================================================
    
    pid_t sched_pid, mmu_pid;

    if ((sched_pid = fork()) == 0) { 
        char MQ1_str[STR_SIZE], MQ2_str[STR_SIZE], num_process_str[STR_SIZE];
        sprintf(MQ1_str, "%d", MQ1_id);
        sprintf(MQ2_str, "%d", MQ2_id);
        sprintf(num_process_str, "%d", k);
        execlp("./sched", "./sched", MQ1_str, MQ2_str, num_process_str, NULL);
        // execlp only returns on failure. Without _exit() here, this child
        // would otherwise fall through and keep running the rest of main()
        // as a rogue second Master -- forking its own children and racing
        // the real Master's IPC cleanup.
        perror("execlp: ./sched");
        _exit(EXIT_FAILURE);
    }
    
    if ((mmu_pid = fork()) == 0) { 
        char SM1_str[STR_SIZE], SM2_str[STR_SIZE], SM3_str[STR_SIZE], MQ2_str[STR_SIZE], MQ3_str[STR_SIZE], k_str[STR_SIZE], m_str[STR_SIZE], f_str[STR_SIZE];
        sprintf(SM1_str, "%d", SM1_id);
        sprintf(SM2_str, "%d", SM2_id);
        sprintf(SM3_str, "%d", SM3_id);
        sprintf(MQ2_str, "%d", MQ2_id);
        sprintf(MQ3_str, "%d", MQ3_id);
        sprintf(k_str, "%d", k);
        sprintf(m_str, "%d", m);
        sprintf(f_str, "%d", f);

        // Submits MMU binary to the current environment configuration
        execlp("./mmu", "./mmu", MQ2_str, MQ3_str, SM1_str, SM2_str, SM3_str, k_str, m_str, f_str, NULL);
        perror("execlp: ./mmu");
        _exit(EXIT_FAILURE);
    }

    // =========================================================================
    // 3. PROPORTIONAL FRAME PRE-ALLOCATION
    // =========================================================================
    
    int total_pages = 0;
    srand((unsigned)time(NULL));
    
    // Assign randomized address space thresholds to each simulated process
    for (int i = 0; i < k; i++) {
        int m_i = rand() % m + 1; 
        page_map[i].num_page = m_i;
        total_pages += m_i;
    }

    int frame_cnt = 0;
    
    // Distribute physical frames proportionally to required virtual pages
    for (int i = 0; i < k; i++) {
        if (frame_cnt >= f) break;
        
        int num_frames = (page_map[i].num_page * f) / total_pages;

        // Force mapping initialization for the computed subset
        for (int j = 0; j < num_frames; j++) {
            page_table[i * m + j].frame_no = frame_cnt;
            page_table[i * m + j].valid = 1;
            free_frame_list[frame_cnt] = 0; 
            frame_cnt++;
        }

        // Failsafe: Guarantee at least a single frame per process
        if (num_frames == 0) {
            page_table[i * m].frame_no = frame_cnt;
            page_table[i * m].valid = 1;
            free_frame_list[frame_cnt] = 0;
            frame_cnt++;
        }
    }

    // =========================================================================
    // 4. REFERENCE STRING GENERATION & PROCESS SPAWNING
    // =========================================================================
    
    for (int i = 0; i < k; i++) {
        int m_i = page_map[i].num_page;
        int x = 2 * m_i + rand() % (8 * m_i + 1); // Compute sequence magnitude
        
        // Assemble comma-delimited reference query
        char ref_str[STR_SIZE];
        memset(ref_str, '\0', STR_SIZE);
        for (int j = 0; j < x; j++) {
            int page_no;
            
            // Induce periodic illegal access faults based on PROB_ILLEGAL
            if ((float)rand() / RAND_MAX < PROB_ILLEGAL) {
                page_no = m_i; 
            } else {
                page_no = rand() % m_i; 
            }
            
            char page_no_str[50];
            memset(page_no_str, '\0', 50);
            if (j == x - 1)
                sprintf(page_no_str, "%d", page_no);
            else
                sprintf(page_no_str, "%d,", page_no);
                
            strcat(ref_str, page_no_str);
        }

        pid_t process_pid;
        if ((process_pid = fork()) == 0) { 
            page_map[i].pid = getpid(); // Record PID mapping to Shared Memory

            char MQ1_str[STR_SIZE], MQ3_str[STR_SIZE];
            sprintf(MQ1_str, "%d", MQ1_id);
            sprintf(MQ3_str, "%d", MQ3_id);
            execlp("./process", "./process", MQ1_str, MQ3_str, ref_str, NULL);
            perror("execlp: ./process");
            _exit(EXIT_FAILURE);
        }
        usleep(250000); // 250ms cadence delay to stagger process ingestion
    }

    // =========================================================================
    // 5. SYSTEM SHUTDOWN & IPC DEALLOCATION
    // =========================================================================

    // Block SIGUSR1 only now, after every child has already been forked, so
    // none of them inherit it as blocked (see the note near the top of main).
    sigset_t block_set, orig_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block_set, &orig_set);

    // Suspend parent execution pending Scheduler completion interrupt
    while (waitSched == 1) {
        sigsuspend(&orig_set);
    }
    waitSched = 0;
    printf("Scheduler finished. Cleaning up...\n");

    // Send interrupt routines to child daemons
    kill(sched_pid, SIGINT); 
    usleep(500000); // Allow brief tolerance for buffer flushes
    kill(mmu_pid, SIGINT);   

    // Detach virtual pointers
    shmdt(page_map);
    shmdt(page_table);
    shmdt(free_frame_list);
    
    // Command kernel to destroy Shared Memory identifiers
    shmctl(SM1_id, IPC_RMID, NULL);
    shmctl(SM2_id, IPC_RMID, NULL);
    shmctl(SM3_id, IPC_RMID, NULL);

    // Command kernel to destroy Message Queue identifiers
    msgctl(MQ1_id, IPC_RMID, NULL);
    msgctl(MQ2_id, IPC_RMID, NULL);
    msgctl(MQ3_id, IPC_RMID, NULL);

    return EXIT_SUCCESS;
}
