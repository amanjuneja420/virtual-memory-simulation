/**
 * @file MMU.c
 * @brief Implements the Memory Management Unit for the Virtual Memory Simulator.
 * 
 * Responsible for translating virtual page numbers to physical frame numbers,
 * handling page faults using a Local Least Recently Used (LRU) algorithm, and 
 * managing shared memory structures including the Page Table and Free Frame List.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

/// @brief Global logical clock for LRU page replacement algorithm.
int timestamp = 0; 

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
 * @brief Message queue structure for Process-to-MMU communication.
 */
struct pageFrame {
    long m_type;
    struct message {
        int pid;
        int data;
    } message;
};

/**
 * @brief Message queue structure for MMU-to-Scheduler communication.
 */
struct schedMsg {
    long m_type;
    int message;
};

/**
 * @brief Handles page faults using a Local LRU replacement policy.
 * 
 * @param p_i The internal process index (0 to k-1).
 * @param m Maximum pages per process.
 * @param f Total physical frames in the system.
 * @param page_num The virtual page number requested.
 * @param page_table Pointer to the shared Page Table.
 * @param free_frame_list Pointer to the shared Free Frame List.
 * @param pageTimeStamp 2D array tracking the last access time of each page.
 */
void PageFaultHandler(int p_i, int m, int f, int page_num, pageTable *page_table, int *free_frame_list, int pageTimeStamp[][m]) {

    // 1. Attempt to allocate an unmapped free frame.
    for (int i = 0; i < f; i++) {
        if (free_frame_list[i] == 1) { 
            page_table[p_i * m + page_num].frame_no = i;
            page_table[p_i * m + page_num].valid = 1;
            free_frame_list[i] = 0; 
            return;
        }
    }

    // 2. Perform Local LRU Page Replacement.
    int diff;
    int lruPage = 0;
    int maxDiff = -1;
    
    // Identify the Least Recently Used (oldest) valid page belonging to this process.
    for (int j = 0; j < m; j++) {
        if (page_table[p_i * m + j].valid == 1) { 
            diff = timestamp - pageTimeStamp[p_i][j];
            if (diff > maxDiff) {
                maxDiff = diff;
                lruPage = j;
            }
        }
    }

    // Swap the victim page (lruPage) with the requested page.
    if (maxDiff != -1) {
        page_table[p_i * m + page_num].frame_no = page_table[p_i * m + lruPage].frame_no;
        page_table[p_i * m + lruPage].valid = 0;
        page_table[p_i * m + page_num].valid = 1;
        pageTimeStamp[p_i][page_num] = timestamp;
    }
}

