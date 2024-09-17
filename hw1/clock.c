/*******************************************************************************
* CPU Clock Measurement Using RDTSC
*
* Description:
*     This C file provides functions to compute and measure the CPU clock using
*     the `rdtsc` instruction. The `rdtsc` instruction returns the Time Stamp
*     Counter, which can be used to measure CPU clock cycles.
*
* Author:
*     Renato Mancuso
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
*     Ensure that the platform supports the `rdtsc` instruction before using
*     these functions. Depending on the CPU architecture and power-saving
*     modes, the results might vary. Always refer to the CPU's official
*     documentation for accurate interpretations.
*
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "timelib.h"

int main (int argc, char ** argv)
{


    /* Parse the number of seconds */
    char *endptr;
    long sec = strtol(argv[1], &endptr, 10);


    /* Parse the number of nanoseconds */
    long nsec = strtol(argv[2], &endptr, 10);


    /* Parse the method ('s' or 'b') */
    char method = argv[3][0];


    /* Determine the wait method */
    uint64_t elapsed_cycles = 0;
    const char *method_str;
    double wait_time_seconds = sec + ((double)nsec) / 1e9;
    double clock_speed_mhz = 0.0;

    if (method == 's') {
        method_str = "SLEEP";
        elapsed_cycles = get_elapsed_sleep(sec, nsec);
    } else { /* method == 'b' */
        method_str = "BUSYWAIT";
        elapsed_cycles = get_elapsed_busywait(sec, nsec);
    }



    /* Calculate CPU clock speed in MHz */
    clock_speed_mhz = (double)elapsed_cycles / wait_time_seconds / 1e6;

    /* Print the results */
    printf("WaitMethod: %s\n", method_str);
    printf("WaitTime: %ld %ld\n", sec, nsec);
    printf("ClocksElapsed: %llu\n", (unsigned long long)elapsed_cycles);
    printf("ClockSpeed: %.2f\n", clock_speed_mhz);


	return EXIT_SUCCESS;
}

