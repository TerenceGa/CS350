/*******************************************************************************
 * Single-Threaded FIFO Image Server Implementation with Queue Limitation
 *
 * Description:
 *     A server implementation designed to process client
 *     requests for image processing in First In, First Out (FIFO)
 *     order. The server binds to the specified port number provided as
 *     a parameter upon launch. It launches a secondary thread to
 *     process incoming requests and allows specifying a maximum queue
 *     size.
 *
 * Usage:
 *     <build directory>/server -q <queue_size> -w <workers> -p <policy> <port_number>
 *
 * Parameters:
 *     port_number - The port number to bind the server to.
 *     queue_size  - The maximum number of queued requests.
 *     workers     - The number of parallel threads to process requests.
 *     policy      - The queue policy to use for request dispatching.
 *
 * Author:
 *     Renato Mancuso
 *
 * Affiliation:
 *     Boston University
 *
 * Creation Date:
 *     October 31, 2023
 *
 * Notes:
 *     Ensure to have proper permissions and available port before running the
 *     server. The server relies on a FIFO mechanism to handle requests, thus
 *     guaranteeing the order of processing. If the queue is full at the time a
 *     new request is received, the request is rejected with a negative acknowledgment.
 *
 *******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>

/* Headers for wait functions */
#include <sys/types.h>
#include <sys/wait.h>

/* Headers for semaphores */
#include <semaphore.h>

/* Include shared structures and functions for client-server communication */
#include "common.h"

/* Constants */
#define BACKLOG_COUNT 100
#define USAGE_STRING                                               \
    "Missing parameter. Exiting.\n"                                \
    "Usage: %s -q <queue size> "                                   \
    "-w <workers: 1> "                                             \
    "-p <policy: FIFO> "                                           \
    "<port_number>\n"

/* Stack size for worker threads (4KB) */
#define STACK_SIZE (4096)

/* Semaphore for synchronized printing. DO NOT MODIFY */
sem_t *print_mutex;

/* Macro for synchronized printing in multi-threaded environments */
#define sync_printf(...)                                          \
    do {                                                          \
        sem_wait(print_mutex);                                    \
        printf(__VA_ARGS__);                                      \
        sem_post(print_mutex);                                    \
    } while (0)

/* Semaphores for queue protection. DO NOT MODIFY */
sem_t *queue_mutex;    /* Mutex for accessing the queue */
sem_t *queue_notify;   /* Semaphore to notify worker threads of new requests */

/* Additional semaphores for registration and socket operations */
sem_t *registration_mutex; /* Mutex for registering new images */
sem_t *socket_mutex;       /* Mutex for socket operations */

/* Dynamic array to store registered images and its current count */
struct img_object **registered_images = NULL;
uint64_t total_images = 0;

/* Structure to hold metadata for each request */
struct request_meta {
    struct request request;             /* The actual client request */
    struct timespec receipt_timestamp;  /* Timestamp when the request was received */
    struct timespec start_timestamp;    /* Timestamp when processing started */
    struct timespec completion_timestamp; /* Timestamp when processing completed */
};

/* Enumeration for queue policies */
enum queue_policy {
    QUEUE_FIFO,  /* First-In, First-Out */
    QUEUE_SJN    /* Shortest Job Next (Not Implemented) */
};

/* Structure representing the request queue */
struct queue {
    size_t write_pos;                  /* Position to write the next request */
    size_t read_pos;                   /* Position to read the next request */
    size_t max_size;                   /* Maximum size of the queue */
    size_t available;                  /* Available slots in the queue */
    enum queue_policy policy;          /* Policy for dispatching requests */
    struct request_meta *requests;     /* Array of queued requests */
};

/* Structure for server configuration parameters */
struct connection_params {
    size_t queue_size;                 /* Maximum number of queued requests */
    size_t workers;                    /* Number of worker threads */
    enum queue_policy queue_policy;    /* Policy for dispatching requests */
};

/* Structure for passing parameters to worker threads */
struct worker_params {
    int conn_socket;                   /* Socket descriptor for client connection */
    int worker_done;                   /* Flag to indicate if the worker should terminate */
    struct queue *the_queue;           /* Pointer to the shared request queue */
    int worker_id;                     /* Identifier for the worker thread */
};

