/*******************************************************************************
 * Multi-Threaded FIFO Image Server Implementation with Queue Limitation
 *
 * Description:
 *     This server handles client requests for image processing in a First-In,
 *     First-Out (FIFO) manner. It listens on a specified port and employs a
 *     thread pool to process incoming requests concurrently. The server
 *     maintains a request queue with a configurable maximum size and adheres
 *     to a defined queue policy for request handling.
 *
 * Usage:
 *     <build_directory>/server -q <queue_size> -w <worker_count> -p <policy> <port_number>
 *
 * Parameters:
 *     port_number  - The port number on which the server listens.
 *     queue_size   - The maximum number of requests that can be queued.
 *     worker_count - The number of worker threads processing the requests.
 *     policy       - The queue policy for dispatching requests (e.g., FIFO).
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
 *     - Ensure appropriate permissions and that the chosen port is available.
 *     - The server employs a FIFO mechanism to process requests, ensuring
 *       they are handled in the order received.
 *     - If the request queue is full, new incoming requests are rejected with
 *       a negative acknowledgment.
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
#include <string.h>

/* Headers for socket programming and synchronization */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <semaphore.h>

/* Include shared structures and functions for client-server communication */
#include "common.h"

/* Constants */
#define MAX_BACKLOG 100
#define USAGE_MESSAGE                                               \
    "Missing parameter. Exiting.\n"                                \
    "Usage: %s -q <queue_size> -w <worker_count> -p <policy> <port_number>\n"

/* Stack size for worker threads (4KB) */
#define WORKER_STACK_SIZE 4096

/* Enumeration for queue policies */
typedef enum {
    POLICY_FIFO,
    POLICY_SJN  /* Shortest Job Next (Not Implemented) */
} QueuePolicy;

/* Structure to hold metadata for each request */
typedef struct {
    Request request;
    struct timespec receipt_time;
    struct timespec start_time;
    struct timespec completion_time;
} RequestMeta;

/* Structure representing an image object */
typedef struct {
    Image *img;
    sem_t img_mutex;          /* Mutex to protect image access */
    sem_t operation_sem;      /* Semaphore to enforce operation order */
    int assign_queue_pos;     /* Position in the operation queue */
    int next_queue_pos;       /* Next expected operation position */
} ImageObject;

/* Structure for the request queue */
typedef struct {
    size_t write_pos;
    size_t read_pos;
    size_t capacity;
    size_t available_slots;
    QueuePolicy policy;
    RequestMeta *requests;
} RequestQueue;

/* Structure for passing parameters to worker threads */
typedef struct {
    int client_socket;
    int should_terminate;
    RequestQueue *request_queue;
    int worker_id;
} WorkerParams;

/* Structure for server configuration */
typedef struct {
    size_t queue_capacity;
    size_t worker_count;
    QueuePolicy queue_policy;
} ServerConfig;

/* Global Variables for Synchronization */
sem_t *print_mutex;          /* Mutex for synchronized printing */
sem_t *queue_access_mutex;   /* Mutex for accessing the request queue */
sem_t *queue_notify_sem;     /* Semaphore to notify workers of new requests */
sem_t *registration_mutex;   /* Mutex for registering new images */
sem_t *socket_access_mutex;  /* Mutex for socket operations */

/* Dynamic array to store registered images */
ImageObject **registered_images = NULL;
uint64_t total_images = 0;

/* Function Prototypes */
void initialize_request_queue(RequestQueue *queue, size_t capacity, QueuePolicy policy);
int enqueue_request(RequestMeta new_request, RequestQueue *queue);
RequestMeta dequeue_request(RequestQueue *queue);
void display_queue_status(RequestQueue *queue);
void register_image(int client_socket, Request *req);
void *worker_thread_main(void *arg);
int manage_worker_threads(int command, size_t worker_count, WorkerParams *common_params);
void handle_client_connection(int client_socket, ServerConfig config);
void cleanup_resources();

/* Enumeration for worker thread commands */
typedef enum {
    WORKER_START,
    WORKER_STOP
} WorkerCommand;

