/*
 *                                                        ____  _____
 *                            ________  _________  ____  / __ \/ ___/
 *                           / ___/ _ \/ ___/ __ \/ __ \/ / / /\__ \
 *                          / /  /  __/ /__/ /_/ / / / / /_/ /___/ /
 *                         /_/   \___/\___/\____/_/ /_/\____//____/
 *
 * ======================================================================
 *
 *   title:        ReconOS library - Main library
 *
 *   project:      ReconOS
 *   author:       Andreas Agne, University of Paderborn
 *                 Markus Happe, University of Paderborn
 *                 Daniel Borkmann, ETH Zürich
 *                 Sebastian Meisner, University of Paderborn
 *                 Christoph Rüthing, University of Paderborn
 *   description:  Main ReconOS library
 *
 * ======================================================================
 */

#include "reconos.h"
#include "private.h"
#include "arch/arch.h"
#include "legacy_os_calls/mbox.h"

#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>

int RECONOS_NUM_HWTS = 0;

static struct hwslot *_hwslots;
static int _proc_control;
static pthread_t _pgf_handler;
static struct sigaction _dt_signal;


/* == ReconOS resource ================================================= */

/*
 * @see header
 */
void reconos_resource_init(struct reconos_resource *rr,
                           int type, void *ptr) {
	rr->type = type;
	rr->ptr = ptr;
}


/* == ReconOS thread =================================================== */

/*
 * @see header
 */
void reconos_thread_init(struct reconos_thread *rt,
                         char* name,
                         int state_size) {
	rt->name = name;

	rt->init_data = NULL;
	rt->resources = NULL;
	rt->resource_count = 0;

	rt->state = RECONOS_THREAD_STATE_INIT;
	rt->state_data = malloc(state_size);
	if (!rt->state_data) {
		panic("[reconos-core] ERROR: failed to allocate memory for state\n");
	}
	//memset(rt->state_data, 0, state_size);

	rt->hwslot = NULL;

	rt->bitstreams = NULL;
	rt->bitstream_lengths = NULL;
}

/*
 * @see header
 */
void reconos_thread_setinitdata(struct reconos_thread *rt, void *init_data) {
	rt->init_data = init_data;
}

/*
 * @see header
 */
void reconos_thread_setresources(struct reconos_thread *rt,
                                 struct reconos_resource *resources,
                                 int resource_count) {
	rt->resources = resources;
	rt->resource_count = resource_count;
}

/*
 * @see header
 */
void reconos_thread_setbitstream(struct reconos_thread *rt,
                                 char **bitstreams,
                                 int *bitstream_lengths) {
	rt->bitstreams = bitstreams;
	rt->bitstream_lengths = bitstream_lengths;
}

/*
 * @see header
 */
void reconos_thread_loadbitstream(struct reconos_thread *rt,
                                  char *path) {
	FILE *file;
	unsigned int i, size;

	debug("[reconos-core] loading bitstreams from %s\n", path);

	rt->bitstream_lengths = (int *)malloc(RECONOS_NUM_HWTS * sizeof(int));
	if (!rt->bitstream_lengths) {
		panic("[reconos-core] ERROR: failed to allocate memory for bitstream");
	}

	rt->bitstreams = (char **)malloc(RECONOS_NUM_HWTS * sizeof(char *));
	if (!rt->bitstreams) {
		panic("[reconos-core] ERROR: failed to allocate memory for bitstream");
	}

	for (i = 0; i < RECONOS_NUM_HWTS; i++) {
		file = fopen(path, "rb");
		if (!file) {
			panic("[reconos-core] ERROR: failed to open bitstream\n");
		}

		fseek(file, 0L, SEEK_END);
		size = ftell(file);
		rewind(file);

		rt->bitstream_lengths[i] = size;
		rt->bitstreams[i] = (char *)malloc(size * sizeof(char));
		if (!rt->bitstreams[i]) {
			panic("[reconos-core] ERROR: failed to allocate memory for bitstream\n");
		}

		fread(rt->bitstreams[i], sizeof(char), size, file);

		fclose(file);
	}
}

/*
 * @see header
 */
