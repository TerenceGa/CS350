#!/bin/bash

# Variables for server and client commands
SERVER_CMD="./build/server_img_perf -q 1000 -h INSTR 3333"
CLIENT_CMD="./client -a 30 -I images/ -n 1000 3333"

# Log files
SERVER_LOG="runexpserver_3.txt"
CLIENT_LOG="client_log.txt"

# Start the server in the background and redirect output to server log
$SERVER_CMD > $SERVER_LOG 2>&1 &
SERVER_PID=$!

echo "Server started with PID $SERVER_PID"

# Give the server a moment to start
sleep 2

# Run the client and redirect output to client log
$CLIENT_CMD > $CLIENT_LOG 2>&1

# Wait for the client to finish
wait

echo "Client finished execution."

# Terminate the server
kill $SERVER_PID

# Wait for the server to exit
wait $SERVER_PID 2>/dev/null

echo "Server terminated."

# Isolate IMG_BLUR and IMG_SHARPEN results from the server log
grep -E "IMG_BLUR|IMG_SHARPEN" $SERVER_LOG > blur_sharpen_results.txt

echo "Results for IMG_BLUR and IMG_SHARPEN operations have been saved to blur_sharpen_results.txt"