/* Main Function */
int main(int argc, char **argv) {
    int server_socket, client_socket, opt, status;
    in_port_t port;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    ServerConfig server_config = {0, 1, POLICY_FIFO}; /* Default configuration */

    /* Parse Command-Line Arguments */
    while ((opt = getopt(argc, argv, "q:w:p:")) != -1) {
        switch (opt) {
            case 'q':
                server_config.queue_capacity = strtoul(optarg, NULL, 10);
                printf("INFO: Queue size set to %lu\n", server_config.queue_capacity);
                break;
            case 'w':
                server_config.worker_count = strtoul(optarg, NULL, 10);
                printf("INFO: Number of worker threads set to %lu\n", server_config.worker_count);
                break;
            case 'p':
                if (strcmp(optarg, "FIFO") == 0) {
                    server_config.queue_policy = POLICY_FIFO;
                } else {
                    fprintf(stderr, "Invalid queue policy.\n" USAGE_MESSAGE, argv[0]);
                    return EXIT_FAILURE;
                }
                printf("INFO: Queue policy set to %s\n", optarg);
                break;
            default:
                fprintf(stderr, USAGE_MESSAGE, argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* Validate Required Parameters */
    if (server_config.queue_capacity == 0) {
        fprintf(stderr, "Queue size must be greater than 0.\n" USAGE_MESSAGE, argv[0]);
        return EXIT_FAILURE;
    }

    if (optind < argc) {
        port = strtoul(argv[optind], NULL, 10);
        printf("INFO: Server will listen on port %d\n", port);
    } else {
        fprintf(stderr, USAGE_MESSAGE, argv[0]);
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
    if (listen(server_socket, MAX_BACKLOG) < 0) {
        perror("Error: Unable to listen on socket");
        close(server_socket);
        return EXIT_FAILURE;
    }

    printf("INFO: Server is listening on port %d...\n", port);

    /* Initialize Global Semaphores */
    print_mutex = malloc(sizeof(sem_t));
    if (!print_mutex) {
        perror("Error: Unable to allocate memory for print_mutex");
        close(server_socket);
        return EXIT_FAILURE;
    }
    sem_init(print_mutex, 0, 1);

    queue_access_mutex = malloc(sizeof(sem_t));
    if (!queue_access_mutex) {
        perror("Error: Unable to allocate memory for queue_access_mutex");
        free(print_mutex);
        close(server_socket);
        return EXIT_FAILURE;
    }
    sem_init(queue_access_mutex, 0, 1);

    queue_notify_sem = malloc(sizeof(sem_t));
    if (!queue_notify_sem) {
        perror("Error: Unable to allocate memory for queue_notify_sem");
        free(print_mutex);
        free(queue_access_mutex);
        close(server_socket);
        return EXIT_FAILURE;
    }
    sem_init(queue_notify_sem, 0, 0);

    registration_mutex = malloc(sizeof(sem_t));
    if (!registration_mutex) {
        perror("Error: Unable to allocate memory for registration_mutex");
        free(print_mutex);
        free(queue_access_mutex);
        free(queue_notify_sem);
        close(server_socket);
        return EXIT_FAILURE;
    }
    sem_init(registration_mutex, 0, 1);

    socket_access_mutex = malloc(sizeof(sem_t));
    if (!socket_access_mutex) {
        perror("Error: Unable to allocate memory for socket_access_mutex");
        free(print_mutex);
        free(queue_access_mutex);
        free(queue_notify_sem);
        free(registration_mutex);
        close(server_socket);
        return EXIT_FAILURE;
    }
    sem_init(socket_access_mutex, 0, 1);

    /* Main Loop: Accept and Handle Client Connections */
    while (1) {
        client_addr_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Error: Unable to accept client connection");
            continue; /* Continue accepting new connections */
        }

        printf("INFO: Accepted new client connection.\n");

        /* Handle the Client Connection */
        handle_client_connection(client_socket, server_config);
    }

    /* Cleanup Resources (This point is never reached in the current implementation) */
    cleanup_resources();
    close(server_socket);
    return EXIT_SUCCESS;
}

/**
 * Initializes the request queue with the specified capacity and policy.
 *
 * @param queue Pointer to the RequestQueue structure.
 * @param capacity Maximum number of requests the queue can hold.
 * @param policy Queue dispatch policy (e.g., FIFO).
 */
void initialize_request_queue(RequestQueue *queue, size_t capacity, QueuePolicy policy) {
    queue->write_pos = 0;
    queue->read_pos = 0;
    queue->capacity = capacity;
    queue->available_slots = capacity;
    queue->policy = policy;
    queue->requests = malloc(sizeof(RequestMeta) * capacity);
    if (!queue->requests) {
        perror("Error: Unable to allocate memory for request queue");
        exit(EXIT_FAILURE);
    }
}

/**
 * Adds a new request to the request queue.
 *
 * @param new_request The RequestMeta to be added.
 * @param queue Pointer to the RequestQueue.
 * @return 0 on success, 1 if the queue is full.
 */
int enqueue_request(RequestMeta new_request, RequestQueue *queue) {
    int is_full = 0;

    /* Acquire Queue Access Mutex */
    sem_wait(queue_access_mutex);

    /* Check if the queue has available slots */
    if (queue->available_slots == 0) {
        is_full = 1;
    } else {
        /* Add the new request to the queue */
        queue->requests[queue->write_pos] = new_request;
        queue->write_pos = (queue->write_pos + 1) % queue->capacity;
        queue->available_slots--;

        /* Notify a worker that a new request is available */
        sem_post(queue_notify_sem);
    }

    /* Release Queue Access Mutex */
    sem_post(queue_access_mutex);

    return is_full;
}

/**
 * Retrieves and removes the next request from the request queue.
 *
 * @param queue Pointer to the RequestQueue.
 * @return The next RequestMeta from the queue.
 */
RequestMeta dequeue_request(RequestQueue *queue) {
    RequestMeta request;

    /* Wait until a request is available */
    sem_wait(queue_notify_sem);

    /* Acquire Queue Access Mutex */
    sem_wait(queue_access_mutex);

    /* Retrieve the request from the queue */
    request = queue->requests[queue->read_pos];
    queue->read_pos = (queue->read_pos + 1) % queue->capacity;
    queue->available_slots++;

    /* Release Queue Access Mutex */
    sem_post(queue_access_mutex);

    return request;
}

/**
 * Displays the current status of the request queue.
 *
 * @param queue Pointer to the RequestQueue.
 */
void display_queue_status(RequestQueue *queue) {
    size_t index, count;

    /* Acquire Queue Access Mutex */
    sem_wait(queue_access_mutex);

    /* Acquire Print Mutex for synchronized output */
    sem_wait(print_mutex);
    printf("Queue Status: [");

    /* Iterate through the queue and display each request ID */
    for (index = queue->read_pos, count = 0; count < (queue->capacity - queue->available_slots);
         index = (index + 1) % queue->capacity, count++) {
        printf("Req%ld%s", queue->requests[index].request.req_id,
               (count + 1 < (queue->capacity - queue->available_slots)) ? ", " : "");
    }

    printf("]\n");
    /* Release Print Mutex */
    sem_post(print_mutex);

    /* Release Queue Access Mutex */
    sem_post(queue_access_mutex);
}

/**
 * Registers a new image by receiving it from the client and storing it.
 *
 * @param client_socket The socket descriptor for the client connection.
 * @param req Pointer to the client's request structure.
 */
void register_image(int client_socket, Request *req) {
    /* Acquire Registration Mutex */
    sem_wait(registration_mutex);

    /* Assign a new image ID */
    uint64_t new_image_id = total_images;
    total_images++;

    /* Reallocate memory to accommodate the new image */
    registered_images = realloc(registered_images, sizeof(ImageObject *) * total_images);
    if (!registered_images) {
        perror("Error: Unable to allocate memory for new image");
        exit(EXIT_FAILURE);
    }

    /* Receive the new image from the client */
    Image *received_image = recvImage(client_socket);
    if (!received_image) {
        fprintf(stderr, "Error: Failed to receive image from client.\n");
        sem_post(registration_mutex);
        return;
    }

    /* Initialize the new ImageObject */
    registered_images[new_image_id] = malloc(sizeof(ImageObject));
    if (!registered_images[new_image_id]) {
        perror("Error: Unable to allocate memory for ImageObject");
        sem_post(registration_mutex);
        exit(EXIT_FAILURE);
    }

    registered_images[new_image_id]->img = received_image;
    sem_init(&registered_images[new_image_id]->img_mutex, 0, 1);
    sem_init(&registered_images[new_image_id]->operation_sem, 0, 1);
    registered_images[new_image_id]->assign_queue_pos = 0;
    registered_images[new_image_id]->next_queue_pos = 0;

    /* Release Registration Mutex */
    sem_post(registration_mutex);

    /* Prepare and send the response to the client */
    Response response;
    response.req_id = req->req_id;
    response.img_id = new_image_id;
    response.ack = RESP_COMPLETED;

    /* Acquire Socket Access Mutex before sending */
    sem_wait(socket_access_mutex);
    send(client_socket, &response, sizeof(Response), 0);
    sem_post(socket_access_mutex);

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
 * @param arg Pointer to WorkerParams structure.
 * @return NULL upon completion.
 */
void *worker_thread_main(void *arg) {
    WorkerParams *params = (WorkerParams *)arg;
    struct timespec current_time;

    /* Log that the worker thread is alive */
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    sem_wait(print_mutex);
    printf("[Worker %d] Thread started at %lf seconds.\n", params->worker_id, 
           (double)current_time.tv_sec + (double)current_time.tv_nsec / 1e9);
    sem_post(print_mutex);

    /* Continuously process requests until termination is signaled */
    while (!params->should_terminate) {
        /* Dequeue the next request */
        RequestMeta request_meta = dequeue_request(params->request_queue);

        /* Check if termination has been signaled */
        if (params->should_terminate) {
            break;
        }

        /* Log the start time of processing */
        clock_gettime(CLOCK_MONOTONIC, &request_meta.start_time);

        /* Retrieve the image ID from the request */
        uint64_t image_id = request_meta.request.img_id;

        /* Acquire Operation Semaphore to enforce operation order */
        sem_wait(&registered_images[image_id]->operation_sem);
        int assigned_pos = registered_images[image_id]->assign_queue_pos++;
        sem_post(&registered_images[image_id]->operation_sem);

        /* Enforce operation order based on assigned queue position */
        while (1) {
            sem_wait(&registered_images[image_id]->operation_sem);
            if (assigned_pos == registered_images[image_id]->next_queue_pos) {
                /* It's this thread's turn to process the request */
                sem_post(&registered_images[image_id]->operation_sem);
                break;
            }
            sem_post(&registered_images[image_id]->operation_sem);
            /* Busy-waiting can be optimized using condition variables */
        }

        /* Acquire Image Mutex before processing */
        sem_wait(&registered_images[image_id]->img_mutex);
        Image *image = registered_images[image_id]->img;

        /* Ensure the image exists */
        if (image == NULL) {
            sem_post(&registered_images[image_id]->img_mutex);
            continue; /* Skip processing if image is not found */
        }

        /* Perform the requested image operation */
        switch (request_meta.request.img_op) {
            case IMG_ROT90CLKW:
                image = rotate90Clockwise(image, NULL);
                break;
            case IMG_BLUR:
                image = blurImage(image, NULL);
                break;
            case IMG_SHARPEN:
                image = sharpenImage(image, NULL);
                break;
            case IMG_VERTEDGES:
                image = detectVerticalEdges(image, NULL);
                break;
            case IMG_HORIZEDGES:
                image = detectHorizontalEdges(image, NULL);
                break;
            case IMG_RETRIEVE:
                /* Retrieval operations are handled separately */
                break;
            default:
                /* Unknown operation */
                break;
        }

        /* If the operation modifies the image, handle accordingly */
        if (request_meta.request.img_op != IMG_RETRIEVE) {
            if (request_meta.request.overwrite) {
                /* Overwrite the existing image */
                deleteImage(registered_images[image_id]->img);
                registered_images[image_id]->img = image;
            } else {
                /* Create a new image entry */
                sem_wait(registration_mutex);
                uint64_t new_image_id = total_images++;
                registered_images = realloc(registered_images, sizeof(ImageObject *) * total_images);
                if (!registered_images) {
                    perror("Error: Unable to allocate memory for new image");
                    sem_post(registration_mutex);
                    exit(EXIT_FAILURE);
                }

                registered_images[new_image_id] = malloc(sizeof(ImageObject));
                if (!registered_images[new_image_id]) {
                    perror("Error: Unable to allocate memory for ImageObject");
                    sem_post(registration_mutex);
                    exit(EXIT_FAILURE);
                }

                registered_images[new_image_id]->img = image;
                sem_init(&registered_images[new_image_id]->img_mutex, 0, 1);
                sem_init(&registered_images[new_image_id]->operation_sem, 0, 1);
                registered_images[new_image_id]->assign_queue_pos = 0;
                registered_images[new_image_id]->next_queue_pos = 0;
                sem_post(registration_mutex);
            }
        }

        /* Record the completion time */
        clock_gettime(CLOCK_MONOTONIC, &request_meta.completion_time);

        /* Prepare the response to the client */
        Response response;
        response.req_id = request_meta.request.req_id;
        response.ack = RESP_COMPLETED;
        response.img_id = (request_meta.request.img_op == IMG_RETRIEVE) ? 
                          request_meta.request.img_id : 
                          (request_meta.request.overwrite ? 
                           request_meta.request.img_id : 
                           total_images - 1);

        /* Send the response to the client */
        sem_wait(socket_access_mutex);
        send(params->client_socket, &response, sizeof(Response), 0);

        /* If the operation is IMG_RETRIEVE, send the image data */
        if (request_meta.request.img_op == IMG_RETRIEVE) {
            if (sendImage(image, params->client_socket) != 0) {
                perror("Error: Failed to send image to client");
            }
        }
        sem_post(socket_access_mutex);

        /* Update the next expected queue position */
        sem_wait(&registered_images[image_id]->operation_sem);
        registered_images[image_id]->next_queue_pos++;
        sem_post(&registered_images[image_id]->operation_sem);

        /* Release Image Mutex */
        sem_post(&registered_images[image_id]->img_mutex);

        /* Log the request processing details */
        sem_wait(print_mutex);
        printf("Worker %d processed Request %ld: Operation %s on Image %ld -> Image %ld\n",
               params->worker_id, request_meta.request.req_id,
               opcode_to_string(request_meta.request.img_op),
               request_meta.request.img_id,
               response.img_id);
        display_queue_status(params->request_queue);
        sem_post(print_mutex);
    }

    /* Clean up and exit the thread */
    pthread_exit(NULL);
}

/**
 * Manages the creation and termination of worker threads.
 *
 * @param command Command indicating whether to start or stop workers.
 * @param worker_count Number of worker threads to manage.
 * @param common_params Shared parameters for worker threads.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
 */
int manage_worker_threads(WorkerCommand command, size_t worker_count, WorkerParams *common_params) {
    static pthread_t *worker_threads = NULL;
    static WorkerParams **workers_params = NULL;

    if (command == WORKER_START) {
        size_t i;

        /* Allocate memory for thread handles and parameters */
        worker_threads = malloc(sizeof(pthread_t) * worker_count);
        workers_params = malloc(sizeof(WorkerParams *) * worker_count);
        if (!worker_threads || !workers_params) {
            perror("Error: Unable to allocate memory for worker threads");
            return EXIT_FAILURE;
        }

        /* Initialize and create worker threads */
        for (i = 0; i < worker_count; i++) {
            workers_params[i] = malloc(sizeof(WorkerParams));
            if (!workers_params[i]) {
                perror("Error: Unable to allocate memory for WorkerParams");
                return EXIT_FAILURE;
            }

            /* Assign shared parameters */
            workers_params[i]->client_socket = common_params->client_socket;
            workers_params[i]->request_queue = common_params->request_queue;
            workers_params[i]->should_terminate = 0;
            workers_params[i]->worker_id = i;

            /* Create the worker thread */
            if (pthread_create(&worker_threads[i], NULL, worker_thread_main, workers_params[i]) != 0) {
                perror("Error: Failed to create worker thread");
                return EXIT_FAILURE;
            }

            printf("INFO: Worker thread %lu started.\n", i);
        }
    }
    else if (command == WORKER_STOP) {
        size_t i;

        /* Signal all worker threads to terminate */
        for (i = 0; i < (workers_params ? sizeof(workers_params) / sizeof(WorkerParams *) : 0); i++) {
            if (workers_params[i]) {
                workers_params[i]->should_terminate = 1;
            }
        }

        /* Notify all workers in case they are waiting on the semaphore */
        for (i = 0; i < worker_count; i++) {
            sem_post(queue_notify_sem);
        }

        /* Wait for all worker threads to finish */
        for (i = 0; i < worker_count; i++) {
            if (workers_params[i]) {
                pthread_join(worker_threads[i], NULL);
                printf("INFO: Worker thread %lu terminated.\n", i);
                free(workers_params[i]);
            }
        }

        /* Free allocated memory for thread handles and parameters */
        free(worker_threads);
        free(workers_params);
        worker_threads = NULL;
        workers_params = NULL;
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
 * @param client_socket The socket descriptor for the client connection.
 * @param config The server configuration parameters.
 */
void handle_client_connection(int client_socket, ServerConfig config) {
    RequestQueue request_queue;
    size_t bytes_received;
    RequestMeta incoming_request;
    WorkerParams common_params;

    /* Initialize the request queue */
    initialize_request_queue(&request_queue, config.queue_capacity, config.queue_policy);

    /* Set up common parameters for worker threads */
    common_params.client_socket = client_socket;
    common_params.request_queue = &request_queue;
    common_params.should_terminate = 0;
    common_params.worker_id = -1; /* Not used in common_params */

    /* Start worker threads */
    if (manage_worker_threads(WORKER_START, config.worker_count, &common_params) != EXIT_SUCCESS) {
        fprintf(stderr, "Error: Failed to start worker threads.\n");
        free(request_queue.requests);
        close(client_socket);
        return;
    }

    /* Continuously receive requests from the client */
    while ((bytes_received = recv(client_socket, &incoming_request.request, sizeof(Request), 0)) > 0) {
        /* Record the receipt time of the request */
        clock_gettime(CLOCK_MONOTONIC, &incoming_request.receipt_time);

        /* Handle image registration immediately */
        if (incoming_request.request.img_op == IMG_REGISTER) {
            clock_gettime(CLOCK_MONOTONIC, &incoming_request.start_time);
            register_image(client_socket, &incoming_request.request);
            clock_gettime(CLOCK_MONOTONIC, &incoming_request.completion_time);

            /* Log the registration */
            sem_wait(print_mutex);
            printf("Processed Registration: Request ID %ld, Image ID %ld\n",
                   incoming_request.request.req_id, total_images - 1);
            sem_post(print_mutex);
            continue;
        }

        /* Enqueue the request for processing */
        RequestMeta request_to_enqueue = incoming_request;
        int enqueue_status = enqueue_request(request_to_enqueue, &request_queue);

        if (enqueue_status) {
            /* Queue is full; send a rejection response */
            Response rejection;
            rejection.req_id = incoming_request.request.req_id;
            rejection.ack = RESP_REJECTED;

            sem_wait(socket_access_mutex);
            send(client_socket, &rejection, sizeof(Response), 0);
            sem_post(socket_access_mutex);

            /* Log the rejection */
            sem_wait(print_mutex);
            printf("Rejected Request ID %ld: Queue Full\n", incoming_request.request.req_id);
            sem_post(print_mutex);
        }
    }

    /* Cleanup after client disconnects */
    manage_worker_threads(WORKER_STOP, config.worker_count, NULL);
    free(request_queue.requests);
    close(client_socket);
    printf("INFO: Client connection closed.\n");
}

/**
 * Cleans up all allocated resources and destroys semaphores.
 */
void cleanup_resources() {
    /* Destroy all semaphores */
    sem_destroy(print_mutex);
    sem_destroy(queue_access_mutex);
    sem_destroy(queue_notify_sem);
    sem_destroy(registration_mutex);
    sem_destroy(socket_access_mutex);

    /* Free semaphore memory */
    free(print_mutex);
    free(queue_access_mutex);
    free(queue_notify_sem);
    free(registration_mutex);
    free(socket_access_mutex);

    /* Destroy and free all registered images */
    for (uint64_t i = 0; i < total_images; i++) {
        if (registered_images[i]) {
            sem_destroy(&registered_images[i]->img_mutex);
            sem_destroy(&registered_images[i]->operation_sem);
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
Image* recvImage(int socket) {
    /* Implementation assumed to be provided in "common.h" */
    // Example Placeholder
    Image *img = malloc(sizeof(Image));
    if (!img) return NULL;
    // Receive image data and populate 'img'
    return img;
}

/**
 * Sends an image to the client over the specified socket.
 *
 * @param image Pointer to the Image structure to send.
 * @param socket The socket descriptor to send the image through.
 * @return 0 on success, non-zero on failure.
 */
int sendImage(Image *image, int socket) {
    /* Implementation assumed to be provided in "common.h" */
    // Example Placeholder
    if (!image) return -1;
    // Send image data over the socket
    return 0;
}

/**
 * Deletes an image and frees associated resources.
 *
 * @param img Pointer to the Image structure to delete.
 */
void deleteImage(Image *img) {
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
