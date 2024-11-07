#!/bin/bash

# Filename: run_experiment.sh

# Configuration
SERVER_CMD="./build/server_img_perf -q 1000 -h INSTR 2222"
CLIENT_CMD="./client -a 30 -I images/ -n 1000 2222"
SERVER_LOG="server_log.txt"
CLIENT_LOG="client_log.txt"

# Optional: Adjust parameters if machine is weak
# Uncomment the following lines to reduce the number of requests
# CLIENT_CMD="./build/client -a 15 -I images/ -n 500 2222"

# Clean previous logs if they exist
echo "Cleaning previous logs..."
rm -f $SERVER_LOG $CLIENT_LOG

# Start the server in the background and redirect its output
echo "Starting server..."
$SERVER_CMD > $SERVER_LOG 2>&1 &
SERVER_PID=$!
echo "Server started with PID $SERVER_PID"

# Give the server a moment to start
sleep 2

# Run the client and redirect its output
echo "Running client..."
$CLIENT_CMD > $CLIENT_LOG 2>&1

# Optionally, you can append timestamps or additional information here
# For example:
# echo "----- Server Log -----" > $SERVER_LOG
# cat server_output.log >> $SERVER_LOG
# echo "----- Client Log -----" > $CLIENT_LOG
# cat client_output.log >> $CLIENT_LOG

# Clean up: Terminate the server
echo "Terminating server..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo "Experiment completed. Server logs saved to $SERVER_LOG and client logs saved to $CLIENT_LOG"
