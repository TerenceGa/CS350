#!/bin/bash

# ======================================================================
# Script: run_experiments.sh
# Description: Automates the execution of two server-client experiments,
#              capturing logs for each run.
# ======================================================================

# ------------------------------ Variables ------------------------------

# Server and client command templates
SERVER_CMD="./build/server_img -q 100 2222"
CLIENT_CMD="./client"

# Connection parameters
PORT=2222
ARRIVAL_RATE=30
NUM_REQUESTS=1000

# Image directories
IMAGES_SMALL="images_small/"
IMAGES_ALL="images_all/"

# Log directory and file names
LOG_DIR="logs"
RUN1_SERVER_LOG="$LOG_DIR/run1_server.log"
RUN1_CLIENT_LOG="$LOG_DIR/run1_client.log"
RUN2_SERVER_LOG="$LOG_DIR/run2_server.log"
RUN2_CLIENT_LOG="$LOG_DIR/run2_client.log"

# Ensure the log directory exists
mkdir -p "$LOG_DIR"

# ---------------------------- Functions -------------------------------

# Function to run a single experiment
run_experiment() {
    local run_num=$1
    local image_dir=$2
    local server_log=$3
    local client_log=$4

    echo "============================================================"
    echo "Starting RUN${run_num} with image directory: $image_dir"
    echo "------------------------------------------------------------"

    # Start the server in the background, redirecting output to server_log
    $SERVER_CMD > "$server_log" 2>&1 &
    SERVER_PID=$!
    echo "Server started with PID $SERVER_PID. Logging to $server_log"

    # Wait for the server to initialize
    sleep 2

    # Run the client and redirect output to client_log
    echo "Running client with parameters: -a $ARRIVAL_RATE -I $image_dir -n $NUM_REQUESTS $PORT"
    $CLIENT_CMD -a "$ARRIVAL_RATE" -I "$image_dir" -n "$NUM_REQUESTS" "$PORT" > "$client_log" 2>&1
    echo "Client completed. Logging to $client_log"

    # Shutdown the server gracefully
    echo "Shutting down server with PID $SERVER_PID"
    kill "$SERVER_PID"

    # Wait for the server process to terminate
    wait "$SERVER_PID" 2>/dev/null
    echo "Server with PID $SERVER_PID has been terminated."
    echo "============================================================"
    echo ""
}

# ------------------------------ Execution -----------------------------

# Run 1: Using images_small/ directory
run_experiment 1 "$IMAGES_SMALL" "$RUN1_SERVER_LOG" "$RUN1_CLIENT_LOG"

# Run 2: Using images_all/ directory
run_experiment 2 "$IMAGES_ALL" "$RUN2_SERVER_LOG" "$RUN2_CLIENT_LOG"

# Completion message
echo "All experiments completed successfully."
echo "Logs are available in the '$LOG_DIR' directory:"
echo " - RUN1 Server Log: $RUN1_SERVER_LOG"
echo " - RUN1 Client Log: $RUN1_CLIENT_LOG"
echo " - RUN2 Server Log: $RUN2_SERVER_LOG"
echo " - RUN2 Client Log: $RUN2_CLIENT_LOG"
