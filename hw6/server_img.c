/*******************************************************************************
 * Single-Threaded FIFO Image Server Implementation with Queue Limit
 *
 * Description:
 *     This server handles image processing requests from clients in a First In,
 *     First Out (FIFO) order. It binds to a specified port and uses a secondary
 *     thread to process incoming requests. The server allows specifying a maximum
 *     queue size.
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
 *     Ensure proper permissions and an available port before running the server.
 *     The server processes requests in a FIFO manner, ensuring order of processing.
 *     If the queue is full when a new request arrives, the request is rejected.
 *
 *******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include "common.h"

#define BACKLOG_LIMIT 100
#define USAGE_MESSAGE \
    "Missing parameter. Exiting.\n" \
    "Usage: %s -q <queue size> " \
    "-w <workers: 1> " \
    "-p <policy: FIFO> " \
    "<port_number>\n"

#define THREAD_STACK_SIZE (4096)

/* Mutex for synchronized printf. DO NOT MODIFY */
sem_t *printf_mutex;

/* Synchronized printf for multi-threaded operation */
#define synchronized_printf(...)     \
    do {                             \
        sem_wait(printf_mutex);      \
        printf(__VA_ARGS__);         \
        sem_post(printf_mutex);      \
    } while (0)

/* Variables to protect the shared queue. DO NOT MODIFY */
sem_t *queue_mutex;
sem_t *queue_notify;

struct request_meta {
    struct request request;
    struct timespec receipt_time;
    struct timespec start_time;
    struct timespec completion_time;
};

enum queue_policy {
    QUEUE_FIFO,
    QUEUE_SJN
};

struct queue {
    size_t write_pos;
    size_t read_pos;
    size_t max_size;
    size_t available;
    enum queue_policy policy;
    struct request_meta *requests;
};

struct connection_params {
    size_t queue_size;
    size_t workers;
    enum queue_policy policy;
};

struct worker_params {
    int conn_socket;
    int worker_done;
    struct queue *queue;
    int worker_id;
    struct dynamic_array *image_array;
};

enum worker_command {
    WORKERS_START,
    WORKERS_STOP
};

struct dynamic_array {
    struct image **images;
    size_t capacity;
    size_t count;
};

volatile uint64_t next_img_id = 0;

/* Image array helper functions */
void dynamic_array_init(struct dynamic_array *array, size_t initial_capacity) {
    array->images = malloc(initial_capacity * sizeof(struct image *));
    array->capacity = initial_capacity;
    array->count = 0;
}

void dynamic_array_add(struct dynamic_array *array, struct image *img) {
    if (array->count >= array->capacity) {
        array->capacity *= 2;
        array->images = realloc(array->images, array->capacity * sizeof(struct image *));
    }
    array->images[array->count] = img;
    array->count++;
}

struct image *dynamic_array_get(struct dynamic_array *array, uint64_t img_id) {
    if (img_id >= array->count) {
        return NULL;  // Invalid img_id
    }
    return array->images[img_id];
}

void dynamic_array_free(struct dynamic_array *array) {
    for (size_t i = 0; i < array->count; i++) {
        free(array->images[i]->pixels);
        free(array->images[i]);
    }
    free(array->images);
}

void queue_initialize(struct queue *queue, size_t size, enum queue_policy policy) {
    queue->read_pos = 0;
    queue->write_pos = 0;
    queue->max_size = size;
    queue->requests = malloc(sizeof(struct request_meta) * size);
    queue->available = size;
    queue->policy = policy;
}

int queue_add(struct request_meta to_add, struct queue *queue) {
    int result = 0;
    /* Queue protection start */
    sem_wait(queue_mutex);
    /* Queue protection end */

    if (queue->available == 0) {
        result = 1;
    } else {
        queue->requests[queue->write_pos] = to_add;
        queue->write_pos = (queue->write_pos + 1) % queue->max_size;
        queue->available--;
        /* Signal consumer */
        sem_post(queue_notify);
    }

    /* Queue protection start */
    sem_post(queue_mutex);
    /* Queue protection end */
    return result;
}

struct request_meta queue_get(struct queue *queue) {
    struct request_meta result;
    /* Queue protection start */
    sem_wait(queue_notify);
    sem_wait(queue_mutex);
    /* Queue protection end */

    result = queue->requests[queue->read_pos];
    queue->read_pos = (queue->read_pos + 1) % queue->max_size;
    queue->available++;

