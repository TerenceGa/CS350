/*******************************************************************************
 * Enhanced Multi-Threaded FIFO Image Server Implementation with Queue Limit
 *
 * Description:
 *     A robust server implementation designed to process client
 *     requests for image processing in First In, First Out (FIFO)
 *     order. The server binds to the specified port number provided as
 *     a parameter upon launch. It supports multiple worker threads
 *     to process incoming requests concurrently and allows specification
 *     of a maximum queue size. Enhancements include improved synchronization,
 *     error handling, logging, and memory management.
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
 *     Renato Mancuso (Original Template)
 *
 * Affiliation:
 *     Boston University
 *
 * Creation Date:
 *     October 31, 2023 (Original Template)
 *
 * Notes:
 *     Ensure proper permissions and an available port before running the
 *     server. The server uses a FIFO mechanism to handle requests, ensuring
 *     the order of processing. If the queue is full when a new request is
 *     received, the request is rejected with a negative acknowledgment.
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
#include <stdarg.h>
#include <stdatomic.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING                                                 \
    "Missing parameter. Exiting.\n"                                 \
    "Usage: %s -q <queue size> "                                    \
    "-w <workers: 1> "                                              \
    "-p <policy: FIFO> "                                            \
    "<port_number>\n"

/* Logging Levels */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

/* Logging Function */
sem_t * log_mutex;

void log_message(log_level_t level, const char *format, ...) {
    const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    va_list args;
    
    sem_wait(log_mutex);
    printf("[%s] ", level_strings[level]);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    sem_post(log_mutex);
}

/* Mutex needed to protect the threaded printf. DO NOT TOUCH */
sem_t * printf_mutex;

/* Synchronized printf for multi-threaded operation */
#define sync_printf(...)                            \
    do {                                            \
        sem_wait(printf_mutex);                     \
        printf(__VA_ARGS__);                        \
        sem_post(printf_mutex);                     \
    } while (0)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

/* Additional Semaphores for Synchronization Enhancements */
sem_t * images_array_mutex; // Protects the global images array
sem_t * socket_mutex;       // Protects socket operations
sem_t * request_mutex;      // Protects request retrieval from the queue

/* Global array of registered images and its length -- reallocated as we go! */
struct img_object **images = NULL;
atomic_uint image_count = 0;

/* Image Object Structure with Synchronization Primitives */
struct img_object {
    struct image *img;
    pthread_mutex_t img_mutex;       // Mutex for exclusive image access
    pthread_cond_t img_cond_var;     // Condition variable for operation sequencing
    atomic_int assign_q;             // Ticket counter
    atomic_int q_next;               // Next operation turn
};

/* Request Metadata Structure */
struct request_meta {
    struct request request;
    struct timespec receipt_timestamp;
    struct timespec start_timestamp;
    struct timespec completion_timestamp;
};

/* Queue Policy Enumeration */
enum queue_policy {
    QUEUE_FIFO,
    QUEUE_SJN
};

/* Queue Structure */
struct queue {
    size_t wr_pos;
    size_t rd_pos;
    size_t max_size;
    size_t available;
    enum queue_policy policy;
    struct request_meta *requests;
};

/* Connection Parameters Structure */
struct connection_params {
    size_t queue_size;
    size_t workers;
    enum queue_policy queue_policy;
};

/* Worker Parameters Structure */
struct worker_params {
    int conn_socket;
    atomic_int worker_done;
    struct queue *the_queue;
    int worker_id;
};

/* Worker Command Enumeration */
enum worker_command {
    WORKERS_START,
    WORKERS_STOP
};

/* Function Prototypes */
void queue_init(struct queue *the_queue, size_t queue_size, enum queue_policy policy);
int add_to_queue(struct request_meta to_add, struct queue *the_queue);
struct request_meta get_from_queue(struct queue *the_queue);
void dump_queue_status(struct queue *the_queue);
void register_new_image(int conn_socket, struct request *req);
void *worker_thread_function(void *arg);
int control_workers(enum worker_command cmd, size_t worker_count, struct worker_params *common_params);
void handle_connection(int conn_socket, struct connection_params conn_params);
void cleanup_resources(struct queue *the_queue);