/* Enumeration for worker thread commands */
enum worker_command {
    WORKERS_START,    /* Command to start worker threads */
    WORKERS_STOP      /* Command to stop worker threads */
};

/* Structure representing an image object */
struct img_object {
    struct image *img;        /* Pointer to the image data */
    sem_t *img_mutex;         /* Mutex to protect image access */
    sem_t *op_semaphore;      /* Semaphore to enforce operation order */
    int assign_q;             /* Assigned position in the operation queue */
    int q_next;               /* Next expected operation position */
};

/**
 * Initializes the request queue with the specified size and policy.
 *
 * @param the_queue Pointer to the queue structure to initialize.
 * @param queue_size Maximum number of requests the queue can hold.
 * @param policy Queue dispatch policy (e.g., FIFO).
 */
void queue_init(struct queue *the_queue, size_t queue_size, enum queue_policy policy)
{
    the_queue->rd_pos = 0;
    the_queue->wr_pos = 0;
    the_queue->max_size = queue_size;
    the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta) * the_queue->max_size);
    the_queue->available = queue_size;
    the_queue->policy = policy;
}

/**
 * Adds a new request to the shared queue.
 *
 * @param to_add The request metadata to add to the queue.
 * @param the_queue Pointer to the queue structure.
 * @return 0 on success, 1 if the queue is full.
 */
int add_to_queue(struct request_meta to_add, struct queue *the_queue)
{
    int is_full = 0;

    /* Acquire queue access mutex */
    sem_wait(queue_mutex);

    /* Check if the queue is full */
    if (the_queue->available == 0) {
        is_full = 1;
    } else {
        /* Add the new request to the queue */
        the_queue->requests[the_queue->wr_pos] = to_add;
        the_queue->wr_pos = (the_queue->wr_pos + 1) % the_queue->max_size;
        the_queue->available--;

        /* Notify a worker thread that a new request is available */
        sem_post(queue_notify);
    }

    /* Release queue access mutex */
    sem_post(queue_mutex);

    return is_full;
}

/**
 * Retrieves and removes the next request from the shared queue.
 *
 * @param the_queue Pointer to the queue structure.
 * @return The next request metadata from the queue.
 */
struct request_meta get_from_queue(struct queue *the_queue)
{
    struct request_meta retval;

    /* Wait until a request is available */
    sem_wait(queue_notify);

    /* Acquire queue access mutex */
    sem_wait(queue_mutex);

    /* Retrieve the next request from the queue */
    retval = the_queue->requests[the_queue->rd_pos];
    the_queue->rd_pos = (the_queue->rd_pos + 1) % the_queue->max_size;
    the_queue->available++;

    /* Release queue access mutex */
    sem_post(queue_mutex);

    return retval;
}

/**
 * Displays the current status of the request queue.
 *
 * @param the_queue Pointer to the queue structure.
 */
void dump_queue_status(struct queue *the_queue)
{
    size_t i, j;

    /* Acquire queue access mutex */
    sem_wait(queue_mutex);

    /* Acquire print mutex for synchronized output */
    sem_wait(print_mutex);
    printf("Queue Status: [");

    /* Iterate through the queue and print each request ID */
    for (i = the_queue->rd_pos, j = 0; j < (the_queue->max_size - the_queue->available);
         i = (i + 1) % the_queue->max_size, ++j)
    {
        printf("Req%ld%s", the_queue->requests[i].request.req_id,
               ((j + 1 != the_queue->max_size - the_queue->available) ? "," : ""));
    }

    printf("]\n");
    /* Release print mutex */
    sem_post(print_mutex);

    /* Release queue access mutex */
    sem_post(queue_mutex);
}

/**
 * Registers a new image by receiving it from the client and storing it.
 *
 * @param conn_socket The socket descriptor for the client connection.
 * @param req Pointer to the client's request structure.
 */
