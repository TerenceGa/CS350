#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Define the range for the -a parameter
START_A=1
END_A=15

# Define the server and client commands
SERVER_CMD="./server_q 2222"
CLIENT_CMD_TEMPLATE="./client -a {a} -s 15 -n 1000 2222"

# Create a directory to store logs
LOG_DIR="experiment_logs"
mkdir -p $LOG_DIR

# Loop through the specified range of -a values
for ((a=START_A; a<=END_A; a++))
do
    echo "=============================="
    echo "Running experiment with -a $a"
    echo "=============================="

    # Define log file for this experiment
    EXP_LOG="$LOG_DIR/experiment_a${a}.log"

    # Start the server in the background, redirecting output to the experiment log file
    $SERVER_CMD > "$EXP_LOG" 2>&1 &
    SERVER_PID=$!

    # Allow the server some time to initialize
    sleep 2

    # Prepare the client command by replacing {a} with the current value
    CLIENT_CMD=${CLIENT_CMD_TEMPLATE//\{a\}/$a}

    # Run the client, appending its output to the same experiment log file
    $CLIENT_CMD >> "$EXP_LOG" 2>&1

    # Wait for the server to finish processing
    wait $SERVER_PID

    echo "Experiment with -a $a completed. Log saved to $EXP_LOG."
done

echo "All experiments completed. Logs saved in $LOG_DIR."

# Optional: Transfer logs to PC automatically
# Uncomment and configure the following lines if you wish to automate log transfer

# PC_USER="your_pc_username"
# PC_IP="your_pc_ip_address"
# PC_DEST_PATH="/path/on/pc/"

# echo "Transferring logs to PC..."
# scp -r "$LOG_DIR" "$PC_USER@$PC_IP:$PC_DEST_PATH/"
# echo "Logs transferred to PC."
