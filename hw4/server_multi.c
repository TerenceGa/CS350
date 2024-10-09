/*******************************************************************************
* Dual-Threaded FIFO Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue size.
*
* Usage:
*     <build directory>/server -q <queue_size> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 29, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. If the queue is full at the time a
*     new request is received, the request is rejected with a negative ack.
*
*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>
/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s -q <queue size> <port_number>\n"


/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
sem_t * print_mutex;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */
int worker_count = 1;

/* Synchronized printf for multi-threaded operation */
#define printf_m(...)				\
	do {					\
		sem_wait(print_mutex);		\
		printf(__VA_ARGS__);		\
		sem_post(print_mutex);		\
	} while (0)

struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
};

struct queue {
	size_t wr_pos;
	size_t rd_pos;
	size_t max_size;
	size_t available;
	struct request_meta * requests;
};

struct worker_params {
    int thread_id;          
    int conn_socket;
    int worker_done;
    struct queue *the_queue;
};

void queue_init(struct queue * the_queue, size_t queue_size)
{
	the_queue->rd_pos = 0;
	the_queue->wr_pos = 0;
	the_queue->max_size = queue_size;
	the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta)
						     * the_queue->max_size);
	the_queue->available = queue_size;
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if (the_queue->available == 0) {
		retval = 1;
	} else {
		/* If all good, add the item in the queue */
		the_queue->requests[the_queue->wr_pos] = to_add;
		the_queue->wr_pos = (the_queue->wr_pos + 1) % the_queue->max_size;
		the_queue->available--;

		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		sem_post(queue_notify);
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct request_meta get_from_queue(struct queue * the_queue)
{
	struct request_meta retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	retval = the_queue->requests[the_queue->rd_pos];
	the_queue->rd_pos = (the_queue->rd_pos + 1) % the_queue->max_size;
	the_queue->available++;

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

void dump_queue_status(struct queue * the_queue)
{
	size_t i, j;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	printf("Q:[");

	for (i = the_queue->rd_pos, j = 0; j < the_queue->max_size - the_queue->available;
	     i = (i + 1) % the_queue->max_size, ++j)
	{
		printf("R%ld%s", the_queue->requests[i].request.req_id,
		       ((j+1 != the_queue->max_size - the_queue->available)?",":""));
	}

	printf("]\n");

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

void busywait(struct timespec duration) {
    struct timespec start, current, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Calculate the end time
    end.tv_sec = start.tv_sec + duration.tv_sec;
    end.tv_nsec = start.tv_nsec + duration.tv_nsec;

    // Normalize the end time
    if (end.tv_nsec >= 1e9) {
        end.tv_sec += end.tv_nsec / (long)1e9;
        end.tv_nsec = end.tv_nsec % (long)1e9;
    }


    do {
        clock_gettime(CLOCK_MONOTONIC, &current);
        
        if ((current.tv_sec > end.tv_sec) ||
            (current.tv_sec == end.tv_sec && current.tv_nsec >= end.tv_nsec)) {
            break;
        }
    } while (1);
}

/* Main logic of the worker thread */
void *worker_main (void * arg)
{
    struct timespec now;
    struct worker_params *params = (struct worker_params *)arg;
    int thread_id = params->thread_id;

	/* Print the first alive message. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(now));

	/* Okay, now execute the main logic. */
	while (!params->worker_done) {

		struct request_meta req;
		struct response resp;
		req = get_from_queue(params->the_queue); // execution might be blocked here while client closes; when the last sem_post(notify) hits, this will return invalid/wrong pointer
		if(params->worker_done) {  // thus add this line to terminate 
			break;
		}

		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);
		busywait(req.request.req_length);
		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

		/* Now provide a response! */
		resp.req_id = req.request.req_id;
		resp.ack = RESP_COMPLETED;
		send(params->conn_socket, &resp, sizeof(struct response), 0);

        printf_m("T%d R%ld:%lf,%lf,%lf,%lf,%lf\n", thread_id, req.request.req_id,
                TSPEC_TO_DOUBLE(req.request.req_timestamp),
                TSPEC_TO_DOUBLE(req.request.req_length),
                TSPEC_TO_DOUBLE(req.receipt_timestamp),
                TSPEC_TO_DOUBLE(req.start_timestamp),
                TSPEC_TO_DOUBLE(req.completion_timestamp)
            );

		dump_queue_status(params->the_queue);
	}

	return NULL;
}

/* This function will start the worker thread wrapping around the
 * pthread_create() POSIX api function call*/
int start_worker(pthread_t* p, void * params)
{
	int retval;

	retval = pthread_create(p, NULL, worker_main, params);

	return retval;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket, size_t queue_size)
{
    struct request_meta * req;
    struct queue * the_queue;
    size_t in_bytes;

    /* The connection with the client is alive here. Let's start
     * the worker thread. */
    struct worker_params worker_params;
    int worker_id;

    /* Now handle queue allocation and initialization */
    the_queue = (struct queue *)malloc(sizeof(struct queue));
    queue_init(the_queue, queue_size);

    pthread_t *threads = malloc(worker_count * sizeof(pthread_t));
    struct worker_params *params = malloc(worker_count * sizeof(struct worker_params));


    for (int i = 0; i < worker_count; i++) {
        params[i].thread_id = i;
        params[i].conn_socket = conn_socket;
        params[i].worker_done = 0;
        params[i].the_queue = the_queue;

        if (pthread_create(&threads[i], NULL, worker_main, (void *)&params[i]) != 0) {
            perror("Failed to create worker thread");
            /* Handle partial thread creation */
            for (int j = 0; j < i; j++) {
                params[j].worker_done = 1;
                sem_post(queue_notify);  // Wake up any waiting threads
            }
            /* Join already created threads */
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            free(threads);
            free(params);
            free(the_queue);
            ERROR_INFO();
            return;
        }

        printf("INFO: Worker thread %d started.\n", i);
    }
    /* We are ready to proceed with the rest of the request
     * handling logic. */

    req = (struct request_meta *)malloc(sizeof(struct request_meta));

    do {
        in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
        clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

        /* Don't just return if in_bytes is 0 or -1. Instead
         * skip the response and break out of the loop in an
         * orderly fashion so that we can de-allocate the req
         * and resp varaibles, and shutdown the socket. */
        if (in_bytes > 0) {
            int res = add_to_queue(*req, the_queue);

            /* The queue is full if the return value is 1 */
            if (res) {
                struct response resp;
                /* Now provide a response! */
                resp.req_id = req->request.req_id;
                resp.ack = RESP_REJECTED;
                send(conn_socket, &resp, sizeof(struct response), 0);

                printf_m("X%ld:%lf,%lf,%lf\n", req->request.req_id,
                       TSPEC_TO_DOUBLE(req->request.req_timestamp),
                       TSPEC_TO_DOUBLE(req->request.req_length),
                       TSPEC_TO_DOUBLE(req->receipt_timestamp)
                    );

                dump_queue_status(the_queue);
            }
        }
    } while (in_bytes > 0);

    /* Ask the worker threads to terminate */
    printf("INFO: Asserting termination flag for worker thread...\n");
    for (int i = 0; i < worker_count; i++) {
        params[i].worker_done = 1;
    }

    /* Make sure to wake-up all worker threads */
    for (int i = 0; i < worker_count; i++) {
        sem_post(queue_notify);
    }

    /* Wait for orderly termination of the worker threads */
    for (int i = 0; i < worker_count; i++) {
        pthread_join(threads[i], NULL);
        printf("INFO: Worker thread %d has exited.\n", i);
    }

    printf("INFO: Worker threads exited.\n");

    free(threads);
    free(params);
    free(the_queue);
    free(req);
    shutdown(conn_socket, SHUT_RDWR);
    close(conn_socket);
    printf("INFO: Client disconnected.\n");
}



/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval, opt;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;
	uint64_t queue_size;
	queue_size = 0;

	/* Parse all the command line arguments */
    while((opt = getopt(argc, argv, "q:w:")) != -1) {
        switch (opt) {
    case 'q':
        queue_size = strtol(optarg, NULL, 10);
        printf("INFO: setting queue size as %ld\n", queue_size);
        break;
    case 'w':
        worker_count = atoi(optarg);
        if (worker_count < 1) {
            fprintf(stderr, "Worker count must be a positive integer.\n");
            exit(EXIT_FAILURE);
        }
        printf("INFO: setting worker count as %d\n", worker_count);
        break;
	}

	if (!queue_size) {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

	if (optind < argc) {
		socket_port = strtol(argv[optind], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}
	}

	/* Now onward to create the right type of socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		ERROR_INFO();
		perror("Unable to create socket");
		return EXIT_FAILURE;
	}

	/* Before moving forward, set socket to reuse address */
	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

	/* Convert INADDR_ANY into network byte order */
	any_address.s_addr = htonl(INADDR_ANY);

	/* Time to bind the socket to the right port  */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(socket_port);
	addr.sin_addr = any_address;

	/* Attempt to bind the socket with the given parameters */
	retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to bind socket");
		return EXIT_FAILURE;
	}

	/* Let us now proceed to set the server to listen on the selected port */
	retval = listen(sockfd, BACKLOG_COUNT);

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to listen on socket");
		return EXIT_FAILURE;
	}

	/* Ready to accept connections! */
	printf("INFO: Waiting for incoming connection...\n");
	client_len = sizeof(struct sockaddr_in);
	accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);

	if (accepted == -1) {
		ERROR_INFO();
		perror("Unable to accept connections");
		return EXIT_FAILURE;
	}

	/* Initialize queue protection variables. DO NOT TOUCH. */
	print_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_notify = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(print_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize print mutex");
		return EXIT_FAILURE;
	}
	retval = sem_init(queue_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue mutex");
		return EXIT_FAILURE;
	}
	retval = sem_init(queue_notify, 0, 0);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue notify");
		return EXIT_FAILURE;
	}
	/* DONE - Initialize queue protection variables */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted, queue_size);

	free(print_mutex);
	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;

}