void reconos_thread_create(struct reconos_thread *rt, int slot) {
	if (slot < 0 || slot >= RECONOS_NUM_HWTS) {
		panic("[reconos-core] ERROR: slot id out of range\n");
	}

	if (rt->state == RECONOS_THREAD_STATE_RUNNING_HW) {
		panic("[reconos-core] ERROR: thread is already running\n");
	}

	rt->hwslot = &_hwslots[slot];
	hwslot_createthread(rt->hwslot, rt);
	rt->state = RECONOS_THREAD_STATE_RUNNING_HW;
}

/*
 * @see header
 */
void reconos_thread_suspend(struct reconos_thread *rt) {
	if (rt->state != RECONOS_THREAD_STATE_RUNNING_HW) {
		panic("[reconos-core] ERROR: cannot suspend not running thread\n");
	}

	rt->state = RECONOS_THREAD_STATE_SUSPENDING;
	hwslot_suspendthread(rt->hwslot);
	rt->state = RECONOS_THREAD_STATE_SUSPENDED;
}

/*
 * @see header
 */
void reconos_thread_suspend_block(struct reconos_thread *rt) {
	if (rt->state != RECONOS_THREAD_STATE_RUNNING_HW) {
		panic("[reconos-core] ERROR: cannot suspend not running thread\n");
	}

	rt->state = RECONOS_THREAD_STATE_SUSPENDING;
	hwslot_suspendthread(rt->hwslot);
	hwslot_jointhread(rt->hwslot);
	rt->state = RECONOS_THREAD_STATE_SUSPENDED;
}

/*
 * @see header
 */
void reconos_thread_resume(struct reconos_thread *rt, int slot) {
	if (slot < 0 || slot >= RECONOS_NUM_HWTS) {
		panic("[reconos-core] ERROR: slot id out of range\n");
	}

	hwslot_resumethread(rt->hwslot, rt);
	rt->state = RECONOS_THREAD_STATE_RUNNING_HW;
}

/*
 * @see header
 */
void reconos_thread_join(struct reconos_thread *rt) {
	hwslot_jointhread(rt->hwslot);
}


/* == General functions ================================================ */

/*
 * Signal handler for exiting the program
 *
 *   sig - signal
 */
void exit_signal(int sig) {
	reconos_cleanup();

	printf("[reconos-core] aborted\n");

	exit(0);
}

/*
 * Signal handler for ínterrupting a delegate thread
 *
 *   sig - signal
 */
void delegate_signal(int sig) {
	debug("[reconos-core] delegate received signal\n");
}

/*
 * @see header
 */
void reconos_init() {
	int i, osif;

	signal(SIGINT, exit_signal);
	signal(SIGTERM, exit_signal);
	signal(SIGABRT, exit_signal);

	reconos_drv_init();

	_proc_control = reconos_proc_control_open();
	if (_proc_control < 0) {
		panic("[reconos-core] ERROR: unable to open proc control\n");
	}

	RECONOS_NUM_HWTS = reconos_proc_control_get_num_hwts(_proc_control);

	_hwslots = (struct hwslot *)malloc(RECONOS_NUM_HWTS * sizeof(struct hwslot));
	if (!_hwslots) {
		panic("[reconos-core] ERROR: unable to allocate memory for slots\n");
	}

	for (i = 0; i < RECONOS_NUM_HWTS; i++) {
		osif = reconos_osif_open(i);
		if (osif < 0)
			panic("[reconos-core] ERROR: unable to open osif %d\n", i);

		hwslot_init(&_hwslots[i], i, osif);
	}

	_dt_signal.sa_handler = delegate_signal;
	sigaction(SIGUSR1, &_dt_signal, NULL);

	reconos_proc_control_sys_reset(_proc_control);

	reconos_proc_control_set_pgd(_proc_control);

#ifdef RECONOS_MMU_true
	pthread_create(&_pgf_handler, NULL, proc_pgfhandler, NULL);
#endif
}

void reconos_cleanup() {
	reconos_proc_control_sys_reset(_proc_control);
}


/* == ReconOS proc control============================================== */

/*
 * @see header
 */
void *proc_pgfhandler(void *arg) {
	while (1) {
		uint32_t *addr;

		addr = (uint32_t *)reconos_proc_control_get_fault_addr(_proc_control);

		printf("[reconos_core] "
		       "page fault occured at address %x\n", (unsigned int)addr);

		*addr = 0;

		reconos_proc_control_clear_page_fault(_proc_control);
	}
}