void register_new_image(int conn_socket, struct request *req)
{
    /* Acquire registration mutex */
    sem_wait(registration_mutex);

    /* Assign a new image ID */
    uint64_t new_image_id = total_images;
    total_images++;

    /* Reallocate memory to accommodate the new image */
    registered_images = realloc(registered_images, sizeof(struct img_object *) * total_images);

    /* Receive the new image from the client */
    struct image *received_image = recvImage(conn_socket);

    /* Initialize the new image object */
    registered_images[new_image_id] = malloc(sizeof(struct img_object));
    registered_images[new_image_id]->img = received_image;

    /* Initialize mutex for the image */
    registered_images[new_image_id]->img_mutex = (sem_t *)malloc(sizeof(sem_t));
    if (sem_init(registered_images[new_image_id]->img_mutex, 0, 1) < 0) {
        perror("Error: Unable to initialize image mutex");
        exit(EXIT_FAILURE);
    }

    /* Initialize semaphore for operation ordering */
    registered_images[new_image_id]->op_semaphore = (sem_t *)malloc(sizeof(sem_t));
    if (sem_init(registered_images[new_image_id]->op_semaphore, 0, 1) < 0) {
        perror("Error: Unable to initialize operation semaphore");
        exit(EXIT_FAILURE);
    }

    /* Initialize operation queue positions */
    registered_images[new_image_id]->assign_q = 0;
    registered_images[new_image_id]->q_next = 0;

    /* Release registration mutex */
    sem_post(registration_mutex);

    /* Prepare and send the response to the client */
    struct response resp;
    resp.req_id = req->req_id;
    resp.img_id = new_image_id;
    resp.ack = RESP_COMPLETED;

    /* Acquire socket access mutex before sending */
    sem_wait(socket_mutex);
    send(conn_socket, &resp, sizeof(struct response), 0);
    sem_post(socket_mutex);

    /* Log the registration */
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    sem_wait(print_mutex);
    printf("Registered Image ID: %ld at %lf seconds.\n", new_image_id, 
           (double)current_time.tv_sec + (double)current_time.tv_nsec / 1e9);
    sem_post(print_mutex);
}

/**
 * The main function executed by each worker thread.
 *
 * @param arg Pointer to worker_params structure.
 * @return NULL upon completion.
 */