    /* Queue protection start */
    sem_post(queue_mutex);
    /* Queue protection end */
    return result;
}

void queue_dump_status(struct queue *queue) {
    size_t i, j;
    /* Queue protection start */
    sem_wait(queue_mutex);
    /* Queue protection end */

    synchronized_printf("Q:[");

    for (i = queue->read_pos, j = 0; j < queue->max_size - queue->available;
         i = (i + 1) % queue->max_size, ++j) {
        synchronized_printf("R%ld%s", queue->requests[i].request.req_id,
                            ((j + 1 != queue->max_size - queue->available) ? "," : ""));
    }

    synchronized_printf("]\n");

    /* Queue protection start */
    sem_post(queue_mutex);
    /* Queue protection end */
}

/* Main logic for worker thread */
void *worker_main(void *arg) {
    struct timespec current_time;
    struct worker_params *params = (struct worker_params *)arg;

    /* Indicate that the worker thread is alive */
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    synchronized_printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(current_time));

    while (!params->worker_done) {
        struct image *input_image;
        struct image *output_image;
        uint8_t operation_result = 0;
        uint8_t should_overwrite = 1;

        uint64_t client_img_id;
        uint64_t server_img_id;

        struct request_meta req;
        struct response resp;
        req = queue_get(params->queue);
        client_img_id = req.request.img_id;
        server_img_id = client_img_id;

        /* Check if termination was requested */
        if (params->worker_done)
            break;

        clock_gettime(CLOCK_MONOTONIC, &req.start_time);
        input_image = dynamic_array_get(params->image_array, req.request.img_id);

        /* Process the client's request */
        switch (req.request.img_op) {
            case IMG_ROT90CLKW:
                output_image = rotate90Clockwise(input_image, &operation_result);
                break;
            case IMG_SHARPEN:
                output_image = sharpenImage(input_image, &operation_result);
                break;
            case IMG_BLUR:
                output_image = blurImage(input_image, &operation_result);
                break;
            case IMG_VERTEDGES:
                output_image = detectVerticalEdges(input_image, &operation_result);
                break;
            case IMG_HORIZEDGES:
                output_image = detectHorizontalEdges(input_image, &operation_result);
                break;
            case IMG_RETRIEVE:
                should_overwrite = 0;
                break;
            default:
                printf("Error: Unknown operation code %d.\n", req.request.img_op);
                return NULL;
        }

        clock_gettime(CLOCK_MONOTONIC, &req.completion_time);

        /* Handle image overwriting or adding new image */
        if (should_overwrite) {
            if (req.request.overwrite) {
                free(input_image->pixels);
                free(input_image);
                params->image_array->images[req.request.img_id] = output_image;
            } else {
                // Handle non-overwrite case (not implemented in this code)
            }
        }

        /* Send response to client */
        resp.req_id = req.request.req_id;
        resp.img_id = req.request.img_id;
        resp.ack = RESP_COMPLETED;

        send(params->conn_socket, &resp, sizeof(struct response), 0);

        if (req.request.img_op == IMG_RETRIEVE) {
            sendImage(input_image, params->conn_socket);
        }

        /* Log the processing status */
        synchronized_printf("T%d R%ld:%lf,%s,%d,%lu,%lu,%lf,%lf,%lf\n",
                            params->worker_id,
                            req.request.req_id,
                            TSPEC_TO_DOUBLE(req.request.req_timestamp),
                            OPCODE_TO_STRING(req.request.img_op),
                            req.request.overwrite,
                            client_img_id,
                            server_img_id,
                            TSPEC_TO_DOUBLE(req.receipt_time),
                            TSPEC_TO_DOUBLE(req.start_time),
                            TSPEC_TO_DOUBLE(req.completion_time));

        queue_dump_status(params->queue);
    }

    return NULL;
}