/* == ReconOS hwslot =================================================== */

/*
 * @see header
 */
void hwslot_init(struct hwslot *slot, int id, int osif) {
	slot->id = id;
	slot->osif = osif;

	slot->rt = NULL;

	slot->dt = 0;
	slot->dt_state = DELEGATE_STATE_STOPPED;
	slot->dt_flags = 0;
	sem_init(&slot->dt_exit, 0, 0);
}

/*
 * @see header
 */
void hwslot_reset(struct hwslot *slot) {
	debug("[reconos-core] resetting slot %d\n", slot->id);
	reconos_proc_control_hwt_reset(_proc_control, slot->id, 1);
	reconos_proc_control_hwt_signal(_proc_control, slot->id, 0);
	reconos_proc_control_hwt_reset(_proc_control, slot->id, 0);
}

/*
 * @see header
 */
void hwslot_setreset(struct hwslot *slot, int reset) {
	reconos_proc_control_hwt_reset(_proc_control, slot->id, reset);
}

/*
 * @see header
 */
void hwslot_createdelegate(struct hwslot *slot) {
	if (slot->dt) {
		panic("[reconos-core] ERROR: delegate thread already running\n");
	}

	slot->dt_state = DELEGATE_STATE_INIT;
	slot->dt_flags = 0;

	pthread_create(&slot->dt, NULL, dt_delegate, slot);
}

/*
 * @see header
 */
void hwslot_createthread(struct hwslot *slot,
                         struct reconos_thread *rt) {
	if (slot->rt) {
		panic("[reconos-core] ERROR: a thread is already running\n");
	}

	slot->rt = rt;

	reconos_proc_control_hwt_reset(_proc_control, slot->id, 1);
	reconos_proc_control_hwt_signal(_proc_control, slot->id, 0);
	reconos_proc_control_hwt_reset(_proc_control, slot->id, 0);

	reconos_osif_write(slot->osif, (uint32_t)OSIF_SIGNAL_THREAD_START);

	hwslot_createdelegate(slot);
}

/*
 * @see header
 */
void hwslot_suspendthread(struct hwslot *slot) {
	if (!slot->rt) {
		panic("[reconos-core] ERROR: no thread running\n");
	}

	slot->dt_flags |= DELEGATE_FLAG_PAUSE_SYSCALLS;
	slot->dt_flags |= DELEGATE_FLAG_SUSPEND;

	switch (slot->dt_state) {
		case DELEGATE_STATE_BLOCKED_OSIF:
			reconos_osif_break(slot->osif);
			break;

		case DELEGATE_STATE_BLOCKED_SYSCALL:
			pthread_kill(slot->dt, SIGUSR1);
			break;
	}
}

/*
 * @see header
 */
void hwslot_resumethread(struct hwslot *slot,
                         struct reconos_thread *rt) {
	if (slot->rt) {
		panic("[reconos-core] ERROR: a thread is already running\n");
	}

	reconos_proc_control_hwt_reset(_proc_control, slot->id, 1);
	reconos_proc_control_hwt_signal(_proc_control, slot->id, 0);
	reconos_proc_control_hwt_reset(_proc_control, slot->id, 0);

	reconos_osif_write(slot->osif, (uint32_t)OSIF_SIGNAL_THREAD_RESUME);

	slot->rt = rt;
}

/*
 * @see header
 */
void hwslot_jointhread(struct hwslot *slot) {
	if (!slot->rt) {
		panic("[reconos-core] ERROR: no thread running\n");
	}

	sem_wait(&slot->dt_exit);

	reconos_proc_control_hwt_reset(_proc_control, slot->id, 1);
	reconos_proc_control_hwt_signal(_proc_control, slot->id, 0);
	slot->rt = NULL;
}


/* == ReconOS delegate ================================================= */


#define RESOURCE_CHECK_TYPE(p_handle, p_type) \
	if ((p_handle) >= slot->rt->resource_count) {\
		panic("[reconos-dt-%d] "\
		      "ERROR: resource count out of range\n",slot->id);\
	}\
	if (slot->rt->resources[(p_handle)].type != (p_type)) {\
		panic("[reconos-dt-%d] "\
		      "ERROR: wrong resource type\n", slot->id);\
	}

