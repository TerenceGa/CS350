/*******************************************************************************
* Time Functions Library (implementation)
*
* Description:
*     A library to handle various time-related functions and operations.
*
* Author:
*     Renato Mancuso <rmancuso@bu.edu>
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Last Update:
*     September 9, 2024
*
* Notes:
*     Ensure to link against the necessary dependencies when compiling and
*     using this library. Modifications or improvements are welcome. Please
*     refer to the accompanying documentation for detailed usage instructions.
*
*******************************************************************************/

#include "timelib.h"
#include "timelib.h"
#include <errno.h>      // Added to define errno and EINTR



/* Return the number of clock cycles elapsed when waiting for
 * wait_time seconds using sleeping functions */
uint64_t get_elapsed_sleep(long sec, long nsec)
{
    uint64_t start, end;
    struct timespec req, rem;

    /* Initialize the timespec structure with the requested sleep time */
    req.tv_sec = sec;
    req.tv_nsec = nsec;

    /* Normalize the timespec structure if nanoseconds >= 1,000,000,000 */
    if (req.tv_nsec >= NANO_IN_SEC) {
        req.tv_sec += req.tv_nsec / NANO_IN_SEC;
        req.tv_nsec = req.tv_nsec % NANO_IN_SEC;
    }

    /* Get the initial TSC value before sleeping */
    get_clocks(start);


    /* Attempt to sleep for the specified duration */
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            /* If sleep was interrupted by a signal, continue sleeping for the remaining time */
            req = rem;
        } else {
            /* For other errors, print an error message and return 0 */
            perror("nanosleep failed");
            return 0;
        }
    }

    /* Get the TSC value after sleeping */
    get_clocks(end);


    /* Calculate and return the difference in clock cycles */
    return (end - start);
}
/* Return the number of clock cycles elapsed when waiting for
 * wait_time seconds using busy-waiting functions */
uint64_t get_elapsed_busywait(long sec, long nsec)
{
    uint64_t start_tsc, end_tsc;
    struct timespec begin_timestamp, current_timestamp;
    struct timespec target_timestamp;

    // Get the current time as begin_timestamp
    if (clock_gettime(CLOCK_MONOTONIC, &begin_timestamp) != 0) {
        perror("clock_gettime failed");
        return 0;
    }

    // Calculate target_timestamp = begin_timestamp + (sec, nsec)
    target_timestamp.tv_sec = begin_timestamp.tv_sec + sec;
    target_timestamp.tv_nsec = begin_timestamp.tv_nsec + nsec;

    // Normalize target_timestamp
    if (target_timestamp.tv_nsec >= 1000000000L) {
        target_timestamp.tv_sec += target_timestamp.tv_nsec / 1000000000L;
        target_timestamp.tv_nsec = target_timestamp.tv_nsec % 1000000000L;
    }

    // Get start TSC
    get_clocks(start_tsc);

    while (1) {
        // Get the current time
        if (clock_gettime(CLOCK_MONOTONIC, &current_timestamp) != 0) {
            perror("clock_gettime failed");
            return 0;
        }

        // Check if current_timestamp >= target_timestamp
        if ((current_timestamp.tv_sec > target_timestamp.tv_sec) ||
            (current_timestamp.tv_sec == target_timestamp.tv_sec &&
             current_timestamp.tv_nsec >= target_timestamp.tv_nsec)) {
            break;
        }
    }

    // Get end TSC
    get_clocks(end_tsc);

    return (end_tsc - start_tsc);
}
/* Utility function to add two timespec structures together. The input
 * parameter a is updated with the result of the sum. */
void timespec_add (struct timespec * a, struct timespec * b)
{
	/* Try to add up the nsec and see if we spill over into the
	 * seconds */
	time_t addl_seconds = b->tv_sec;
	a->tv_nsec += b->tv_nsec;
	if (a->tv_nsec > NANO_IN_SEC) {
		addl_seconds += a->tv_nsec / NANO_IN_SEC;
		a->tv_nsec = a->tv_nsec % NANO_IN_SEC;
	}
	a->tv_sec += addl_seconds;
}

/* Utility function to compare two timespec structures. It returns 1
 * if a is in the future compared to b; -1 if b is in the future
 * compared to a; 0 if they are identical. */
int timespec_cmp(struct timespec *a, struct timespec *b)
{
	if(a->tv_sec == b->tv_sec && a->tv_nsec == b->tv_nsec) {
		return 0;
	} else if((a->tv_sec > b->tv_sec) ||
		  (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec)) {
		return 1;
	} else {
		return -1;
	}
}

/* Busywait for the amount of time described via the delay
 * parameter */
uint64_t busywait_timespec(struct timespec delay)
{
	/* IMPLEMENT ME! (Optional but useful) */
}