/* Initialize the Queue */
void queue_init(struct queue *the_queue, size_t queue_size, enum queue_policy policy) {
    the_queue->rd_pos = 0;
    the_queue->wr_pos = 0;
    the_queue->max_size = queue_size;
    the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta) * the_queue->max_size);
    if (the_queue->requests == NULL) {
        log_message(LOG_ERROR, "Failed to allocate memory for queue requests.");
        exit(EXIT_FAILURE);
    }
    the_queue->available = queue_size;
    the_queue->policy = policy;
}

/* Add a New Request to the Queue */
int add_to_queue(struct request_meta to_add, struct queue *the_queue) {
    int retval = 0;
    /* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
    sem_wait(queue_mutex);
    /* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

    /* Add the request if the queue is not full */
    if (the_queue->available == 0) {
        retval = 1; // Queue is full
    } else {
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

/* Retrieve a Request from the Queue */
struct request_meta get_from_queue(struct queue *the_queue) {
    struct request_meta retval;
    /* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
    sem_wait(queue_notify); // If queue is empty, workers will be blocked
    sem_wait(queue_mutex);  // One worker can take from queue
    /* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

    retval = the_queue->requests[the_queue->rd_pos];
    the_queue->rd_pos = (the_queue->rd_pos + 1) % the_queue->max_size;
    the_queue->available++;

    /* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
    sem_post(queue_mutex);
    /* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
    return retval;
}

/* Dump the Current Status of the Queue */
void dump_queue_status(struct queue *the_queue) {
    size_t i, j;
    /* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
    sem_wait(queue_mutex);
    /* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

    sem_wait(log_mutex);
    printf("Q:[");
    for (i = the_queue->rd_pos, j = 0; j < the_queue->max_size - the_queue->available;
         i = (i + 1) % the_queue->max_size, ++j) {
        printf("R%ld%s", the_queue->requests[i].request.req_id,
               ((j + 1 != the_queue->max_size - the_queue->available) ? "," : ""));
    }
    printf("]\n");
    sem_post(log_mutex);

    /* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
    sem_post(queue_mutex);
    /* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

/* Register a New Image */
void register_new_image(int conn_socket, struct request *req) {
    /* Lock the images array to ensure thread safety */
    sem_wait(images_array_mutex);

    int retval = 0;
    uint64_t index = atomic_fetch_add(&image_count, 1); // Atomic increment

    /* Reallocate array of image pointers */
    struct img_object **temp_images = realloc(images, image_count * sizeof(struct img_object *));
    if (temp_images == NULL) {
        log_message(LOG_ERROR, "Failed to realloc images array.");
        sem_post(images_array_mutex);
        exit(EXIT_FAILURE);
    }
    images = temp_images;

    /* Allocate and initialize the new image object */
    images[index] = (struct img_object *)malloc(sizeof(struct img_object));
    if (images[index] == NULL) {
        log_message(LOG_ERROR, "Failed to allocate memory for new img_object.");
        sem_post(images_array_mutex);
        exit(EXIT_FAILURE);
    }

    /* Receive the new image from the client */
    images[index]->img = recvImage(conn_socket);
    if (images[index]->img == NULL) {
        log_message(LOG_ERROR, "Failed to receive image from client.");
        free(images[index]);
        sem_post(images_array_mutex);
        exit(EXIT_FAILURE);
    }

    /* Initialize the image mutex */
    if (pthread_mutex_init(&images[index]->img_mutex, NULL) != 0) {
        log_message(LOG_ERROR, "Failed to initialize image mutex.");
        deleteImage(images[index]->img);
        free(images[index]);
        sem_post(images_array_mutex);
        exit(EXIT_FAILURE);
    }

    /* Initialize the condition variable */
    if (pthread_cond_init(&images[index]->img_cond_var, NULL) != 0) {
        log_message(LOG_ERROR, "Failed to initialize image condition variable.");
        pthread_mutex_destroy(&images[index]->img_mutex);
        deleteImage(images[index]->img);
        free(images[index]);
        sem_post(images_array_mutex);
        exit(EXIT_FAILURE);
    }

    /* Initialize operation sequencing counters atomically */
    atomic_init(&images[index]->assign_q, 0);
    atomic_init(&images[index]->q_next, 0);

    /* Unlock the images array after modifications */
    sem_post(images_array_mutex);

    /* Immediately provide a response to the client */
    struct response resp;
    resp.req_id = req->req_id;
    resp.img_id = index;
    resp.ack = RESP_COMPLETED;

    /* Lock the socket for sending responses */
    sem_wait(socket_mutex);
    ssize_t sent_bytes = send(conn_socket, &resp, sizeof(struct response), 0);
    if (sent_bytes == -1) {
        log_message(LOG_ERROR, "Failed to send registration response to client.");
    }
    sem_post(socket_mutex);

    /* Log the registration event */
    log_message(LOG_INFO, "Registered new image: ReqID=%ld, ImgID=%ld", req->req_id, index);
}

/* Worker Thread Function */
void *worker_thread_function(void *arg) {
    struct timespec now;
    struct worker_params *params = (struct worker_params *)arg;

    /* Print the first alive message. */
    clock_gettime(CLOCK_MONOTONIC, &now);
    log_message(LOG_INFO, "[#WORKER#] %.6lf Worker Thread Alive!", TSPEC_TO_DOUBLE(now));

    /* Execute the main logic. */
    while (!atomic_load(&params->worker_done)) {
        struct request_meta req;
        struct response resp;
        struct image *img = NULL;
        uint64_t img_id;

        /* Retrieve the next request from the queue */
        req = get_from_queue(params->the_queue);

        /* Detect wakeup after termination asserted */
        if (atomic_load(&params->worker_done)) {
            break;
        }

        /* Record the start timestamp */
        clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);

        img_id = req.request.img_id;

        /* Ensure img_id is within bounds */
        if (img_id >= atomic_load(&image_count)) {
            log_message(LOG_WARN, "Invalid ImgID %ld received.", img_id);
            continue;
        }

        /* Acquire a ticket for operation sequencing */
        int ticket = atomic_fetch_add(&images[img_id]->assign_q, 1) + 1;

        log_message(LOG_DEBUG, "Worker %d acquired ticket %d for ImgID %ld.", params->worker_id, ticket, img_id);

        /* Lock the image mutex and wait for the turn */
        pthread_mutex_lock(&images[img_id]->img_mutex);
        while (ticket != atomic_load(&images[img_id]->q_next)) {
            pthread_cond_wait(&images[img_id]->img_cond_var, &images[img_id]->img_mutex);
        }

        /* It's this thread's turn to process the image operation */
        img = images[img_id]->img;
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
            case IMG_RETRIEVE:
                /* No modification needed */
                break;
            default:
                log_message(LOG_WARN, "Unknown image operation: %d", req.request.img_op);
                break;
        }

        /* Handle the operation result */
        if (req.request.img_op != IMG_RETRIEVE) {
            if (req.request.overwrite) {
                /* Overwrite the existing image */
                deleteImage(images[img_id]->img);
                images[img_id]->img = img;
                log_message(LOG_INFO, "Overwritten image ImgID=%ld with new operation.", img_id);
            } else {
                /* Register the modified image as a new image */
                sem_wait(images_array_mutex);
                uint64_t new_img_id = atomic_fetch_add(&image_count, 1);

                /* Reallocate the images array */
                struct img_object **temp_images = realloc(images, image_count * sizeof(struct img_object *));
                if (temp_images == NULL) {
                    log_message(LOG_ERROR, "Failed to realloc images array for new image.");
                    sem_post(images_array_mutex);
                    pthread_mutex_unlock(&images[img_id]->img_mutex);
                    exit(EXIT_FAILURE);
                }
                images = temp_images;

                /* Allocate and initialize the new image object */
                images[new_img_id] = (struct img_object *)malloc(sizeof(struct img_object));
                if (images[new_img_id] == NULL) {
                    log_message(LOG_ERROR, "Failed to allocate memory for new img_object.");
                    sem_post(images_array_mutex);
                    pthread_mutex_unlock(&images[img_id]->img_mutex);
                    exit(EXIT_FAILURE);
                }

                images[new_img_id]->img = img;

                /* Initialize the image mutex and condition variable */
                if (pthread_mutex_init(&images[new_img_id]->img_mutex, NULL) != 0) {
                    log_message(LOG_ERROR, "Failed to initialize image mutex for new image.");
                    deleteImage(images[new_img_id]->img);
                    free(images[new_img_id]);
                    sem_post(images_array_mutex);
                    pthread_mutex_unlock(&images[img_id]->img_mutex);
                    exit(EXIT_FAILURE);
                }

                if (pthread_cond_init(&images[new_img_id]->img_cond_var, NULL) != 0) {
                    log_message(LOG_ERROR, "Failed to initialize image condition variable for new image.");
                    pthread_mutex_destroy(&images[new_img_id]->img_mutex);
                    deleteImage(images[new_img_id]->img);
                    free(images[new_img_id]);
                    sem_post(images_array_mutex);
                    pthread_mutex_unlock(&images[img_id]->img_mutex);
                    exit(EXIT_FAILURE);
                }

                /* Initialize operation sequencing counters atomically */
                atomic_init(&images[new_img_id]->assign_q, 0);
                atomic_init(&images[new_img_id]->q_next, 0);

                sem_post(images_array_mutex);
                log_message(LOG_INFO, "Registered new image ImgID=%ld from worker %d.", new_img_id, params->worker_id);
            }
        }

        /* Record the completion timestamp */
        clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

        /* Prepare the response */
        resp.req_id = req.request.req_id;
        resp.ack = RESP_COMPLETED;
        resp.img_id = img_id;

        /* Lock the socket for sending responses */
        sem_wait(socket_mutex);
        ssize_t sent_bytes = send(params->conn_socket, &resp, sizeof(struct response), 0);
        if (sent_bytes == -1) {
            log_message(LOG_ERROR, "Failed to send response to client for ReqID=%ld.", req.request.req_id);
        }

        /* Handle IMG_RETRIEVE operation by sending the image payload */
        if (req.request.img_op == IMG_RETRIEVE) {
            uint8_t err = sendImage(img, params->conn_socket);
            if (err) {
                log_message(LOG_ERROR, "Failed to send image payload to client for ImgID=%ld.", img_id);
            } else {
                log_message(LOG_INFO, "Sent image payload for ImgID=%ld to client.", img_id);
            }
        }
        sem_post(socket_mutex);

        /* Log the processed request */
        log_message(LOG_INFO, "Worker %d processed ReqID=%ld: Op=%s, Overwrite=%d, ImgID=%ld -> ImgID=%ld",
                    params->worker_id, req.request.req_id,
                    OPCODE_TO_STRING(req.request.img_op),
                    req.request.overwrite, req.request.img_id,
                    img_id);

        /* Dump the current queue status */
        dump_queue_status(params->the_queue);

        /* Increment the next operation turn and signal waiting threads */
        atomic_fetch_add(&images[img_id]->q_next, 1);
        pthread_cond_broadcast(&images[img_id]->img_cond_var);

        /* Unlock the image mutex */
        pthread_mutex_unlock(&images[img_id]->img_mutex);
    }

    /* Control Worker Threads: Start or Stop */
    int control_workers(enum worker_command cmd, size_t worker_count, struct worker_params *common_params) {
        /* Static variables to keep track of workers */
        static pthread_t *worker_pthreads = NULL;
        static struct worker_params **worker_params_array = NULL;
        static int *worker_ids = NULL;

        /* Start all the workers */
        if (cmd == WORKERS_START) {
            size_t i;

            /* Allocate all structs and parameters */
            worker_pthreads = (pthread_t *)malloc(worker_count * sizeof(pthread_t));
            worker_params_array = (struct worker_params **)malloc(worker_count * sizeof(struct worker_params *));
            worker_ids = (int *)malloc(worker_count * sizeof(int));

            if (!worker_pthreads || !worker_params_array || !worker_ids) {
                log_message(LOG_ERROR, "Unable to allocate arrays for worker threads.");
                return EXIT_FAILURE;
            }

            /* Allocate and initialize worker parameters */
            for (i = 0; i < worker_count; ++i) {
                worker_ids[i] = -1;

                worker_params_array[i] = (struct worker_params *)malloc(sizeof(struct worker_params));
                if (!worker_params_array[i]) {
                    log_message(LOG_ERROR, "Unable to allocate memory for worker parameters.");
                    return EXIT_FAILURE;
                }

                worker_params_array[i]->conn_socket = common_params->conn_socket;
                worker_params_array[i]->the_queue = common_params->the_queue;
                atomic_init(&worker_params_array[i]->worker_done, 0);
                worker_params_array[i]->worker_id = i;
            }

            /* Start the worker threads */
            for (i = 0; i < worker_count; ++i) {
                if (pthread_create(&worker_pthreads[i], NULL, worker_thread_function, worker_params_array[i]) != 0) {
                    log_message(LOG_ERROR, "Unable to start worker thread %ld.", i);
                    return EXIT_FAILURE;
                } else {
                    log_message(LOG_INFO, "Worker thread %ld started successfully.", i);
                }
            }
        }
        /* Stop all the workers */
        else if (cmd == WORKERS_STOP) {
            size_t i;

            /* Check if workers were started */
            if (!worker_pthreads || !worker_params_array || !worker_ids) {
                log_message(LOG_WARN, "No workers to stop.");
                return EXIT_FAILURE;
            }

            /* Signal all workers to terminate */
            for (i = 0; i < worker_count; ++i) {
                if (worker_ids[i] < 0) {
                    continue;
                }

                atomic_store(&worker_params_array[i]->worker_done, 1);
            }

            /* Unblock all workers waiting on the queue */
            for (i = 0; i < worker_count; ++i) {
                if (worker_ids[i] < 0) {
                    continue;
                }

                sem_post(queue_notify);
            }

            /* Join all worker threads */
            for (i = 0; i < worker_count; ++i) {
                if (worker_ids[i] < 0) {
                    continue;
                }

                pthread_join(worker_pthreads[i], NULL);
                log_message(LOG_INFO, "Worker thread %ld exited.", i);
            }

            /* Clean up allocated worker parameters */
            for (i = 0; i < worker_count; ++i) {
                free(worker_params_array[i]);
            }

            free(worker_pthreads);
            free(worker_params_array);
            free(worker_ids);

            worker_pthreads = NULL;
            worker_params_array = NULL;
            worker_ids = NULL;
        }
        /* Invalid command */
        else {
            log_message(LOG_ERROR, "Invalid worker control command.");
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    /* Handle Client Connection */
    void handle_connection(int conn_socket, struct connection_params conn_params) {
        struct request_meta *req = NULL;
        struct queue *the_queue = NULL;
        size_t in_bytes;
        int res;
        int retval;

        /* Allocate and initialize the request metadata */
        req = (struct request_meta *)malloc(sizeof(struct request_meta));
        if (req == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for request_meta.");
            close(conn_socket);
            exit(EXIT_FAILURE);
        }

        /* Allocate and initialize the queue */
        the_queue = (struct queue *)malloc(sizeof(struct queue));
        if (the_queue == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for queue.");
            free(req);
            close(conn_socket);
            exit(EXIT_FAILURE);
        }
        queue_init(the_queue, conn_params.queue_size, conn_params.queue_policy);

        /* Initialize worker parameters and start workers */
        struct worker_params common_worker_params;
        common_worker_params.conn_socket = conn_socket;
        common_worker_params.the_queue = the_queue;
        res = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

        /* Initialize registration mutex */
        sem_wait(reg_mutex); // Ensure mutual exclusion during registration
        sem_post(reg_mutex);

        /* Do not continue if there has been a problem while starting the workers. */
        if (res != EXIT_SUCCESS) {
            free(the_queue);
            free(req);
            control_workers(WORKERS_STOP, conn_params.workers, NULL);
            return;
        }

        /* Start receiving and handling client requests */
        do {
            in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
            if (in_bytes < 0) {
                log_message(LOG_ERROR, "Failed to receive data from client.");
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

            /* Handle image registration immediately */
            if (in_bytes > 0 && req->request.img_op == IMG_REGISTER) {
                clock_gettime(CLOCK_MONOTONIC, &req->start_timestamp);
                register_new_image(conn_socket, &req->request);
                clock_gettime(CLOCK_MONOTONIC, &req->completion_timestamp);

                log_message(LOG_INFO, "Processed IMG_REGISTER: ReqID=%ld, ImgID=%ld, Timestamps=[%.6lf, %.6lf, %.6lf]",
                            req->request.req_id, req->request.img_id,
                            TSPEC_TO_DOUBLE(req->receipt_timestamp),
                            TSPEC_TO_DOUBLE(req->start_timestamp),
                            TSPEC_TO_DOUBLE(req->completion_timestamp));

                dump_queue_status(the_queue);
                continue;
            }

            /* Enqueue the request */
            if (in_bytes > 0) {
                res = add_to_queue(*req, the_queue);

                /* If the queue is full, reject the request */
                if (res) {
                    struct response resp;
                    resp.req_id = req->request.req_id;
                    resp.ack = RESP_REJECTED;

                    /* Lock the socket for sending responses */
                    sem_wait(socket_mutex);
                    ssize_t sent_bytes = send(conn_socket, &resp, sizeof(struct response), 0);
                    if (sent_bytes == -1) {
                        log_message(LOG_ERROR, "Failed to send rejection response to client for ReqID=%ld.", req->request.req_id);
                    }
                    sem_post(socket_mutex);

                    log_message(LOG_WARN, "Rejected ReqID=%ld: Queue Full.", req->request.req_id);
                }
            }
        } while (in_bytes > 0);

        /* Clean up and shutdown the connection */
        control_workers(WORKERS_STOP, conn_params.workers, NULL);
        cleanup_resources(the_queue);
        free(req);
        shutdown(conn_socket, SHUT_RDWR);
        close(conn_socket);
        log_message(LOG_INFO, "Client disconnected.");
    }

    /* Cleanup Resources to Prevent Memory Leaks */
    void cleanup_resources(struct queue *the_queue) {
        if (the_queue) {
            free(the_queue->requests);
            free(the_queue);
        }

        /* Clean up all images and their synchronization primitives */
        for (uint64_t i = 0; i < atomic_load(&image_count); ++i) {
            if (images[i]) {
                deleteImage(images[i]->img);
                pthread_mutex_destroy(&images[i]->img_mutex);
                pthread_cond_destroy(&images[i]->img_cond_var);
                free(images[i]);
            }
        }
        free(images);

        /* Destroy and free semaphores and mutexes */
        sem_destroy(printf_mutex);
        free(printf_mutex);
        sem_destroy(queue_mutex);
        free(queue_mutex);
        sem_destroy(queue_notify);
        free(queue_notify);
        sem_destroy(images_array_mutex);
        free(images_array_mutex);
        sem_destroy(socket_mutex);
        free(socket_mutex);
        sem_destroy(request_mutex);
        free(request_mutex);

        /* Additional cleanup can be added here if necessary */
    }

    /* Main Function: Server Setup and Connection Handling */
    int main(int argc, char **argv) {
        int sockfd, retval, accepted, optval, opt;
        in_port_t socket_port;
        struct sockaddr_in addr, client;
        struct in_addr any_address;
        socklen_t client_len;
        struct connection_params conn_params;
        conn_params.queue_size = 0;
        conn_params.queue_policy = QUEUE_FIFO;
        conn_params.workers = 1;

        /* Parse all the command line arguments */
        while ((opt = getopt(argc, argv, "q:w:p:")) != -1) {
            switch (opt) {
                case 'q':
                    conn_params.queue_size = strtol(optarg, NULL, 10);
                    log_message(LOG_INFO, "Setting queue size to %ld.", conn_params.queue_size);
                    break;
                case 'w':
                    conn_params.workers = strtol(optarg, NULL, 10);
                    log_message(LOG_INFO, "Setting worker count to %ld.", conn_params.workers);
                    break;
                case 'p':
                    if (!strcmp(optarg, "FIFO")) {
                        conn_params.queue_policy = QUEUE_FIFO;
                    } else {
                        log_message(LOG_ERROR, "Invalid queue policy: %s", optarg);
                        fprintf(stderr, "Invalid queue policy.\n" USAGE_STRING, argv[0]);
                        return EXIT_FAILURE;
                    }
                    log_message(LOG_INFO, "Setting queue policy to %s.", optarg);
                    break;
                default: /* '?' */
                    fprintf(stderr, USAGE_STRING, argv[0]);
                    return EXIT_FAILURE;
            }
        }

        if (conn_params.queue_size == 0) {
            log_message(LOG_ERROR, "Queue size must be greater than 0.");
            fprintf(stderr, USAGE_STRING, argv[0]);
            return EXIT_FAILURE;
        }

        if (optind < argc) {
            socket_port = strtol(argv[optind], NULL, 10);
            log_message(LOG_INFO, "Setting server port to %d.", socket_port);
        } else {
            log_message(LOG_ERROR, "Port number not specified.");
            fprintf(stderr, USAGE_STRING, argv[0]);
            return EXIT_FAILURE;
        }

        /* Create a TCP socket */
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            log_message(LOG_ERROR, "Failed to create socket.");
            perror("Unable to create socket");
            return EXIT_FAILURE;
        }

        /* Set socket options to reuse address */
        optval = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval)) < 0) {
            log_message(LOG_ERROR, "Failed to set socket options.");
            perror("setsockopt");
            close(sockfd);
            return EXIT_FAILURE;
        }

        /* Convert INADDR_ANY into network byte order */
        any_address.s_addr = htonl(INADDR_ANY);

        /* Bind the socket to the specified port */
        addr.sin_family = AF_INET;
        addr.sin_port = htons(socket_port);
        addr.sin_addr = any_address;

        retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
        if (retval < 0) {
            log_message(LOG_ERROR, "Failed to bind socket to port %d.", socket_port);
            perror("Unable to bind socket");
            close(sockfd);
            return EXIT_FAILURE;
        }

        /* Listen for incoming connections */
        retval = listen(sockfd, BACKLOG_COUNT);
        if (retval < 0) {
            log_message(LOG_ERROR, "Failed to listen on socket.");
            perror("Unable to listen on socket");
            close(sockfd);
            return EXIT_FAILURE;
        }

        log_message(LOG_INFO, "Server is listening on port %d.", socket_port);

        /* Accept a single client connection */
        client_len = sizeof(struct sockaddr_in);
        accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);
        if (accepted == -1) {
            log_message(LOG_ERROR, "Failed to accept client connection.");
            perror("Unable to accept connections");
            close(sockfd);
            return EXIT_FAILURE;
        }

        log_message(LOG_INFO, "Client connected.");

        /* Initialize logging mutex */
        log_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (log_mutex == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for log_mutex.");
            close(accepted);
            close(sockfd);
            return EXIT_FAILURE;
        }
        if (sem_init(log_mutex, 0, 1) < 0) {
            log_message(LOG_ERROR, "Failed to initialize log_mutex.");
            free(log_mutex);
            close(accepted);
            close(sockfd);
            return EXIT_FAILURE;
        }

        /* Initialize threaded printf mutex */
        printf_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (printf_mutex == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for printf_mutex.");
            sem_destroy(log_mutex);
            free(log_mutex);
            close(accepted);
            close(sockfd);
            return EXIT_FAILURE;
        }
        if (sem_init(printf_mutex, 0, 1) < 0) {
            log_message(LOG_ERROR, "Failed to initialize printf_mutex.");
            free(printf_mutex);
            sem_destroy(log_mutex);
            free(log_mutex);
            close(accepted);
            close(sockfd);
            return EXIT_FAILURE;
        }

        /* Initialize queue protection variables. DO NOT TOUCH. */
        queue_mutex = (sem_t *)malloc(sizeof(sem_t));
        queue_notify = (sem_t *)malloc(sizeof(sem_t));
        if (queue_mutex == NULL || queue_notify == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for queue semaphores.");
            cleanup_resources(NULL);
            close(accepted);
            close(sockfd);
            return EXIT_FAILURE;
        }
        if (sem_init(queue_mutex, 0, 1) < 0) {
            log_message(LOG_ERROR, "Failed to initialize queue_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }
        if (sem_init(queue_notify, 0, 0) < 0) {
            log_message(LOG_ERROR, "Failed to initialize queue_notify.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }
        /* DONE - Initialize queue protection variables */

        /* Initialize additional semaphores and mutexes */
        images_array_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (images_array_mutex == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for images_array_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }
        if (sem_init(images_array_mutex, 0, 1) < 0) {
            log_message(LOG_ERROR, "Failed to initialize images_array_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }

        socket_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (socket_mutex == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for socket_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }
        if (sem_init(socket_mutex, 0, 1) < 0) {
            log_message(LOG_ERROR, "Failed to initialize socket_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }

        request_mutex = (sem_t *)malloc(sizeof(sem_t));
        if (request_mutex == NULL) {
            log_message(LOG_ERROR, "Failed to allocate memory for request_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }
        if (sem_init(request_mutex, 0, 1) < 0) {
            log_message(LOG_ERROR, "Failed to initialize request_mutex.");
            cleanup_resources(NULL);
            return EXIT_FAILURE;
        }

        /* Ready to handle the new connection with the client. */
        handle_connection(accepted, conn_params);

        /* Clean up and shutdown */
        cleanup_resources(NULL);
        close(sockfd);
        log_message(LOG_INFO, "Server shutdown successfully.");

        return EXIT_SUCCESS;
    }
