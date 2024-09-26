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
*     September 10, 2023
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

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* Main logic of the worker thread */
/* IMPLEMENT HERE THE MAIN FUNCTION OF THE WORKER */
void *worker_main(void *arg) {
    struct timespec ts;
    char buffer[100];

    // Get the current time for the initial message
    clock_gettime(CLOCK_REALTIME, &ts);
    double timestamp = ts.tv_sec + ts.tv_nsec / 1e9;

    printf("[#WORKER#] %.6f Worker Thread Alive!\n", timestamp);

    // Enter an infinite loop
    while (1) {
        // 1. Busywait for exactly 1 second
        clock_gettime(CLOCK_REALTIME, &ts);
        double start_time = ts.tv_sec + ts.tv_nsec / 1e9;
        double current_time = start_time;
        while ((current_time - start_time) < 1.0) {
            clock_gettime(CLOCK_REALTIME, &ts);
            current_time = ts.tv_sec + ts.tv_nsec / 1e9;
        }

        // 2. Print "Still Alive!" message with timestamp
        clock_gettime(CLOCK_REALTIME, &ts);
        timestamp = ts.tv_sec + ts.tv_nsec / 1e9;
        printf("[#WORKER#] %.6f Still Alive!\n", timestamp);

        // 3. Sleep for exactly 1 second
        sleep(1);
    }

    return NULL; // Although this point is never reached
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket)
{

	/* The connection with the client is alive here. Let's start
	 * the worker thread. */

	/* IMPLEMENT HERE THE LOGIC TO START THE WORKER THREAD. */
	int pthread_create(pthread_t *thread, 
                   const pthread_attr_t *attr,
                   void *(*start_routine) (void *), 
                   void *arg);
	pthread_t worker_thread;
    int ret = pthread_create(&worker_thread, NULL, worker_main, NULL);
	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	/* REUSE LOGIC FROM HW1 TO HANDLE THE PACKETS */
    struct timespec ts_recv;    // Timestamp of the last received request
    struct timespec ts_compli;  // Timestamp of the last completed request
    struct request req;         // Request received from the client
    struct response resp;       // Response to be sent to the client
    ssize_t result;             // Result of the send() function
    ssize_t received;           // Result of the recv() function

    while (1){
        // Receiving the request
        received = recv(conn_socket, &req, sizeof(req), 0);
        if (received == 0) {
            // Client has closed the connection
            printf("INFO: Client disconnected (socket %d)\n", conn_socket);
            break;
        }
        else if (received < 0) {
            // Error in receiving data
            ERROR_INFO();
            perror("Error receiving data");
            break;
        }
        else if (received != sizeof(req)) {
            // Incomplete request received
            ERROR_INFO();
            fprintf(stderr, "Incomplete request received. Expected %zu bytes, got %zd bytes.\n",
                    sizeof(req), received);
            break;
        }

        // Recording the receipt timestamp
        if (clock_gettime(CLOCK_MONOTONIC, &ts_recv) == -1) {
            ERROR_INFO();
            perror("clock_gettime failed");
            break;
        }



        // Recording the completion timestamp
        if (clock_gettime(CLOCK_MONOTONIC, &ts_compli) == -1) {
            ERROR_INFO();
            perror("clock_gettime failed");
            break;
        }

        // Preparing the response
        resp.request_id = req.request_id;
        resp.reserved = 0;
        resp.ack = 0;

        // Sending the response back to the client
        result = send(conn_socket, &resp, sizeof(resp), 0);
        if (result < 0) {
            // Error in sending data
            ERROR_INFO();
            perror("Error sending data");
            break;
        }
        else if (result != sizeof(resp)) {
            // Incomplete response sent
            ERROR_INFO();
            fprintf(stderr, "Incomplete response sent. Expected %zu bytes, sent %zd bytes.\n",
                    sizeof(resp), result);
            break;
        }

        // Logging the handled request in the specified format
        printf("R%lu:%.6lf,%.6lf,%.6lf,%.6lf\n",
               req.request_id,
               TSPEC_TO_DOUBLE(req.sent_timestamp),
               TSPEC_TO_DOUBLE(req.request_length),
               TSPEC_TO_DOUBLE(ts_recv),
               TSPEC_TO_DOUBLE(ts_compli));
    }

    // Closing the connection socket
    close(conn_socket);
	
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

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	close(sockfd);
	return EXIT_SUCCESS;

}
