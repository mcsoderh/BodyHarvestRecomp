// BH polls the controllers from a dedicated high-priority thread that loops
// osContStartReadData -> osRecvMesg(SI) -> osContGetReadData. On hardware the
// SI transfer takes real time, so the thread blocks on every iteration. The
// runtime completes it instantly, so the thread never blocks and starves every
// lower-priority thread. Wait one frame before each read so the thread yields.

#include "patches.h"

typedef struct {
    int storage[6];
} OSMesgQueue;

typedef struct {
    long long storage[4];
} OSTimer;

typedef void* OSMesg;

#define OS_MESG_BLOCK 1

// One NTSC frame in CPU count cycles (46.875 MHz).
#define POLL_INTERVAL_CYCLES (16667ULL * 46875000ULL / 1000000ULL)

// Runtime implementations of the libultra functions (see syms.ld).
void osCreateMesgQueue_recomp(OSMesgQueue* mq, OSMesg* msg, int count);
int osRecvMesg_recomp(OSMesgQueue* mq, OSMesg* msg, int flag);
int osSetTimer_recomp(OSTimer* timer, unsigned long long countdown, unsigned long long interval, OSMesgQueue* mq, OSMesg msg);
void osContGetReadData_recomp(void* data);

// Controller pad data.
extern char D_800475B8[];

static OSMesgQueue poll_queue;
static OSMesg poll_msg;
static OSTimer poll_timer;
static int poll_queue_created;

RECOMP_PATCH void func_80002ED4_3AD4(void) {
    if (!poll_queue_created) {
        osCreateMesgQueue_recomp(&poll_queue, &poll_msg, 1);
        poll_queue_created = 1;
    }
    osSetTimer_recomp(&poll_timer, POLL_INTERVAL_CYCLES, 0, &poll_queue, 0);
    osRecvMesg_recomp(&poll_queue, 0, OS_MESG_BLOCK);

    osContGetReadData_recomp(D_800475B8);
}
