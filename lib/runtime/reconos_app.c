/*
 *                                                        ____  _____
 *                            ________  _________  ____  / __ \/ ___/
 *                           / ___/ _ \/ ___/ __ \/ __ \/ / / /\__ \
 *                          / /  /  __/ /__/ /_/ / / / / /_/ /___/ /
 *                         /_/   \___/\___/\____/_/ /_/\____//____/
 *
 * ======================================================================
 *
 *   title:        Application library
 *
 *   project:      ReconOS
 *   author:       Andreas Agne, University of Paderborn
 *                 Christoph Rüthing, University of Paderborn
 *   description:  Auto-generated application specific header file
 *                 including definitions of all resources and functions
 *                 to instantiate resources and threads automatically.
 *
 * ======================================================================
 */

<<reconos_preproc>>

#include "reconos_app.h"

#include "reconos.h"
#include "utils.h"

/* == Application resources ============================================ */

/*
 * @see header
 */
<<generate for RESOURCES(Type == "mbox")>>
struct mbox <<NameLower>>_s;
struct mbox *<<NameLower>> = &<<NameLower>>_s;
<<end generate>>

<<generate for RESOURCES(Type == "sem")>>
sem_t <<NameLower>>_s;
sem_t *<<NameLower>> = &<<NameLower>>_s;
<<end generate>>

<<generate for RESOURCES(Type == "mutex")>>
pthread_mutex_t <<NameLower>>_s;
pthread_mutex_t *<<NameLower>> = &<<NameLower>>_s;
<<end generate>>

<<generate for RESOURCES(Type == "cond")>>
pthread_cond <<NameLower>>_s;
pthread_cond *<<NameLower>> = &<<NameLower>>_s;
<<end generate>>

<<generate for RESOURCES>>
struct reconos_resource <<NameLower>>_res = {
	.ptr = &<<NameLower>>_s,
	.type = RECONOS_RESOURCE_TYPE_<<TypeUpper>>
};
<<end generate>>

struct mbox* resources_mbox_array [<<TotalResourceCount>>];

/* == Application functions ============================================ */

/*
 * @see header
 */
void reconos_app_init() {
	<<generate for RESOURCES(Type == "mbox")>>
	mbox_init(<<NameLower>>, <<Args>>);
	<<end generate>>

	<<generate for RESOURCES(Type == "sem")>>
	sem_init(<<NameLower>>, <<Args>>);
	<<end generate>>

	<<generate for RESOURCES(Type == "mutex")>>
	pthread_mutex_init(<<NameLower>>, NULL);
	<<end generate>>

	<<generate for RESOURCES(Type == "cond")>>
	pthread_cond_init(<<NameLower>>, NULL);
	<<end generate>>

	int i = 0;
	<<generate for RESOURCES(Type == "mbox")>>
	resources_mbox_array[i++] = &<<NameLower>>_s;
	<<end generate>>
}

/*
 * @see header
 */
void reconos_app_cleanup() {
	<<generate for RESOURCES(Type == "mbox")>>
	mbox_destroy(<<NameLower>>);
	<<end generate>>

	<<generate for RESOURCES(Type == "sem")>>
	sem_destroy(<<NameLower>>);
	<<end generate>>

	<<generate for RESOURCES(Type == "mutex")>>
	pthread_mutex_destroy(<<NameLower>>);
	<<end generate>>

	<<generate for RESOURCES(Type == "cond")>>
	pthread_cond_destroy(<<NameLower>>, NULL);
	<<end generate>>
}

/*
 * Empty software thread if no software specified
 *
 *   data - pointer to ReconOS thread
 */
void *swt_idle(void *data) {
	pthread_exit(0);
}

<<generate for THREADS>>
struct reconos_resource *resources_<<Name>>[] = {<<Resources>>};

<<=generate for HasHw=>>
/*
 * @see header
 */
struct reconos_thread *reconos_thread_create_hwt_<<Name>>() {
	struct reconos_thread *rt = (struct reconos_thread *)malloc(sizeof(struct reconos_thread));
	if (!rt) {
		panic("[reconos-core] ERROR: failed to allocate memory for thread\n");
	}

	int slots[] = {<<Slots>>};
	reconos_thread_init(rt, "<<Name>>", 0);
	reconos_thread_setinitdata(rt, 0);
	reconos_thread_setallowedslots(rt, slots, <<SlotCount>>);
	reconos_thread_setresourcepointers(rt, resources_<<Name>>, <<ResourceCount>>);
	reconos_thread_create_auto(rt, RECONOS_THREAD_HW);

	return rt;
}

/*
 * @see header
 */
struct reconos_thread *reconos_thread_create_hwt_in_slot_<<Name>>(int slot, void* init_data) {
	struct reconos_thread *rt = (struct reconos_thread *)malloc(sizeof(struct reconos_thread));
	if (!rt) {
		panic("[reconos-core] ERROR: failed to allocate memory for thread\n");
	}

	int slots[] = {slot};
	reconos_thread_init(rt, "<<Name>>", 0);
	reconos_thread_setinitdata(rt, init_data);
	reconos_thread_setallowedslots(rt, slots, 1);
	reconos_thread_setresourcepointers(rt, resources_<<Name>>, <<ResourceCount>>);
	reconos_thread_create_auto(rt, RECONOS_THREAD_HW);

	return rt;
}
<<=end generate=>>

<<=generate for HasSw=>>
extern void *rt_<<Name>>(void *data);

/*
 * @see header
 */
struct reconos_thread *reconos_thread_create_swt_<<Name>>() {
	struct reconos_thread *rt = (struct reconos_thread *)malloc(sizeof(struct reconos_thread));
	if (!rt) {
		panic("[reconos-core] ERROR: failed to allocate memory for thread\n");
	}

	int slots[] = {<<Slots>>};
	reconos_thread_init(rt, "<<Name>>", 0);
	reconos_thread_setinitdata(rt, 0);
	reconos_thread_setallowedslots(rt, slots, <<SlotCount>>);
	reconos_thread_setresourcepointers(rt, resources_<<Name>>, <<ResourceCount>>);
	reconos_thread_setswentry(rt, rt_<<Name>>);
	reconos_thread_create_auto(rt, RECONOS_THREAD_SW);

	return rt;
}

/*
 * @see header
 */
struct reconos_thread *reconos_thread_create_swt_in_slot_<<Name>>(int slot, void* init_data) {
	struct reconos_thread *rt = (struct reconos_thread *)malloc(sizeof(struct reconos_thread));
	if (!rt) {
		panic("[reconos-core] ERROR: failed to allocate memory for thread\n");
	}

	int slots[] = {slot};
	reconos_thread_init(rt, "<<Name>>", 0);
	reconos_thread_setinitdata(rt, init_data);
	reconos_thread_setallowedslots(rt, slots, 1);
	reconos_thread_setresourcepointers(rt, resources_<<Name>>, <<ResourceCount>>);
	reconos_thread_setswentry(rt, rt_<<Name>>);
	reconos_thread_create_auto(rt, RECONOS_THREAD_SW);

	return rt;
}
<<=end generate=>>

/*
 * @see header
 */
void reconos_thread_destroy_<<Name>>(struct reconos_thread *rt) {
	// not implemented yet
}
<<end generate>>

<<generate for CLOCKS>>
/*
 * @see header
 */
int reconos_clock_<<NameLower>>_set(int f)
{
	return reconos_clock_set(<<Id>>, <<M>>, f);
}
<<end generate>>