int main(int argc, char *argv[]) {
    // Validate command line arguments
    if (argc < 9) {
        fprintf(stderr, "Usage: %s <MQ2> <MQ3> <SM1> <SM2> <SM3> <k> <m> <f>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int MQ2_id = atoi(argv[1]); 
    int MQ3_id = atoi(argv[2]); 
    int SM1_id = atoi(argv[3]); 
    int SM2_id = atoi(argv[4]); 
    int SM3_id = atoi(argv[5]); 
    int k = atoi(argv[6]);      
    int m = atoi(argv[7]);      
    int f = atoi(argv[8]);      

    // Initialize result output file.
    int fd = open("result.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);

    // Attach IPC Shared Memory segments.
    pageTable *page_table = (pageTable *)shmat(SM1_id, NULL, 0);
    int *free_frame_list = (int *)shmat(SM2_id, NULL, 0);
    pageMap *page_map = (pageMap *)shmat(SM3_id, NULL, 0);

    // Initialize LRU timestamps matrix.
    int pageTimeStamp[k][m];
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < m; j++) {
            if (page_table[i * m + j].valid == 1) {
                // Pre-allocated frames receive initial timestamp.
                pageTimeStamp[i][j] = timestamp;
            } else {
                pageTimeStamp[i][j] = -1;
            }
        }
    }

    // Initialize performance monitoring arrays.
    int page_faults[k], invalid_page_ref[k];
    for (int i = 0; i < k; i++) {
        page_faults[i] = 0;
        invalid_page_ref[i] = 0;
    }

    struct schedMsg schMsg;
    struct pageFrame pgf_msg;
    int completed_processes = 0;

    // Main MMU processing loop.
    while (completed_processes < k) {

        // Block until a page translation request is received (MQ3).
        msgrcv(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 1, 0);

        int page_num = pgf_msg.message.data;
        int p_pid = pgf_msg.message.pid;

        // Resolve internal process index (1-based for output consistency).
        int process_num = 0;
        for (int i = 0; i < k; i++) {
            if (page_map[i].pid == p_pid) {
                process_num = i + 1; 
                break;
            }
        }

        char str[100];
        memset(str, '\0', sizeof(str));
        printf("Global ordering - (%d,%d,%d)\n", timestamp, process_num, page_num);
        sprintf(str, "Global ordering - (%d,%d,%d)\n", timestamp, process_num, page_num);
        write(fd, str, strlen(str));

        // Handle process termination marker.
        if (page_num == -9) {
            for (int i = 0; i < k; i++) {
                if (page_map[i].pid == p_pid) {
                    // Deallocate all physical frames held by the terminating process.
                    for (int j = 0; j < m; j++) {
                        if (page_table[i * m + j].valid == 1) {
                            free_frame_list[page_table[i * m + j].frame_no] = 1; 
                            page_table[i * m + j].valid = 0;                     
                        }
                    }
                    break;
                }
            }

            // Notify Scheduler of termination.
            schMsg.m_type = 1;
            schMsg.message = 2;
            msgsnd(MQ2_id, &schMsg, sizeof(schMsg.message), 0);
            completed_processes++;
        } 
        // Handle standard memory access requests.
        else {
            for (int i = 0; i < k; i++) {
                if (page_map[i].pid == p_pid) {
                    
                    // Validate address space bounds (Segmentation Fault check).
                    if (page_map[i].num_page <= page_num) { 

                        printf("Invalid page reference - (%d,%d)\n", process_num, page_num);
                        memset(str, '\0', sizeof(str));
                        sprintf(str, "Invalid page reference - (%d,%d)\n", process_num, page_num);
                        write(fd, str, strlen(str));

                        // Instruct process to terminate via -2 code.
                        pgf_msg.m_type = p_pid;
                        pgf_msg.message.data = -2;
                        msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);
                        invalid_page_ref[i]++;

                        // Reclaim frames from the faulting process.
                        for (int j = 0; j < m; j++) {
                            if (page_table[i * m + j].valid == 1) {
                                free_frame_list[page_table[i * m + j].frame_no] = 1;
                                page_table[i * m + j].valid = 0;
                            }
                        }

                        // Notify Scheduler of termination.
                        schMsg.m_type = 1;
                        schMsg.message = 2;
                        msgsnd(MQ2_id, &schMsg, sizeof(schMsg.message), 0);
                        completed_processes++;
                    } 
                    // Legal memory access.
                    else {
                        // Page Hit scenario.
                        if (page_table[i * m + page_num].valid == 1) {
                            pageTimeStamp[i][page_num] = timestamp; // Update LRU tracking
                            
                            // Return the physical frame number.
                            pgf_msg.m_type = p_pid;
                            pgf_msg.message.data = page_table[i * m + page_num].frame_no;
                            msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);
                        } 
                        // Page Fault scenario.
                        else {
                            printf("Page fault sequence - (%d,%d)\n", process_num, page_num);
                            memset(str, '\0', sizeof(str));
                            sprintf(str, "Page fault sequence - (%d,%d)\n", process_num, page_num);
                            write(fd, str, strlen(str));

                            // Initiate page replacement.
                            PageFaultHandler(i, m, f, page_num, page_table, free_frame_list, pageTimeStamp);
                            page_faults[i]++;

                            // Instruct process to pause execution pending page load (-1).
                            pgf_msg.m_type = p_pid;
                            pgf_msg.message.data = -1;
                            msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);

                            // Notify Scheduler to re-enqueue the process.
                            schMsg.m_type = 1;
                            schMsg.message = 1;
                            msgsnd(MQ2_id, &schMsg, sizeof(schMsg.message), 0);
                        }
                    }
                    break; // Process found, break inner loop.
                }
            }
        }
        timestamp++; 
    }

    // Output final system statistics.
    for (int i = 0; i < k; i++) {
        printf("Process %d: No. of Page faults = %d, No. of Invalid page references = %d\n", i + 1, page_faults[i], invalid_page_ref[i]);
        char str[500];
        memset(str, '\0', sizeof(str));
        sprintf(str, "Process %d: No. of Page faults = %d, No. of Invalid page references = %d\n", i + 1, page_faults[i], invalid_page_ref[i]);
        write(fd, str, strlen(str));
    }

    close(fd);
    pause(); 
    return EXIT_SUCCESS;
}