/* Function to control worker threads */
int control_workers(enum worker_command cmd, size_t worker_count,
                    struct worker_params *common_params) {
    static pthread_t *worker_threads = NULL;
    static struct worker_params **worker_params_array = NULL;
    static int *worker_thread_ids = NULL;

    if (cmd == WORKERS_START) {
        size_t i;

        worker_threads = malloc(worker_count * sizeof(pthread_t));
        worker_params_array = malloc(worker_count * sizeof(struct worker_params *));
        worker_thread_ids = malloc(worker_count * sizeof(int));

        if (!worker_threads || !worker_params_array || !worker_thread_ids) {
            ERROR_INFO();
            perror("Unable to allocate arrays for threads.");
            return EXIT_FAILURE;
        }

        for (i = 0; i < worker_count; ++i) {
            worker_thread_ids[i] = -1;

            worker_params_array[i] = malloc(sizeof(struct worker_params));

            if (!worker_params_array[i]) {
                ERROR_INFO();
                perror("Unable to allocate memory for thread.");
                return EXIT_FAILURE;
            }

            worker_params_array[i]->conn_socket = common_params->conn_socket;
            worker_params_array[i]->queue = common_params->queue;
            worker_params_array[i]->image_array = common_params->image_array;
            worker_params_array[i]->worker_done = 0;
            worker_params_array[i]->worker_id = i;
        }

        for (i = 0; i < worker_count; ++i) {
            worker_thread_ids[i] = pthread_create(&worker_threads[i], NULL, worker_main, worker_params_array[i]);

            if (worker_thread_ids[i] < 0) {
                ERROR_INFO();
                perror("Unable to start thread.");
                return EXIT_FAILURE;
            } else {
                printf("INFO: Worker thread %ld (TID = %d) started!\n", i, worker_thread_ids[i]);
            }
        }
    } else if (cmd == WORKERS_STOP) {
        size_t i;

        if (!worker_threads || !worker_params_array || !worker_thread_ids) {
            return EXIT_FAILURE;
        }

        for (i = 0; i < worker_count; ++i) {
            if (worker_thread_ids[i] < 0) {
                continue;
            }

            worker_params_array[i]->worker_done = 1;
        }

        for (i = 0; i < worker_count; ++i) {
            if (worker_thread_ids[i] < 0) {
                continue;
            }

            sem_post(queue_notify);
        }

        for (i = 0; i < worker_count; ++i) {
            pthread_join(worker_threads[i], NULL);
            printf("INFO: Worker thread exited.\n");
        }

        for (i = 0; i < worker_count; ++i) {
            free(worker_params_array[i]);
        }

        free(worker_threads);
        worker_threads = NULL;

        free(worker_params_array);
        worker_params_array = NULL;

        free(worker_thread_ids);
        worker_thread_ids = NULL;
    } else {
        ERROR_INFO();
        perror("Invalid thread control command.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* Function to handle client connection */
void handle_connection(int conn_socket, struct connection_params conn_params) {
    struct request_meta *req;
    struct queue *queue;
    size_t bytes_received;

    struct worker_params common_worker_params;
    int result;

    struct dynamic_array *image_array;

    queue = malloc(sizeof(struct queue));
    queue_initialize(queue, conn_params.queue_size, conn_params.policy);

    image_array = malloc(sizeof(struct dynamic_array));
    dynamic_array_init(image_array, 10);

    common_worker_params.conn_socket = conn_socket;
    common_worker_params.queue = queue;
    common_worker_params.image_array = image_array;

    result = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

    if (result != EXIT_SUCCESS) {
        free(queue);
        control_workers(WORKERS_STOP, conn_params.workers, NULL);
        return;
    }

    req = malloc(sizeof(struct request_meta));

    do {
        bytes_received = recv(conn_socket, &req->request, sizeof(struct request), 0);
        clock_gettime(CLOCK_MONOTONIC, &req->receipt_time);

        if (bytes_received > 0) {
            struct response resp;

            if (req->request.img_op == IMG_REGISTER) {
            uint64_t current_img_id = image_array->count;
            struct image *img = recvImage(conn_socket);
            dynamic_array_add(image_array, img);

            if (!result) {
                resp.img_id = current_img_id;
                resp.req_id = req->request.req_id;
                resp.ack = RESP_COMPLETED;
                send(conn_socket, &resp, sizeof(struct response), 0);

                // Log the server-assigned img_id instead of the client-provided img_id
                synchronized_printf("T%lu R%ld:%lf,%s,%d,%lu,%lu,%lf,%lf,%lf\n",
                                    conn_params.workers,
                                    req->request.req_id,
                                    TSPEC_TO_DOUBLE(req->request.req_timestamp),
                                    OPCODE_TO_STRING(req->request.img_op),
                                    req->request.overwrite,
                                    resp.img_id, // Use server-assigned img_id
                                    resp.img_id, // Use server-assigned img_id
                                    TSPEC_TO_DOUBLE(req->receipt_time),
                                    TSPEC_TO_DOUBLE(req->start_time),
                                    TSPEC_TO_DOUBLE(req->completion_time));
            }
            next_img_id++;
        } else {
                result = queue_add(*req, queue);

                if (result) {
                    resp.req_id = req->request.req_id;
                    resp.ack = RESP_REJECTED;
                    send(conn_socket, &resp, sizeof(struct response), 0);

                    synchronized_printf("X%ld:%lf,%lf,%lf\n", req->request.req_id,
                                        TSPEC_TO_DOUBLE(req->request.req_timestamp),
                                        TSPEC_TO_DOUBLE(req->request.req_length),
                                        TSPEC_TO_DOUBLE(req->receipt_time));
                }
            }
        }
    } while (bytes_received > 0);

    control_workers(WORKERS_STOP, conn_params.workers, NULL);

    dynamic_array_free(image_array);
    free(req);
    shutdown(conn_socket, SHUT_RDWR);
    close(conn_socket);
    printf("INFO: Client disconnected.\n");
}

/* Main function */
int main(int argc, char **argv) {
    int sockfd, retval, accepted_socket, optval, opt;
    in_port_t port_number;
    struct sockaddr_in server_addr, client_addr;
    struct in_addr any_addr;
    socklen_t client_len;
    struct connection_params conn_params;

    conn_params.queue_size = 0;
    conn_params.policy = QUEUE_FIFO;
    conn_params.workers = 1;

    while ((opt = getopt(argc, argv, "q:w:p:")) != -1) {
        switch (opt) {
            case 'q':
                conn_params.queue_size = strtol(optarg, NULL, 10);
                printf("INFO: setting queue size = %ld\n", conn_params.queue_size);
                break;
            case 'w':
                conn_params.workers = strtol(optarg, NULL, 10);
                printf("INFO: setting worker count = %ld\n", conn_params.workers);
                if (conn_params.workers != 1) {
                    ERROR_INFO();
                    fprintf(stderr, "Only 1 worker is supported in this implementation!\n" USAGE_MESSAGE, argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'p':
                if (!strcmp(optarg, "FIFO")) {
                    conn_params.policy = QUEUE_FIFO;
                } else {
                    ERROR_INFO();
                    fprintf(stderr, "Invalid queue policy.\n" USAGE_MESSAGE, argv[0]);
                    return EXIT_FAILURE;
                }
                printf("INFO: setting queue policy = %s\n", optarg);
                break;
            default:
                fprintf(stderr, USAGE_MESSAGE, argv[0]);
        }
    }

    if (!conn_params.queue_size) {
        ERROR_INFO();
        fprintf(stderr, USAGE_MESSAGE, argv[0]);
        return EXIT_FAILURE;
    }

    if (optind < argc) {
        port_number = strtol(argv[optind], NULL, 10);
        printf("INFO: setting server port as: %d\n", port_number);
    } else {
        ERROR_INFO();
        fprintf(stderr, USAGE_MESSAGE, argv[0]);
        return EXIT_FAILURE;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        ERROR_INFO();
        perror("Unable to create socket");
        return EXIT_FAILURE;
    }

    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

    any_addr.s_addr = htonl(INADDR_ANY);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr = any_addr;

    retval = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));

    if (retval < 0) {
        ERROR_INFO();
        perror("Unable to bind socket");
        return EXIT_FAILURE;
    }

    retval = listen(sockfd, BACKLOG_LIMIT);

    if (retval < 0) {
        ERROR_INFO();
        perror("Unable to listen on socket");
        return EXIT_FAILURE;
    }

    printf("INFO: Waiting for incoming connection...\n");
    client_len = sizeof(struct sockaddr_in);
    accepted_socket = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);

    if (accepted_socket == -1) {
        ERROR_INFO();
        perror("Unable to accept connections");
        return EXIT_FAILURE;
    }

    printf_mutex = malloc(sizeof(sem_t));
    retval = sem_init(printf_mutex, 0, 1);
    if (retval < 0) {
        ERROR_INFO();
        perror("Unable to initialize printf mutex");
        return EXIT_FAILURE;
    }

    queue_mutex = malloc(sizeof(sem_t));
    queue_notify = malloc(sizeof(sem_t));
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

    handle_connection(accepted_socket, conn_params);

    free(queue_mutex);
    free(queue_notify);

    close(sockfd);
    return EXIT_SUCCESS;
}
