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

#ifndef RECONOS_APP_H
#define RECONOS_APP_H

#include "mbox.h"

#include <pthread.h>
#include <semaphore.h>

/* == Application resources ============================================ */

/*
 * Definition of different resources of the application.
 *
 *   mbox  - mailbox (struct mbox)
 *   sem   - semaphore (sem_t)
 *   mutex - mutex (pthread_mutex)
 *   cond  - condition variable (pthread_cond)
 */
<<generate for RESOURCES(Type == "mbox")>>
extern struct mbox <<NameLower>>_s;
extern struct mbox *<<NameLower>>;
<<end generate>>

<<generate for RESOURCES(Type == "sem")>>
extern sem_t <<NameLower>>_s;
extern sem_t *<<NameLower>>;
<<end generate>>

<<generate for RESOURCES(Type == "mutex")>>
extern pthread_mutex_t <<NameLower>>_s;
extern pthread_mutex_t *<<NameLower>>;
<<end generate>>

<<generate for RESOURCES(Type == "cond")>>
extern pthread_cond <<NameLower>>_s;
extern pthread_cond *<<NameLower>>;
<<end generate>>

extern struct mbox* resources_mbox_array [<<TotalResourceCount>>];

/* == Application functions ============================================ */

/*
 * Initializes the application by creating all resources.
 */
void reconos_app_init();

/*
 * Cleans up the application by destroying all resources.
 */
void reconos_app_cleanup();

<<generate for THREADS>>
<<=generate for HasHw=>>
/*
 * Creates a hardware thread in one of the specified slots with its associated
 * resources.
 *
 *   returns: pointer to the ReconOS thread
 */
struct reconos_thread *reconos_thread_create_hwt_<<Name>>();

/*
 * Creates a hardware thread in one specific slot with its associated
 * resources and init data.
 *
 *   returns: pointer to the ReconOS thread
 */
struct reconos_thread *reconos_thread_create_hwt_in_slot_<<Name>>(int slot, void* init_data);
<<=end generate=>>

<<=generate for HasSw=>>
/*
 * Creates a software thread with its associated resources.
 *
 *   returns: pointer to the ReconOS thread
 */
struct reconos_thread *reconos_thread_create_swt_<<Name>>();

/*
 * Creates a software thread with its associated resources and init data.
 *
 *   returns: pointer to the ReconOS thread
 */
struct reconos_thread *reconos_thread_create_swt_in_slot_<<Name>>(int slot, void* init_data);
<<=end generate=>>

/*
 * Destroys a hardware thread created.
 *
 *   rt   - pointer to the ReconOS thread
 */
void reconos_thread_destroy_<<Name>>(struct reconos_thread *rt);
<<end generate>>

<<generate for CLOCKS>>
/*
 * Sets the frequency for the given clock. Returns the actual clock that
 * the system was able to configure.
 *
 *   f - the wanted frequency in kHz
 */
int reconos_clock_<<NameLower>>_set(int f);
<<end generate>>

#endif /* RECONOS_APP_H */