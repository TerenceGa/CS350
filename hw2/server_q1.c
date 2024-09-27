/*******************************************************************************
* Simple FIFO Order Server Implementation
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch.
*
* Usage:
*     <build directory>/server <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 202
*
* Last Changes:
*     September 16, 2024
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. For debugging or more details, refer
*     to the accompanying documentation and logs.
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
	"Usage: %s <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)
/* Max number of requests that can be queued */
#define QUEUE_SIZE 1000

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

/* Define the queue structure */
struct queue {
    struct request_meta queue[QUEUE_SIZE];
    int head;   
    int tail;   
    int count;  
};

/* Structure to pass parameters to the worker thread */
struct worker_params {
    struct queue * the_queue;
    void * conn_socket;
};

/* Add a new request <to_add> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	if (the_queue->count >= QUEUE_SIZE) {
		retval = -1; // Queue is full
	} else {
		the_queue->queue[the_queue->tail] = to_add;
		the_queue->tail = (the_queue->tail + 1) % QUEUE_SIZE;
		the_queue->count++;
		retval = 0;
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	sem_post(queue_notify);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Get a request from the shared queue <the_queue> */
struct request_meta get_from_queue(struct queue * the_queue)
{
	struct request_meta retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	retval = the_queue->queue[the_queue->head];
	the_queue->head = (the_queue->head + 1) % QUEUE_SIZE;
	the_queue->count--;

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
	printf("Q:[");
    for (int i = 0; i < the_queue->count; i++) {
        int index = (the_queue->head + i) % QUEUE_SIZE;
        printf("R%ld", the_queue->queue[index].req.req_id);
        if (i < the_queue->count - 1) {
            printf(",");
        }
 	}
	printf("]\n");

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

int termination_flag=0;
/* Main logic of the worker thread */
void * worker_thread_func(void * args)
{
	struct worker_params * params = (struct worker_params *) args;
	struct queue * queue = params->the_queue;
	struct timespec start_timestamp;
 	struct timespec completion_timestamp;

	while (termination_flag != 1) {
		// Get a request from the queue
		struct request_meta req = get_from_queue(params->the_queue);

		if (params -> the_queue->count<0){
			continue;
		}
		struct response resp;
		resp.req_id = req.req.req_id;  
		resp.ack = 0;  
		resp.reserved = 0;  

		clock_gettime(CLOCK_MONOTONIC, &start_timestamp);

		// Process the request
		get_elapsed_busywait(req.req.req_length.tv_sec, req.req.req_length.tv_nsec);

		// Record the completion timestamp
		clock_gettime(CLOCK_MONOTONIC, &completion_timestamp);

		// Send response back to client
		send(*(int*)(params->conn_socket), &resp, sizeof(struct response), 0);

		// Print the report
		printf("R%ld:%lf,%lf,%lf,%lf,%lf\n", req.req.req_id,
			TSPEC_TO_DOUBLE(req.req.req_timestamp),
			TSPEC_TO_DOUBLE(req.req.req_length),
			TSPEC_TO_DOUBLE(req.receipt_ts),
			TSPEC_TO_DOUBLE(start_timestamp),
			TSPEC_TO_DOUBLE(completion_timestamp));

		// Dump queue status
		dump_queue_status(queue);
	}

}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket)
{
	struct request * req;
	struct queue * the_queue;

	/* The connection with the client is alive here. Let's
	 * initialize the shared queue. */

	/* IMPLEMENT HERE ANY QUEUE INITIALIZATION LOGIC */
	the_queue = (struct queue *)malloc(sizeof(struct queue));
	the_queue->head = 0;
	the_queue->tail = 0;
	the_queue->count = 0;

	/* Queue ready to go here. Let's start the worker thread. */
	/* IMPLEMENT HERE THE LOGIC TO START THE WORKER THREAD. */
	pthread_t worker_thread;
    struct worker_params params;

	params.the_queue = the_queue;
	int *socket_ptr = malloc(sizeof(int));
	if (socket_ptr == NULL) {
		perror("Failed to allocate memory");
		close(conn_socket);
		return;
	}
	socket_ptr = &conn_socket;
	params.conn_socket = (void*) socket_ptr;

	// Create the worker thread
	if (pthread_create(&worker_thread, NULL, worker_thread_func, &params) != 0) {
		free(the_queue);  // Free the queue if the thread creation fails
		close(conn_socket);
		return;
	}

	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	/* REUSE LOGIC FROM HW1 TO HANDLE THE PACKETS */
	req = (struct request *)malloc(sizeof(struct request));
	struct request_meta *req_meta = (struct request_meta*)malloc(sizeof(struct request_meta));
 	struct timespec receipt_ts;

	int in_bytes;
	do {
		in_bytes = recv(conn_socket, req, sizeof(struct request), 0);
		if (in_bytes <= 0) {
			break;
		}
		clock_gettime(CLOCK_MONOTONIC, &receipt_ts);
		req_meta->receipt_ts = receipt_ts;
		req_meta->req = *req;
		add_to_queue(*req_meta, the_queue); 

		/* Don't just return if in_bytes is 0 or -1. Instead
		 * skip the response and break out of the loop in an
		 * orderly fashion so that we can de-allocate the req
		 * and resp variables, and shutdown the socket. */
	} while (in_bytes > 0);

	/* PERFORM ORDERLY DEALLOCATION AND OUTRO HERE */
	free(req);
	free(req_meta);

	/* Ask the worker thread to terminate */
	/* ASSERT TERMINATION FLAG FOR THE WORKER THREAD */
	termination_flag = 1;

	/* Make sure to wake-up any thread left stuck waiting for items in the queue. DO NOT TOUCH */
	sem_post(queue_notify);

	/* Wait for orderly termination of the worker thread */	
	/* ADD HERE LOGIC TO WAIT FOR TERMINATION OF WORKER */
	pthread_join(worker_thread, NULL);

	/* FREE UP DATA STRUCTURES AND SHUTDOWN CONNECTION WITH CLIENT */
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

	/* Get port to bind our socket to */
	if (argc > 1) {
		socket_port = strtol(argv[1], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
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