#define SYSCALL_NONBLOCK(p_call)\
	if (slot->dt_flags & DELEGATE_FLAG_PAUSE_SYSCALLS) {\
		debug("[reconos-dt-%d] "\
		      "interrupted in nonblocking syscall\n", slot->id);\
		goto intr;\
	}\
	p_call;

#define SYSCALL_BLOCK(p_call)\
	if (slot->dt_flags & DELEGATE_FLAG_PAUSE_SYSCALLS) {\
		debug("[reconos-dt-%d] "\
		      "interrupted in blocking syscall\n", slot->id);\
		goto intr;\
	}\
	if ((p_call) < 0) {\
		goto intr;\
	}

/*
 * Delegate function: Get initialization data
 *   Command: OSIF_CMD_GET_INIT_DATA
 *
 *   slot - pointer to the hardware slot
 */
static int dt_get_init_data(struct hwslot *slot) {
	reconos_osif_write(slot->osif, (uint32_t)slot->rt->init_data);

	return 0;
}

/*
 * Delegate function: Post Semaphore
 *   Command: OSIF_CMD_SEM_POST
 *   Syscall: sem_post
 *
 *   slot - pointer to the hardware slot
 */
static int dt_sem_post(struct hwslot *slot) {
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_SEM);

	debug("[reconos-dt-%d] (sem_post on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = sem_post(slot->rt->resources[handle].ptr));
	debug("[reconos-dt-%d] (sem_post on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Acquire Semaphore
 *   Command: OSIF_CMD_SEM_WAIT
 *   Syscall: sem_wait
 *
 *   slot - pointer to the hardware slot
 */
static int dt_sem_wait(struct hwslot *slot) {
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_SEM);

	debug("[reconos-dt-%d] (sem_wait on %d) ...\n", slot->id, handle);
	slot->dt_state = DELEGATE_STATE_BLOCKED_SYSCALL;
	SYSCALL_BLOCK(ret = sem_wait(slot->rt->resources[handle].ptr));
	slot->dt_state = DELEGATE_STATE_PROCESSING;
	debug("[reconos-dt-%d] (sem_wait on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Lock a mutex
 *   Command: OSIF_CMD_MUTEX_LOCK
 *   Syscall: pthread_mutex_lock
 *
 *   slot - pointer to the hardware slot
 */
static int dt_mutex_lock(struct hwslot *slot) {
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MUTEX);

	debug("[reconos-dt-%d] (mutex_lock on %d) ...\n", slot->id, handle);
	slot->dt_state = DELEGATE_STATE_BLOCKED_SYSCALL;
	SYSCALL_BLOCK(ret = pthread_mutex_lock(slot->rt->resources[handle].ptr));
	slot->dt_state = DELEGATE_STATE_PROCESSING;
	debug("[reconos-dt-%d] (mutex_lock on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Unlock a mutex
 *   Command: OSIF_CMD_MUTEX_UNLOCK
 *   Syscall: pthread_mutex_unlock
 *
 *   slot - pointer to the hardware slot
 */
static int dt_mutex_unlock(struct hwslot *slot) {
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MUTEX);

	debug("[reconos-dt-%d] (mutex_unlock on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = pthread_mutex_unlock(slot->rt->resources[handle].ptr));
	debug("[reconos-dt-%d] (mutex_unlock on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Tries to lock a mutex
 *   Command: OSIF_CMD_MUTEX_TRYLOCK
 *   Syscall: pthread_mutex_trylock
 *
 *   slot - pointer to the hardware slot
 */
static uint32_t dt_mutex_trylock(struct hwslot *slot) {
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MUTEX);

	debug("[reconos-dt-%d] (mutex_trylock on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = pthread_mutex_trylock(slot->rt->resources[handle].ptr));
	debug("[reconos-dt-%d] (mutex_trylock on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Waits for a condition variable
 *   Command: OSIF_CMD_COND_WAIT
 *   Syscall: pthread_cond_wait
 *
 *   slot - pointer to the hardware slot
 */
static int dt_cond_wait(struct hwslot *slot) {
#ifndef RECONOS_MINIMAL
	int handle, handle2, ret;

	handle = reconos_osif_read(slot->osif);
	handle2 = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_COND);
	RESOURCE_CHECK_TYPE(handle2, RECONOS_RESOURCE_TYPE_MUTEX);

	debug("[reconos-dt-%d] (cond_wait on %d) ...\n", slot->id, handle);
	slot->dt_state = DELEGATE_STATE_BLOCKED_SYSCALL;
	SYSCALL_BLOCK(ret = pthread_cond_wait(slot->rt->resources[handle].ptr,
	                                      slot->rt->resources[handle2].ptr));
	slot->dt_state = DELEGATE_STATE_PROCESSING;
	debug("[reconos-dt-%d] (cond_wait on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;
#else
	panic("[reconos-dt-%d] ERROR: (cond_wait on %d) not supported\n", slot->id, handle);

	return 0;
#endif

intr:
	return -1;
}

/*
 * Delegate function: Signals a condition variable
 *   Command: OSIF_CMD_COND_SIGNAL
 *   Syscall: pthread_cond_signal
 *
 *   slot - pointer to the hardware slot
 */
static int dt_cond_signal(struct hwslot *slot) {
#ifndef RECONOS_MINIMAL
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_COND);

	debug("[reconos-dt-%d] (cond_signal on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = pthread_cond_signal(slot->rt->resources[handle].ptr));
	debug("[reconos-dt-%d] (cond_signal on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;
#else
	panic("[reconos-dt-%d] ERROR: (cond_signal on %d) not supported\n", slot->id, handle);

	return 0;
#endif

intr:
	return -1;
}

/*
 * Delegate function: Broadcasts a condition variable
 *   Command: OSIF_CMD_COND_BROADCAST
 *   Syscall: pthread_cond_broadcast
 *
 *   slot - pointer to the hardware slot
 */
static uint32_t dt_cond_broadcast(struct hwslot *slot) {
#ifndef RECONOS_MINIMAL
	int handle, ret;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_COND);

	debug("[reconos-dt-%d] (cond_broadcast on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = pthread_cond_broadcast(slot->rt->resources[handle].ptr));
	debug("[reconos-dt-%d] (cond_broadcast on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;
#else
	panic("[reconos-dt-%d] ERROR: (cond_broadcast on %d) not supported\n", slot->id, handle);

	return 0;
#endif

intr:
	return -1;
}

/*
 * Delegate function: Gets a message from mbox
 *   Command: OSIF_CMD_MBOX_GET
 *   Syscall: mbox_get
 *
 *   slot - pointer to the hardware slot
 */
static int dt_mbox_get(struct hwslot *slot) {
	int handle, ret;
	uint32_t msg;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MBOX);

	debug("[reconos-dt-%d] (mbox_get on %d) ...\n", slot->id, handle);
	slot->dt_state = DELEGATE_STATE_BLOCKED_SYSCALL;
	SYSCALL_BLOCK(ret = mbox_get_interruptible(slot->rt->resources[handle].ptr, &msg));
	slot->dt_state = DELEGATE_STATE_PROCESSING;
	debug("[reconos-dt-%d] (mbox_get on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, msg);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Puts a message into mbox
 *   Command: OSIF_CMD_MBOX_PUT
 *   Syscall: mbox_put
 *
 *   slot - pointer to the hardware slot
 */
static int dt_mbox_put(struct hwslot *slot) {
	int handle, ret;
	uint32_t arg0;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MBOX);

	arg0 = reconos_osif_read(slot->osif);

	debug("[reconos-dt-%d] (mbox_put on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = mbox_put(slot->rt->resources[handle].ptr, arg0));
	debug("[reconos-dt-%d] (mbox_put on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Tries to get a message from mbox
 *   Command: OSIF_CMD_MBOX_TRYGET
 *   Syscall: mbox_tryget
 *
 *   slot - pointer to the hardware slot
 */
static int dt_mbox_tryget(struct hwslot *slot) {
	int handle, ret;
	uint32_t data;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MBOX);

	debug("[reconos-dt-%d] (mbox_tryget on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = mbox_tryget(slot->rt->resources[handle].ptr, &data));
	reconos_osif_write(slot->osif, data);
	debug("[reconos-dt-%d] (mbox_tryget on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * Delegate function: Tries to put a message into mbox
 *   Command: OSIF_CMD_MBOX_TYPUT
 *   Syscall: mbox_tryput
 *
 *   slot - pointer to the hardware slot
 */
static int dt_mbox_tryput(struct hwslot *slot) {
	int handle, ret;
	uint32_t arg0;

	handle = reconos_osif_read(slot->osif);
	RESOURCE_CHECK_TYPE(handle, RECONOS_RESOURCE_TYPE_MBOX);

	arg0 = reconos_osif_read(slot->osif);

	debug("[reconos-dt-%d] (mbox_tryput on %d) ...\n", slot->id, handle);
	SYSCALL_NONBLOCK(ret = mbox_tryput(slot->rt->resources[handle].ptr, arg0));
	debug("[reconos-dt-%d] (mbox_tryput on %d) done\n", slot->id, handle);

	reconos_osif_write(slot->osif, (uint32_t)ret);

	return 0;

intr:
	return -1;
}

/*
 * @see header
 */
void *dt_delegate(void *arg) {
	struct hwslot *slot;
	uint32_t cmd;
	int ret;

	slot = (struct hwslot *)arg;

	slot->dt_state = DELEGATE_STATE_PROCESSING;

	while(1) {
		if (slot->dt_flags & DELEGATE_FLAG_SUSPEND) {
			slot->dt_flags &= ~DELEGATE_FLAG_SUSPEND;
			reconos_proc_control_hwt_signal(_proc_control, slot->id, 1);
		}

		debug("[reconos-dt-%d] waiting for command ...\n", slot->id);
		slot->dt_state = DELEGATE_STATE_BLOCKED_OSIF;
		cmd = reconos_osif_read(slot->osif);
		slot->dt_state = DELEGATE_STATE_PROCESSING;
		debug("[reconos-dt-%d] received command 0x%x\n", slot->id, cmd);

		switch (cmd & OSIF_CMD_MASK) {
			case OSIF_CMD_MBOX_PUT:
				ret = dt_mbox_put(slot);
				break;

			case OSIF_CMD_MBOX_TRYPUT:
				ret = dt_mbox_tryput(slot);
				break;

			case OSIF_CMD_SEM_POST:
				ret = dt_sem_post(slot);
				break;

			case OSIF_CMD_MUTEX_UNLOCK:
				ret = dt_mutex_unlock(slot);
				break;

			case OSIF_CMD_COND_SIGNAL:
				ret = dt_cond_signal(slot);
				break;

			case OSIF_CMD_COND_BROADCAST:
				ret = dt_cond_broadcast(slot);
				break;

			case OSIF_CMD_MBOX_GET:
				ret = dt_mbox_get(slot);
				break;

			case OSIF_CMD_MBOX_TRYGET:
				ret = dt_mbox_tryget(slot);
				break;

			case OSIF_CMD_SEM_WAIT:
				ret = dt_sem_wait(slot);
				break;

			case OSIF_CMD_MUTEX_LOCK:
				ret = dt_mutex_lock(slot);
				break;

			case OSIF_CMD_MUTEX_TRYLOCK:
				ret = dt_mutex_trylock(slot);
				break;

			case OSIF_CMD_COND_WAIT:
				ret = dt_cond_wait(slot);
				break;

			case OSIF_CMD_THREAD_GET_INIT_DATA:
				ret = dt_get_init_data(slot);
				break;

			case OSIF_CMD_THREAD_EXIT:
				reconos_proc_control_hwt_reset(_proc_control, slot->id, 1);
				reconos_proc_control_hwt_signal(_proc_control, slot->id, 0);
				slot->dt_flags = 0;
				sem_post(&slot->dt_exit);
				break;

			case OSIF_CMD_THREAD_GET_STATE_ADDR:
				slot->dt_flags &= ~DELEGATE_FLAG_PAUSE_SYSCALLS;
				reconos_osif_write(slot->osif, (uint32_t)slot->rt->state_data);
				break;

			case OSIF_INTERRUPTED:
				break;

			default:
				panic("[reconos-dt-%d] ERROR received unknown command\n", slot->id);
				break;
		}
	}

return NULL;
}