void *worker_main(void *arg)
{
    struct timespec now;
    struct worker_params *params = (struct worker_params *)arg;

    /* Log that the worker thread has started */
    clock_gettime(CLOCK_MONOTONIC, &now);
    sync_printf("[Worker %d] Thread started at %lf seconds.\n", params->worker_id, 
               (double)now.tv_sec + (double)now.tv_nsec / 1e9);

    /* Continuously process requests until termination is signaled */
    while (!params->worker_done) {
        struct request_meta req;
        struct response resp;
        struct image *img = NULL;
        uint64_t img_id;
        int queue_position;

        /* Dequeue the next request from the queue */
        req = get_from_queue(params->the_queue);

        /* Check if termination has been signaled after dequeueing */
        if (params->worker_done)
            break;

        /* Record the start time of processing */
        clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);

        /* Retrieve the image ID from the request */
        img_id = req.request.img_id;

        /* Acquire operation semaphore to assign a queue position */
        sem_wait(registered_images[img_id]->op_semaphore);
        queue_position = registered_images[img_id]->assign_q++;
        sem_post(registered_images[img_id]->op_semaphore);

        printf("Ticket: %d, Completed: %d\n", queue_position, registered_images[img_id]->q_next);

        /* Enforce operation order based on assigned queue position */
        while (1){
            sem_wait(registered_images[img_id]->op_semaphore);
            if(queue_position == registered_images[img_id]->q_next){
                /* It's this thread's turn to process the request */
                sem_post(registered_images[img_id]->op_semaphore);
                break;
            }
            sem_post(registered_images[img_id]->op_semaphore);
            /* Busy-waiting can be optimized using condition variables */
        }

        /* Acquire image mutex before processing */
        sem_wait(registered_images[img_id]->img_mutex);
        img = registered_images[img_id]->img; /* Access the image */

        /* Ensure the image exists */
        assert(img != NULL);

        /* Perform the requested image operation */
        switch (req.request.img_op) {
            case IMG_ROT90CLKW:
                img = rotate90Clockwise(img, NULL);
                break;
            case IMG_BLUR:
                img = blurImage(img, NULL);
                break;
            case IMG_SHARPEN:
                img = sharpenImage(img, NULL);
                break;
            case IMG_VERTEDGES:
                img = detectVerticalEdges(img, NULL);
                break;
            case IMG_HORIZEDGES:
                img = detectHorizontalEdges(img, NULL);
                break;
            default:
                /* Unknown operation */
                break;
        }

        /* If the operation modifies the image, handle accordingly */
        if (req.request.img_op != IMG_RETRIEVE) {
            if (req.request.overwrite) {
                /* Overwrite the existing image */
                deleteImage(registered_images[img_id]->img);
                registered_images[img_id]->img = img;
            } else {
                /* Create a new image entry */
                sem_wait(registration_mutex);
                img_id = total_images++;
                registered_images = realloc(registered_images, sizeof(struct img_object *) * total_images);

                /* Initialize the new image object */
                registered_images[img_id] = malloc(sizeof(struct img_object));
                registered_images[img_id]->img = img;

                /* Initialize mutex for the new image */
                registered_images[img_id]->img_mutex = (sem_t *)malloc(sizeof(sem_t));
                if (sem_init(registered_images[img_id]->img_mutex, 0, 1) < 0) {
                    perror("Error: Unable to initialize image mutex");
                    exit(EXIT_FAILURE);
                }

                /* Initialize semaphore for operation ordering */
                registered_images[img_id]->op_semaphore = (sem_t *)malloc(sizeof(sem_t));
                if (sem_init(registered_images[img_id]->op_semaphore, 0, 1) < 0) {
                    perror("Error: Unable to initialize operation semaphore");
                    exit(EXIT_FAILURE);
                }

                /* Initialize operation queue positions */
                registered_images[img_id]->assign_q = 0;
                registered_images[img_id]->q_next = 0;

                /* Release registration mutex */
                sem_post(registration_mutex);
            }
        }

        /* Record the completion time of processing */
        clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

        /* Prepare the response to the client */
        resp.req_id = req.request.req_id;
        resp.ack = RESP_COMPLETED;
        resp.img_id = (req.request.img_op == IMG_RETRIEVE) ? 
                      req.request.img_id : 
                      (req.request.overwrite ? 
                       req.request.img_id : 
                       total_images - 1);

        /* Send the response to the client */
        sem_wait(socket_mutex);
        send(params->conn_socket, &resp, sizeof(struct response), 0);

        /* If the operation is IMG_RETRIEVE, send the image data */
        if (req.request.img_op == IMG_RETRIEVE) {
            uint8_t error_flag = sendImage(img, params->conn_socket);
            if(error_flag) {
                perror("Error: Unable to send image payload to client.");
            }
        }
        sem_post(socket_mutex);

        /* Update the next expected queue position */
        sem_wait(registered_images[img_id]->op_semaphore);
        registered_images[img_id]->q_next++;
        sem_post(registered_images[img_id]->op_semaphore);

        /* Release image mutex */
        sem_post(registered_images[img_id]->img_mutex);

        /* Log the request processing details */
        printf("Worker %d processed Request %ld: Operation %s on Image %ld -> Image %ld\n",
               params->worker_id, req.request.req_id,
               opcode_to_string(req.request.img_op),
               req.request.img_id,
               resp.img_id);
        dump_queue_status(params->the_queue);
    }

    /**
     * Manages the creation and termination of worker threads.
     *
     * @param cmd Command indicating whether to start or stop workers.
     * @param worker_count Number of worker threads to manage.
     * @param common_params Shared parameters for worker threads.
     * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
     */
    int control_workers(enum worker_command cmd, size_t worker_count,
                        struct worker_params *common_params)
    {
        /* Static variables to keep track of worker threads and their parameters */
        static pthread_t *worker_pthreads = NULL;
        static struct worker_params **worker_params = NULL;
        static int *worker_ids = NULL;

        /* Start all the workers */
        if (cmd == WORKERS_START) {
            size_t i;

            /* Allocate memory for thread handles and parameters */
            worker_pthreads = (pthread_t *)malloc(worker_count * sizeof(pthread_t));
            worker_params = (struct worker_params **)malloc(worker_count * sizeof(struct worker_params *));
            worker_ids = (int *)malloc(worker_count * sizeof(int));

            if (!worker_pthreads || !worker_params || !worker_ids) {
                perror("Error: Unable to allocate memory for worker threads.");
                return EXIT_FAILURE;
            }

            /* Initialize and create worker threads */
            for (i = 0; i < worker_count; ++i) {
                worker_ids[i] = -1;

                worker_params[i] = (struct worker_params *)malloc(sizeof(struct worker_params));
                if (!worker_params[i]) {
                    perror("Error: Unable to allocate memory for worker parameters.");
                    return EXIT_FAILURE;
                }

                worker_params[i]->conn_socket = common_params->conn_socket;
                worker_params[i]->the_queue = common_params->the_queue;
                worker_params[i]->worker_done = 0;
                worker_params[i]->worker_id = i;

                /* Create the worker thread */
                worker_ids[i] = pthread_create(&worker_pthreads[i], NULL, worker_main, worker_params[i]);

                if (worker_ids[i] != 0) {
                    perror("Error: Unable to create worker thread.");
                    return EXIT_FAILURE;
                } else {
                    printf("INFO: Worker thread %ld started.\n", i);
                }
            }
        }
        /* Stop all the workers */
        else if (cmd == WORKERS_STOP) {
            size_t i;

            /* Ensure that worker threads have been started */
            if (!worker_pthreads || !worker_params || !worker_ids) {
                return EXIT_FAILURE;
            }

            /* Signal all worker threads to terminate */
            for (i = 0; i < worker_count; i++) {
                if (worker_ids[i] != 0) {
                    continue;
                }

                worker_params[i]->worker_done = 1;
            }

            /* Notify all workers in case they are waiting on the semaphore */
            for (i = 0; i < worker_count; i++) {
                if (worker_ids[i] != 0) {
                    continue;
                }
                sem_post(queue_notify);
            }

            /* Wait for all worker threads to finish */
            for (i = 0; i < worker_count; i++) {
                if (worker_ids[i] != 0) {
                    continue;
                }
                pthread_join(worker_pthreads[i], NULL);
                printf("INFO: Worker thread %ld terminated.\n", i);
                free(worker_params[i]);
            }

            /* Free allocated memory for thread handles and parameters */
            free(worker_pthreads);
            free(worker_params);
            free(worker_ids);
            worker_pthreads = NULL;
            worker_params = NULL;
            worker_ids = NULL;
        }
        else {
            fprintf(stderr, "Error: Invalid worker command.\n");
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    /**
     * Handles the client connection by initializing the request queue, spawning
     * worker threads, and managing incoming requests.
     *
     * @param conn_socket The socket descriptor for the client connection.
     * @param conn_params The server configuration parameters.
     */
    void handle_connection(int conn_socket, struct connection_params conn_params)
    {
        struct request_meta *req;
        struct queue *the_queue;
        size_t bytes_received;

        /* Initialize the request queue */
        the_queue = (struct queue *)malloc(sizeof(struct queue));
        queue_init(the_queue, conn_params.queue_size, conn_params.queue_policy);

        /* Set up worker parameters */
        struct worker_params common_worker_params;
        int res;

        common_worker_params.conn_socket = conn_socket;
        common_worker_params.the_queue = the_queue;
        common_worker_params.worker_done = 0;
        common_worker_params.worker_id = -1; /* Not used in common_params */

        /* Start worker threads */
        res = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

        /* Initialize additional semaphores */
        registration_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (sem_init(registration_mutex, 0, 1) < 0) {
            perror("Error: Unable to initialize registration mutex");
            exit(EXIT_FAILURE);
        }

        /* Check if workers started successfully */
        if (res != EXIT_SUCCESS) {
            free(the_queue);
            /* Stop any worker that was successfully started */
            control_workers(WORKERS_STOP, conn_params.workers, NULL);
            return;
        }

        /* Allocate memory for incoming requests */
        req = (struct request_meta *)malloc(sizeof(struct request_meta));

        /* Continuously receive requests from the client */
        do {
            bytes_received = recv(conn_socket, &req->request, sizeof(struct request), 0);
            clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

            /* If bytes_received > 0, process the request */
            if (bytes_received > 0) {
                /* Handle image registration immediately */
                if(req->request.img_op == IMG_REGISTER) {
                    clock_gettime(CLOCK_MONOTONIC, &req->start_timestamp);
                    register_new_image(conn_socket, &req->request);
                    clock_gettime(CLOCK_MONOTONIC, &req->completion_timestamp);

                    /* Log the registration */
                    sem_wait(print_mutex);
                    printf("Processed Registration: Request ID %ld, Image ID %ld\n",
                           req->request.req_id, total_images - 1);
                    sem_post(print_mutex);
                    continue;
                }

                /* Enqueue the request for processing */
                int enqueue_status = add_to_queue(*req, the_queue);

                if (enqueue_status) {
                    /* Queue is full; send a rejection response */
                    struct response resp;
                    resp.req_id = req->request.req_id;
                    resp.ack = RESP_REJECTED;

                    /* Acquire socket access mutex before sending */
                    sem_wait(socket_mutex);
                    send(conn_socket, &resp, sizeof(struct response), 0);
                    sem_post(socket_mutex);

                    /* Log the rejection */
                    sem_wait(print_mutex);
                    printf("Rejected Request ID %ld: Queue Full\n", req->request.req_id);
                    sem_post(print_mutex);
                }
            }
        } while (bytes_received > 0);

        /* Cleanup after client disconnects */
        control_workers(WORKERS_STOP, conn_params.workers, NULL);
        free(req);
        free(the_queue->requests);
        free(the_queue);
        close(conn_socket);
        printf("INFO: Client connection closed.\n");
    }

    /**
     * Cleans up all allocated resources and destroys semaphores.
     */
    void cleanup_resources()
    {
        /* Destroy all semaphores */
        sem_destroy(print_mutex);
        sem_destroy(queue_mutex);
        sem_destroy(queue_notify);
        sem_destroy(registration_mutex);
        sem_destroy(socket_mutex);

        /* Free semaphore memory */
        free(print_mutex);
        free(queue_mutex);
        free(queue_notify);
        free(registration_mutex);
        free(socket_mutex);

        /* Destroy and free all registered images */
        for (uint64_t i = 0; i < total_images; i++) {
            if (registered_images[i]) {
                sem_destroy(registered_images[i]->img_mutex);
                sem_destroy(registered_images[i]->op_semaphore);
                deleteImage(registered_images[i]->img);
                free(registered_images[i]);
            }
        }
        free(registered_images);
    }

    /* Utility Functions (Assumed to be Defined Elsewhere) */

    /**
     * Receives an image from the client over the specified socket.
     *
     * @param socket The socket descriptor to receive the image from.
     * @return Pointer to the received Image structure, or NULL on failure.
     */
    struct image* recvImage(int socket) {
        /* Implementation assumed to be provided in "common.h" */
        // Placeholder implementation
        struct image *img = malloc(sizeof(struct image));
        if (!img) return NULL;
        // Receive image data and populate 'img'
        return img;
    }

    /**
     * Sends an image to the client over the specified socket.
     *
     * @param img Pointer to the Image structure to send.
     * @param socket The socket descriptor to send the image through.
     * @return 0 on success, non-zero on failure.
     */
    int sendImage(struct image *img, int socket) {
        /* Implementation assumed to be provided in "common.h" */
        // Placeholder implementation
        if (!img) return -1;
        // Send image data over the socket
        return 0;
    }

    /**
     * Deletes an image and frees associated resources.
     *
     * @param img Pointer to the Image structure to delete.
     */
    void deleteImage(struct image *img) {
        if (img) {
            /* Implementation to free image data */
            free(img);
        }
    }

    /**
     * Converts an image operation code to its string representation.
     *
     * @param opcode The image operation code.
     * @return String representing the operation.
     */
    const char* opcode_to_string(int opcode) {
        switch (opcode) {
            case IMG_ROT90CLKW:
                return "Rotate90Clockwise";
            case IMG_BLUR:
                return "Blur";
            case IMG_SHARPEN:
                return "Sharpen";
            case IMG_VERTEDGES:
                return "DetectVerticalEdges";
            case IMG_HORIZEDGES:
                return "DetectHorizontalEdges";
            case IMG_REGISTER:
                return "RegisterImage";
            case IMG_RETRIEVE:
                return "RetrieveImage";
            default:
                return "UnknownOperation";
        }
    }

    /* Main function to handle client connection */
    int main(int argc, char **argv) {
        int server_socket, client_socket, opt, status;
        in_port_t port;
        struct sockaddr_in server_addr, client_addr;
        socklen_t client_addr_len;
        struct connection_params server_config = {0, 1, QUEUE_FIFO}; /* Default configuration */

        /* Parse Command-Line Arguments */
        while ((opt = getopt(argc, argv, "q:w:p:")) != -1) {
            switch (opt) {
                case 'q':
                    server_config.queue_size = strtol(optarg, NULL, 10);
                    printf("INFO: Queue size set to %ld\n", server_config.queue_size);
                    break;
                case 'w':
                    server_config.workers = strtol(optarg, NULL, 10);
                    printf("INFO: Number of worker threads set to %ld\n", server_config.workers);
                    break;
                case 'p':
                    if (strcmp(optarg, "FIFO") == 0) {
                        server_config.queue_policy = QUEUE_FIFO;
                    } else {
                        fprintf(stderr, "Invalid queue policy.\n" USAGE_STRING, argv[0]);
                        return EXIT_FAILURE;
                    }
                    printf("INFO: Queue policy set to %s\n", optarg);
                    break;
                default:
                    fprintf(stderr, USAGE_STRING, argv[0]);
                    return EXIT_FAILURE;
            }
        }

        /* Validate Required Parameters */
        if (server_config.queue_size == 0) {
            fprintf(stderr, "Queue size must be greater than 0.\n" USAGE_STRING, argv[0]);
            return EXIT_FAILURE;
        }

        if (optind < argc) {
            port = strtol(argv[optind], NULL, 10);
            printf("INFO: Server will listen on port %d\n", port);
        } else {
            fprintf(stderr, USAGE_STRING, argv[0]);
            return EXIT_FAILURE;
        }

        /* Create Server Socket */
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            perror("Error: Unable to create socket");
            return EXIT_FAILURE;
        }

        /* Set Socket Options to Reuse Address */
        int reuse_opt = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(reuse_opt)) < 0) {
            perror("Error: Unable to set socket options");
            close(server_socket);
            return EXIT_FAILURE;
        }

        /* Configure Server Address Structure */
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        /* Bind Socket to Address */
        if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Error: Unable to bind socket");
            close(server_socket);
            return EXIT_FAILURE;
        }

        /* Start Listening for Incoming Connections */
        if (listen(server_socket, BACKLOG_COUNT) < 0) {
            perror("Error: Unable to listen on socket");
            close(server_socket);
            return EXIT_FAILURE;
        }

        printf("INFO: Server is listening on port %d...\n", port);

        /* Initialize Global Semaphores */
        print_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (!print_mutex) {
            perror("Error: Unable to allocate memory for print_mutex");
            close(server_socket);
            return EXIT_FAILURE;
        }
        if (sem_init(print_mutex, 0, 1) < 0) {
            perror("Error: Unable to initialize print_mutex");
            free(print_mutex);
            close(server_socket);
            return EXIT_FAILURE;
        }

        /* Initialize queue protection semaphores */
        queue_mutex = (sem_t *)malloc(sizeof(sem_t));
        queue_notify = (sem_t *)malloc(sizeof(sem_t));
        if (!queue_mutex || !queue_notify) {
            perror("Error: Unable to allocate memory for queue semaphores");
            cleanup_resources();
            close(server_socket);
            return EXIT_FAILURE;
        }
        if (sem_init(queue_mutex, 0, 1) < 0) {
            perror("Error: Unable to initialize queue_mutex");
            cleanup_resources();
            close(server_socket);
            return EXIT_FAILURE;
        }
        if (sem_init(queue_notify, 0, 0) < 0) {
            perror("Error: Unable to initialize queue_notify");
            cleanup_resources();
            close(server_socket);
            return EXIT_FAILURE;
        }

        /* Initialize additional semaphores */
        socket_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (!socket_mutex) {
            perror("Error: Unable to allocate memory for socket_mutex");
            cleanup_resources();
            close(server_socket);
            return EXIT_FAILURE;
        }
        if (sem_init(socket_mutex, 0, 1) < 0) {
            perror("Error: Unable to initialize socket_mutex");
            cleanup_resources();
            close(server_socket);
            return EXIT_FAILURE;
        }

        /* Accept a client connection */
        printf("INFO: Waiting for incoming connection...\n");
        client_addr_len = sizeof(struct sockaddr_in);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_socket == -1) {
            perror("Error: Unable to accept connections");
            cleanup_resources();
            close(server_socket);
            return EXIT_FAILURE;
        }

        printf("INFO: Accepted new client connection.\n");

        /* Handle the client connection */
        handle_connection(client_socket, server_config);

        /* Cleanup resources */
        cleanup_resources();
        close(server_socket);
        return EXIT_SUCCESS;
    }
