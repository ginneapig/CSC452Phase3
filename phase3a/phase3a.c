/*
 * Names: Annie Gao, Raymond Rea
 * netid: anniegao, raymondprea
 *
 * phase3a.c
 *
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3Int.h"

P3_VmStats  P3_vmStats;

// TODO: use this? or can faults just be tracked with an id, have an int array
typedef struct Fault {
    int pid;    // process that's faulting
    int page;   // page on which process is faulting
    int frame;  // not sure if this remains unassigned initially, P3PageFaultResolve seems to assign this later
} Fault;

// queue of faults
// locks to protect accessing queue of faults b/c shared b/t FaultHandler and Pager

int SHUTDOWN = FALSE;

static void
FaultHandler(int type, void *arg)
{
    /*******************

    if it's an access fault (USLOSS_MmuGetCause)
        terminate faulting process w/ P3_ACCESS_VIOLATION
    else
        add fault information to a queue of pending faults
        let the pager know that there is a pending fault
        wait until the fault has been handled by the pager
        terminate the process if necessary

    *********************/
    int cause = USLOSS_MmuGetCause();
    if (cause == USLOSS_MMU_ACCESS) { // if it's an access fault
        // terminate process with P3_ACCESS_VIOLATION;
    }

}

static int 
Pager(void *arg)
{
    /*******************

    loop until P3_VmShutdown is called
        wait for a fault
        if the process does not have a page table
            call USLOSS_Abort with an error message
        rc = P3PageFaultResolve(pid, page, &frame)
        if rc == P3_OUT_OF_SWAP
            mark the faulting process for termination
        else
            if rc == P3_NOT_IMPLEMENTED
                frame = page
            update PTE in page table to map page to frame
       unblock faulting process

    *********************/
    CheckMode();

    while (!SHUTDOWN) {
        while (0) { // while no requests
            //Wait();
        }
        // update P3_vmStats->pages, ->frames, etc. if successful mapping
    }
    return 0;
}

int
P3_VmInit(int unused, int pages, int frames, int pagers)
{
    CheckMode();

    // zero P3_vmStats
    // initialize fault queue, lock, and condition variable
    // call P3FrameInit
    // call P3SwapInit
    // fork pager

    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;
    return P1_SUCCESS;
}

void
P3_VmShutdown(void)
{
    CheckMode();
    
    // cause pager to quit
    P3_PrintStats(&P3_vmStats);

    SHUTDOWN = TRUE;
}

USLOSS_PTE *
P3_AllocatePageTable(int pid)
{
    CheckMode();

    USLOSS_PTE  *table = NULL;
    // create a new page table here
    return table;
}

void
P3_FreePageTable(int pid)
{
    CheckMode();

    // free the page table here
}

int
P3PageTableGet(PID pid, USLOSS_PTE **table)
{
    CheckMode();
    
    *table = NULL;
    return P1_SUCCESS;
}

int P3_Startup(void *arg)
{
    CheckMode();
    
    int pid;
    int pid4;
    int status;
    int rc;

    rc = Sys_Spawn("P4_Startup", P4_Startup, NULL,  3 * USLOSS_MIN_STACK, 2, &pid4);
    assert(rc == 0);
    assert(pid4 >= 0);
    rc = Sys_Wait(&pid, &status);
    assert(rc == 0);
    assert(pid == pid4);
    Sys_VmShutdown();
    return 0;
}

void
P3_PrintStats(P3_VmStats *stats)
{
    USLOSS_Console("P3_PrintStats:\n");
    USLOSS_Console("\tpages:\t\t%d\n", stats->pages);
    USLOSS_Console("\tframes:\t\t%d\n", stats->frames);
    USLOSS_Console("\tblocks:\t\t%d\n", stats->blocks);
    USLOSS_Console("\tfreeFrames:\t%d\n", stats->freeFrames);
    USLOSS_Console("\tfreeBlocks:\t%d\n", stats->freeBlocks);
    USLOSS_Console("\tfaults:\t\t%d\n", stats->faults);
    USLOSS_Console("\tnewPages:\t%d\n", stats->newPages);
    USLOSS_Console("\tpageIns:\t%d\n", stats->pageIns);
    USLOSS_Console("\tpageOuts:\t%d\n", stats->pageOuts);
    USLOSS_Console("\treplaced:\t%d\n", stats->replaced);
}

