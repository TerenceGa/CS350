#!/bin/bash

# ======================================================================
# Script: run_experiments.sh
# Description: Automates the execution of multiple server-client experiments,
#              capturing logs for each run with different optimization levels.
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

# Optimization levels to test
OPT_LEVELS=("O0" "O1" "O2")

# Makefile path (assuming it's in the current directory)
MAKEFILE_PATH="Makefile"

# Ensure the log directory exists
mkdir -p "$LOG_DIR"

# ---------------------------- Functions -------------------------------

# Function to prepare images_small/ directory with only test1.bmp and test2.bmp
prepare_images_small() {
    local source_dir="images_all/"
    local target_dir="$IMAGES_SMALL"
    echo "Preparing images_small/ directory with test1.bmp and test2.bmp..."
    mkdir -p "$target_dir"
    rm -f "$target_dir"*.bmp  # Remove existing BMP files
    cp "$source_dir"test1.bmp "$source_dir"test2.bmp "$target_dir"
    echo "images_small/ prepared."
}

# Function to modify the optimization level in the Makefile
set_optimization_level() {
    local opt_level=$1
    echo "Setting optimization level to -$opt_level in $MAKEFILE_PATH..."
    
    # Backup the original Makefile if not already backed up
    if [ ! -f "${MAKEFILE_PATH}.bak" ]; then
        cp "$MAKEFILE_PATH" "${MAKEFILE_PATH}.bak"
        echo "Backup of Makefile created at ${MAKEFILE_PATH}.bak"
    fi

    # Use sed to replace the optimization flag
    sed -i "s/-O[0-3]/-O$opt_level/g" "$MAKEFILE_PATH"

    # Verify the change
    grep "LDFLAGS" "$MAKEFILE_PATH"
    echo "Optimization level set to -$opt_level."
}

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

    # Check if server is still running
    if ! ps -p $SERVER_PID > /dev/null; then
        echo "Server failed to start. Check $server_log for details."
        exit 1
    fi

    # Run the client and redirect output to client_log
    echo "Running client with parameters: -a $ARRIVAL_RATE -I $image_dir -n $NUM_REQUESTS $PORT"
    $CLIENT_CMD -a "$ARRIVAL_RATE" -I "$image_dir" -n "$NUM_REQUESTS" "$PORT" > "$client_log" 2>&1
    CLIENT_EXIT_CODE=$?
    echo "Client completed. Logging to $client_log"

    if [ $CLIENT_EXIT_CODE -ne 0 ]; then
        echo "Client encountered an error. Check $client_log for details."
    fi

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
prepare_images_small
run_experiment 1 "$IMAGES_SMALL" "$RUN1_SERVER_LOG" "$RUN1_CLIENT_LOG"

# Run 2: Using images_all/ directory with default optimization (-O0)
run_experiment 2 "$IMAGES_ALL" "$RUN2_SERVER_LOG" "$RUN2_CLIENT_LOG"

# Additional Runs with Different Optimization Levels
for opt in "${OPT_LEVELS[@]}"; do
    if [ "$opt" != "O0" ]; then
        # Set the desired optimization level
        set_optimization_level "$opt"

        # Recompile the server and libraries
        echo "Recompiling the server and libraries with -$opt..."
        make clean
        make
        echo "Recompilation with -$opt completed."

        # Define new log file names for each optimization level
        OPT_SERVER_LOG="$LOG_DIR/run2_${opt}_server.log"
        OPT_CLIENT_LOG="$LOG_DIR/run2_${opt}_client.log"

        # Run Run2 again with the same parameters
        run_experiment "2_${opt}" "$IMAGES_ALL" "$OPT_SERVER_LOG" "$OPT_CLIENT_LOG"
    fi
done

# Restore the original Makefile after all optimizations
echo "Restoring the original Makefile from backup..."
mv "${MAKEFILE_PATH}.bak" "$MAKEFILE_PATH"
echo "Makefile restored."

# Completion message
echo "All experiments completed successfully."
echo "Logs are available in the '$LOG_DIR' directory:"
echo " - RUN1 Server Log: $RUN1_SERVER_LOG"
echo " - RUN1 Client Log: $RUN1_CLIENT_LOG"
echo " - RUN2 Server Log: $RUN2_SERVER_LOG"
echo " - RUN2 Client Log: $RUN2_CLIENT_LOG"
for opt in "${OPT_LEVELS[@]}"; do
    if [ "$opt" != "O0" ]; then
        echo " - RUN2_${opt} Server Log: $LOG_DIR/run2_${opt}_server.log"
        echo " - RUN2_${opt} Client Log: $LOG_DIR/run2_${opt}_client.log"
    fi
done
