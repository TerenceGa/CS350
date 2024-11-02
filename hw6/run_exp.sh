#!/bin/bash

# Paths to the server and client executables
SERVER="./build/server_img"
CLIENT="./client"
SERVER_PORT=2222

# First experiment parameters
QUEUE_SIZE=100
CLIENT_ARRIVAL_RATE=30
CLIENT_IMAGE_DIR="images_small/"
CLIENT_NUM_REQUESTS=1000

# Second experiment parameters
CLIENT_IMAGE_DIR_ALL="images_all/"

# Output files
SERVER_LOG1="server_run1.log"
CLIENT_LOG1="client_run1.log"

SERVER_LOG2="server_run2.log"
CLIENT_LOG2="client_run2.log"

# Function to start the server
start_server() {
    echo "Starting server..."
    # Kill any existing server processes
    pkill -f server_img
    sleep 1
    $SERVER -q $QUEUE_SIZE $SERVER_PORT > $1 2>&1 &
    SERVER_PID=$!
    echo "Server started with PID $SERVER_PID"
}

# Function to run the client
run_client() {
    echo "Running client..."
    $CLIENT -a $CLIENT_ARRIVAL_RATE -I $1 -n $CLIENT_NUM_REQUESTS $SERVER_PORT > $2 2>&1
    echo "Client finished."
}

# Function to stop the server
stop_server() {
    echo "Stopping server..."
    kill -9 $SERVER_PID
    wait $SERVER_PID 2>/dev/null
    sleep 2  # Wait for sockets to close
    echo "Server stopped."
}

# Run first experiment
echo "Running first experiment with images_small..."
start_server $SERVER_LOG1
sleep 1  # Give server time to start
run_client $CLIENT_IMAGE_DIR $CLIENT_LOG1
stop_server

# Run second experiment
echo "Running second experiment with images_all..."
start_server $SERVER_LOG2
sleep 1  # Give server time to start
run_client $CLIENT_IMAGE_DIR_ALL $CLIENT_LOG2
stop_server

echo "Experiments completed."
