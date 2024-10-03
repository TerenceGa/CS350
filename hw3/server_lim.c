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
* Last Update:
*     September 25, 2024
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
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

double timespec_to_seconds(struct timespec ts) {
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
// #queue_size globle init
int queue_size = 0;

struct queue {
    /* IMPLEMENT ME */
	struct meta_request *meta_requests;
    int front;      // Points to the front of the queue
    int rear;       // Points to the next insertion point
    int count;      // Number of elements in the queue
	int queue_size; // Maximum number of elements in the queue
    volatile int termination_flag; // Flag to signal termination
};

struct worker_params {
    /* IMPLEMENT ME */
	int socket;
	struct queue *the_queue;
};



/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct meta_request to_add, struct queue * the_queue, int conn_socket)
{
	int retval = 0;
	struct timespec reject_timestamp;
	
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if (the_queue->count < the_queue->queue_size) {
        the_queue->meta_requests[the_queue->rear] = to_add;
        the_queue->rear = (the_queue->rear + 1) % the_queue->queue_size;
        the_queue->count++;
		printf("INFO: Added REQ %ld to queue. Queue count: %d\n", to_add.req.request_id, the_queue->count);
        retval = 1;
		
		sem_post(queue_notify);
	} else {
		printf("INFO: Queue is full. Rejecting request %ld\n", to_add.req.request_id);
		clock_gettime(CLOCK_MONOTONIC, &reject_timestamp);
		printf("preparing to send %d\n", conn_socket);
		struct response rej_res;
		rej_res.request_id = to_add.req.request_id;
		rej_res.reserved = 0;
		rej_res.ack = 1;
		if (send(conn_socket, &rej_res, sizeof(rej_res), 0) < 0) {
			perror("send failed");
		}

		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		printf("X%ld:%.6f,%.6f,%.6f\n",
               to_add.req.request_id,
               timespec_to_seconds(to_add.req.sent_timestamp),
               timespec_to_seconds(to_add.req.request_length),
               timespec_to_seconds(reject_timestamp));
		dump_queue_status(the_queue);
		
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */

	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct meta_request get_from_queue(struct queue * the_queue)
{
    struct meta_request retval;
    /* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
    sem_wait(queue_notify);
    sem_wait(queue_mutex);
    /* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

    if (the_queue->count == 0) {
        if (the_queue->termination_flag) {
            // Signal worker to terminate
            retval.req.request_id = -1; // Sentinel value
            sem_post(queue_mutex);
            return retval;
        } else {
            // Queue is empty but termination flag not set
            // Release the mutex and wait again
            sem_post(queue_mutex);
            continue; // This would require a loop, so we need to handle it differently
        }
    }

    // Proceed to dequeue
    retval = the_queue->meta_requests[the_queue->front];
    the_queue->front = (the_queue->front + 1) % the_queue->queue_size;
    the_queue->count--;
    printf("INFO: Dequeued REQ %ld from queue. Queue count: %d\n", retval.req.request_id, the_queue->count);

    /* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
    sem_post(queue_mutex);
    /* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
    return retval;
}



/* Implement this method to correctly dump the status of the queue
 * following the format Q:[R<request ID>,R<request ID>,...] */
void dump_queue_status(struct queue * the_queue)
{
	int i;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	printf("Q:[");
	for (i = 0; i < the_queue->count; i++) {
		int index = (the_queue->front + i) % the_queue->queue_size;
		printf("R%d", the_queue->meta_requests[index].req.request_id);
		if (i != the_queue->count - 1) {
			printf(",");
		}
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
/* IMPLEMENT HERE THE MAIN FUNCTION OF THE WORKER */

void *worker_main(void *arg) {
    struct worker_params *params = (struct worker_params *)arg;
    struct queue *the_queue = params->the_queue;
    struct meta_request m_req;
    int conn_socket = params->socket;  // Get the socket descriptor
    struct response res;
    ssize_t out_bytes;

    while (1) {
        printf("INFO: Worker thread waiting for requests...\n");

        // Wait for items to be added to the queue
        sem_wait(queue_notify);

        // Protect access to the queue
        sem_wait(queue_mutex);

        // Check if there are items in the queue
        if (the_queue->count > 0) {
            // Dequeue the request
            m_req = the_queue->meta_requests[the_queue->front];
            the_queue->front = (the_queue->front + 1) % the_queue->queue_size;
            the_queue->count--;
            printf("INFO: Dequeued REQ %ld from queue. Queue count: %d\n", m_req.req.request_id, the_queue->count);
            sem_post(queue_mutex);

            // Process the request
            printf("INFO: Worker thread processing request %ld\n", m_req.req.request_id);

            // Record start timestamp
            struct timespec start_time, completion_time;
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            // Process the request by performing a busywait
            busywait(m_req.req.request_length);

            // Prepare the response
            res.request_id = m_req.req.request_id;
            res.reserved = 0;
            res.ack = 0;

            // Sending the response back to the client
            printf("INFO: Sending response to client for request %ld\n", m_req.req.request_id);
            send(conn_socket, &res, sizeof(res), 0);
            printf("INFO: Response sent for request %ld on socket %d\n", m_req.req.request_id, conn_socket);

            // Record completion timestamp
            clock_gettime(CLOCK_MONOTONIC, &completion_time);

            // Print the report
            printf("R%ld:%.6f,%.6f,%.6f,%.6f,%.6f\n",
                   m_req.req.request_id,
                   timespec_to_seconds(m_req.req.sent_timestamp),
                   timespec_to_seconds(m_req.req.request_length),
                   timespec_to_seconds(m_req.receipt_timestamp),
                   timespec_to_seconds(start_time),
                   timespec_to_seconds(completion_time));

            // Dump queue status
            dump_queue_status(the_queue);

        } else if (the_queue->termination_flag) {
            // Queue is empty and termination flag is set
            sem_post(queue_mutex);
            printf("INFO: Termination flag set and queue empty. Exiting worker thread.\n");
            break;
        } else {
            // Queue is empty but termination flag not set
            // Release the mutex and wait again
            sem_post(queue_mutex);
            continue;
        }
    }

    // Clean up and exit the thread
    free(params);
    return NULL;
}


/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket)
{
    struct request *req;
    struct meta_request m_req;
    struct queue *the_queue = malloc(sizeof(struct queue));
    struct worker_params *params;
    pthread_t worker_thread;
    ssize_t in_bytes;
    size_t total_bytes_read;
    char *buffer;

	/* The connection with the client is alive here. Let's
	 * initialize the shared queue. */

	if (the_queue == NULL) {
		perror("Failed to allocate memory for queue");
		close(conn_socket);
		return;
	}

	the_queue->meta_requests = (struct meta_request *)malloc(sizeof(struct meta_request) * queue_size);
	if (the_queue->meta_requests == NULL) {
		perror("Failed to allocate memory for meta_requests");
		free(the_queue);
		close(conn_socket);
		return;
	}
	/* IMPLEMENT HERE ANY QUEUE INITIALIZATION LOGIC */
	the_queue->front = 0;
    the_queue->rear = 0;
    the_queue->count = 0;
	the_queue->queue_size = queue_size;
    the_queue->termination_flag = 0;
	/* Queue ready to go here. Let's start the worker thread. */

	/* IMPLEMENT HERE THE LOGIC TO START THE WORKER THREAD. */
	params = (struct worker_params *)malloc(sizeof(struct worker_params));
	params->the_queue = the_queue;
    params->socket = conn_socket;  // **Ensure this assignment is present**
	/* We are ready to proceed with the rest of the request
	 * handling logic. */
	int ret = pthread_create(&worker_thread, NULL, worker_main, (void *)params);
	/* REUSE LOGIC FROM HW1 TO HANDLE THE PACKETS */

	 req = (struct request *)malloc(sizeof(struct request));
    buffer = (char *)req;
    do {
        total_bytes_read = 0;
        while (total_bytes_read < sizeof(struct request)) {
            in_bytes = recv(conn_socket, buffer + total_bytes_read, sizeof(struct request) - total_bytes_read, 0);
            if (in_bytes == 0) {
                // Connection closed
                break;
            } else if (in_bytes < 0) {
                perror("recv failed");
                break;
            } else {
                total_bytes_read += in_bytes;
            }
        }
        if (in_bytes <= 0) {
            // Error or connection closed
            break;
        }
        if (total_bytes_read == sizeof(struct request)) {
            clock_gettime(CLOCK_MONOTONIC, &m_req.receipt_timestamp);
            m_req.req = *req;
            add_to_queue(m_req, the_queue, conn_socket);
        }
    } while (1);

	/* PERFORM ORDERLY DEALLOCATION AND OUTRO HERE */
	
	/* Ask the worker thead to terminate */
	/* ASSERT TERMINATION FLAG FOR THE WORKER THREAD */
	free(req);
    the_queue->termination_flag = 1;
	/* Make sure to wake-up any thread left stuck waiting for items in the queue. DO NOT TOUCH */
	sem_post(queue_notify);
	pthread_join(worker_thread, NULL);
	/* Wait for orderly termination of the worker thread */	
	/* ADD HERE LOGIC TO WAIT FOR TERMINATION OF WORKER */
	
	/* FREE UP DATA STRUCTURES AND SHUTDOWN CONNECTION WITH CLIENT */
	free(the_queue->meta_requests);
	free(the_queue);
    close(conn_socket);
    printf("INFO: Client disconnected.\n");
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;
	int opt;
    

	/* Parse all the command line arguments */
	/* IMPLEMENT ME!! */
	/* PARSE THE COMMANDS LINE: */
	/* 1. Detect the -q parameter and set aside the queue size  */
	/* Use getopt to parse -q <queue_size> */
	while ((opt = getopt(argc, argv, "q:")) != -1) {
        switch (opt) {
            case 'q':
                queue_size = atoi(optarg);
                if (queue_size <= 0) {
                    fprintf(stderr, "Invalid queue size: %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            default:
                fprintf(stderr, USAGE_STRING, argv[0]);
                return EXIT_FAILURE;
        }
    }
	if (optind >= argc) {
    fprintf(stderr, "Expected <port_number> after options\n");
    fprintf(stderr, USAGE_STRING, argv[0]);
    return EXIT_FAILURE;
	}
	/* 2. Detect the port number to bind the server socket to (see HW1 and HW2) */
	socket_port = atoi(argv[optind]);
	if (socket_port <= 0) {
        fprintf(stderr, "Invalid port number: %s\n", argv[optind]);
        return EXIT_FAILURE;
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
	queue_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_notify = (sem_t *)malloc(sizeof(sem_t));
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
	/* DONE - Initialize queue protection variables. DO NOT TOUCH */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;

}
