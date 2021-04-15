/*
 * Names: Annie Gao, Raymond Rea
 * netid: anniegao, raymondprea
  *
 * phase3a.c
 *
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3Int.h"

#define CHECKKERNEL() \
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) USLOSS_IllegalInstruction()

#define LOCK(lid) { \
    int _rc = P1_Lock(lid); \
    assert(_rc == P1_SUCCESS); \
}

#define UNLOCK(lid) { \
    int _rc = P1_Unlock(lid); \
    assert(_rc == P1_SUCCESS); \
}

#define WAIT(vid) { \
    int _rc = P1_Wait(vid); \
    assert(_rc == P1_SUCCESS); \
}

//P3_VmStats P3_VmStats;

P3_VmStats *stats;

// TODO: use this? or can faults just be tracked with an id, have an int array
// TODO what other fields should go here?

typedef struct Fault Fault;

struct Fault {
    int pid;    // process that's faulting
    int page;   // page on which process is faulting
    int frame;  // not sure if this remains unassigned initially, P3PageFaultResolve seems to assign this later
    int markedForTerm; // if the process causing the fault needs to be terminated 
    int cause; // what caused the fault 

    Fault *next;
};

// queue of faults
// locks to protect accessing queue of faults b/c shared b/t FaultHandler and Pager

//locks
int PAGER_LOCK;
int FALUTQ_LOCK;
int STATS_LOCK;

//condition variables 
int Q_NOT_EMPTY;
//TODO should this go in the fault struct?
int FAULT_HANDLED;
int PAGER_QUIT;

//pager will use this as state variable 
Fault *HEAD;
//fault handler will use this as state variable 
int PAGER_DONE = FALSE;
//shutdown will use this as a state variable
int pagerQuit = FALSE;

int STARTUP  = FALSE;
int SHUTDOWN = FALSE;
// more globals 
int *vmRegion;
int *pmAddr;
int pageSize;
int numPages;
int numFrames;
int mode;

//TODO make 2d array [proc][page num]
USLOSS_PTE *tables;

static void InitStub(USLOSS_Sysargs *sysargs);
static void ShutdownStub(USLOSS_Sysargs *sysargs);

int enQFault(Fault *fault) {
    if (HEAD == NULL) {
        HEAD = fault;
        return;
    }
    
    Fault *curr = HEAD;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = fault;
    return 0;
}

Fault * deQFault() {
    if (HEAD == NULL) {return;}

    Fault *removed = HEAD;
    HEAD = HEAD->next;
    return removed;
}

static void
FaultHandler(int type, void *arg)
{
    int rc;
    int offset = (int) arg;
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
        P2_Terminate(P3_ACCESS_VIOLATION);
    } else {
        //TODO how do i get the rest of the info? 
        Fault *newFault         = malloc(sizeof(Fault));
        newFault->pid           = P1_GetPid();
        newFault->cause         = cause;
        newFault->page          = offset/pagesize; 
        newFault->markedForTerm = FALSE;
        newFault->next          = NULL;

        // add fault to queue
        LOCK(FAULTQ_LOCK);
        rc = enQFault(newFault);
        assert(rc == 0);
        
        // signal pager 
        rc = P1_Signal(Q_NOT_EMPTY);
        assert(rc == P1_SUCCESS);
        UNLOCK(FAULTQ_LOCK);

        // wait until fault has been handled 
        LOCK(PAGER_LOCK);
        while(!PAGER_DONE) {
            WAIT(FAULT_HANDLED);
        }
        PAGER_DONE = FALSE;
        UNLOCK(PAGER_LOCK);

        // terminate the process if necessary
        // TODO how do i know if it's necessary?
        // rc = P2_Terminate(P3_OUT_OF_SWAP);
        // assert(rc == P1_SUCCESS);
        
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
    int rc;  
    int frame;
    Fault *currFault;

    while (!SHUTDOWN) {

        LOCK(FAULTQ_LOCK);
        // while no requests 
        while (HEAD == NULL) { 
            // wait for a fault 
            WAIT(Q_NOT_EMPTY);
        }
        currFault = deQFault();
        UNLOCK(FAULTQ_LOCK);

        // if calling process doesn't have page table 
        if (tables[currFault->pid] == NULL) {
            USLOSS_Abort("Faulting process does not have a page table\n");
        }
        rc = P3PageFaultResolve(currFault->pid, currFault->page, &frame); 
        if (rc == P3_OUT_OF_SWAP) {
            currFault->markedForTerm = TRUE;
        } else {
            USLOSS_PTE *currPTE = tables[currFault->pid][currFault->page];
            if (rc == P3_NOT_IMPLEMENTED) {
                currPTE->frame = currFault->page;
            }
            currPTE->incore = TRUE;
            //update mmu
            rc = USLOSS_MmuSetPageTable(currPTE);
            assert(rc == USLOSS_MMU_OK); 

        }
        //TODO unblock faulting process 
        LOCK(PAGER_LOCK);
        rc = P1_Signal(FAULT_HANDLED);
        assert(rc == P1_SUCCESS);
        UNLOCK(PAGER_LOCK);
        // update P3_vmStats->pages, ->frames, etc. if successful mapping
    }
    return 0;
}

int
P3_VmInit(int unused, int pages, int frames, int pagers)
{
    CheckMode();
    int rc;
    int pagerPid;

    if (startup == TRUE) { return P3_ALREADY_INITIALIZED; }
    if (pages < 0)  { return P3_INVALID_NUM_PAGES; }
    if (frams < 0)  { return P3_INVALID_NUM_FRAMES; }
    if (pagers < 0 || pagers != P3_MAX_PAGERS) { return P3_INVALID_NUM_PAGERS; }

    // zero stats
    stats = malloc(sizeof(P3_VmInit));
    stats->pages      = 0;
    stats->frames     = 0;
    stats->blocks     = 0;
    stats->freeFrames = 0;
    stats->freeBlocks = 0;
    stats->faults     = 0;
    stats->newPages   = 0;
    stats->pageIns    = 0;
    stats->pageOuts   = 0;
    stats->replaced   = 0;    

    // initilizing fault queue, lock and cond var
    HEAD = NULL; 
    rc = P1_LockCreate("fault q lock", &FAULTQ_LOCK);
    assert(rc == P1_SUCCESS);
    rc = P1_CondCreate("q not empty", FAULTQ_LOCK, &Q_NOT_EMPTY);

    // call P3FrameInit, P3SwapInit
    rc = P3FrameInit(pages, frames);
    assert(rc == P1_SUCCESS);
    rc = P3SwapInit(pages, frames);
    assert(rc == P1_SUCCESS);

    //numMaps is not used, passing in 0, to avoid null arg err
    rc = USLOSS_MmuInit(0, pages, frames);
    assert(rc == USLOSS_MMU_OK);
 
    STARTUP = TRUE;

    // fork pager                
    rc = P1_Fork("pager", Pager, P3_MAX_PAGERS, USLOSS_MIN_STACK, &pagerPid);

    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;
    return P1_SUCCESS;
}

void
P3_VmShutdown(void)
{
    CheckMode();
    int rc;
    // do nothing if P3_VmInit hasn't been called
    if (!startup) {return;}

    // waiting for the pager to finish
    LOCK(PAGER_LOCK); 
    while(!pagerQuit) {
        WAIT(PAGER_QUIT);
    }    
    UNLOCK(PAGER_LOCK);

    rc = USLOSS_MmuDone();
    assert(rc == USLOSS_MMU_OK);    

    P3_PrintStats(&P3_vmStats);

    SHUTDOWN = TRUE;
}

USLOSS_PTE *
P3_AllocatePageTable(int pid)
{
    CheckMode();
    int i;
    // if P3_VmInit hasn't been called 
    if (!STARTUP) {return NULL;}

    USLOSS_PTE  *pte = NULL;
    pte = malloc(sizeof(USLOSS_PTE));
    pte->incore = FALSE;
    pte->read   = TRUE;
    pte->write  = TRUE;
    pte->frame  = -1;  

    // setting the newly created PTE into the array 
    // of proccesses PTEs
    for(i=0; i<P1_MAXPROC; i++) {
        if (tables[pid][i] == NULL) {
            tables[pid][i] = pte;
            break;
        }
    }
 
    return table;
}

void
P3_FreePageTable(int pid)
{
    CheckMode();
    int rc;
    // do nothing if init hasn't been called
    if (!STARTUP) {return;}

    rc = P3FrameFreeAll(pid);
    assert(rc == P1_SUCCESS);
  
    rc = P3SwapFreeAll(pid);
    assert(rc == P1_SUCCESS);

    // freeing this processes page table 
    USLOSS_PTE *freed = tables[pid]
    free(freed);
    tables[pid] = NULL;
}

int
P3PageTableGet(PID pid, USLOSS_PTE **table)
{
    CheckMode();
    if (pid < 0 || pid >= P1_MAXPROC) {return P1_INVALID_PID;}

    *table = tables[pid];
    return P1_SUCCESS;
}

int P3_Startup(void *arg)
{
    CheckMode();
    
    int pid;
    int pid4;
    int status;
    int rc;
    int i;

    tables = malloc(P1_MAXPROC * numPages * sizeof(USLOSS_PTE));

    rc = Sys_Spawn("P4_Startup", P4_Startup, NULL,  3 * USLOSS_MIN_STACK, 2, &pid4);
    assert(rc == 0);
    assert(pid4 >= 0);

    // initializing array of page tables
    for (i=0; i < P1_MAXPROC; i++){
        tables[i] = NULL;
    }    

    rc = P1_LockCreate("pager lock", &PAGER_LOCK);
    assert(rc == P1_SUCCESS);
    rc = P1_LockCreate("fault lock", &FAULT_LOCK);
    assert(rc == P1_SUCESS);
    rc = P1_CondCreate("fault handled", PAGER_LOCK, &FAULT_HANDLED);
    assert(rc == P1_SUCCESS);
    rc = P1_CondCreate("pager quit", PAGER_LOCK, &PAGER_QUIT);
    assert(rc == P1_SUCCESS);

    rc = Sys_Wait(&pid, &status);
    assert(rc == 0);
    assert(pid == pid4);
    Sys_VmShutdown();
    return 0;
}

static void InitStub(USLOSS_Sysargs *sysargs) {
    int rc;

    sysargs->arg4 = (void *) P3_VmInit(0, (int) sysargs->arg2, (int) sysargs->arg3, (int) sysargs->arg4);

    rc = USLOSS_MmuGetConfig(&vmRegion, &pmAddr, &pageSize, &numPages, &numFrames, &mode);
    assert(rc == USLOSS_MMU_OK);    
    sysargs->arg1 = (void *) *vmRegion;
    sysargs->arg2 = (void *) pageSize;
}

static void ShutdownStub(USLOSS_Sysargs *sysargs) {
    P3_VmShutdown();
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

