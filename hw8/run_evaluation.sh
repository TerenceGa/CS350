#!/bin/bash

# =============================================================================
# Script: run_evaluation.sh
# Description:
#     Automates the execution of the server and client applications for EVAL
#     Problem 1. It performs 10 runs, each time increasing the number of
#     Mid section repetitions in the client SCRIPT. The runtime for each run
#     is measured and logged for analysis.
#
# Usage:
#     ./run_evaluation.sh
#
# Notes:
#     - Ensure that server_mimg and client executables are in the current directory.
#     - The images directory should contain all required BMP images.
#     - Requires /usr/bin/time and gnuplot for runtime measurement and plotting.
# =============================================================================

# Exit immediately if a command exits with a non-zero status
set -e

# Define the number of runs
TOTAL_RUNS=10

# Define the server command template
SERVER_CMD="/usr/bin/time -v ./server_mimg -q 1500 -w 1 -p FIFO 2222"
# Define the client command template
CLIENT_CMD="./client 2222 -I ./images/ -L"

# Define Intro, Mid, and Outro sections
INTRO="0:R:1:6,0:R:1:7,0:R:1:8,0:R:1:9,0:R:1:10,0:R:1:11,0:R:1:12,0:R:1:13,0:R:1:14,0.5:R:1:15,"

# Define the Mid section as a multi-line string for readability
MID="0:r:1:0,0:b:1:0,0:s:1:0,0:v:1:0,0:h:1:0,0:r:1:0,0:b:1:0,0:s:1:0,0:v:1:0,0:h:1:0,\
0:r:1:1,0:b:1:1,0:s:1:1,0:v:1:1,0:h:1:1,0:r:1:1,0:b:1:1,0:s:1:1,0:v:1:1,0:h:1:1,\
0:r:1:2,0:b:1:2,0:s:1:2,0:v:1:2,0:h:1:2,0:r:1:2,0:b:1:2,0:s:1:2,0:v:1:2,0:h:1:2,\
0:r:1:3,0:b:1:3,0:s:1:3,0:v:1:3,0:h:1:3,0:r:1:3,0:b:1:3,0:s:1:3,0:v:1:3,0:h:1:3,\
0:r:1:4,0:b:1:4,0:s:1:4,0:v:1:4,0:h:1:4,0:r:1:4,0:b:1:4,0:s:1:4,0:v:1:4,0:h:1:4,\
0:r:1:5,0:b:1:5,0:s:1:5,0:v:1:5,0:h:1:5,0:r:1:5,0:b:1:5,0:s:1:5,0:v:1:5,0:h:1:5,\
0:r:1:6,0:b:1:6,0:s:1:6,0:v:1:6,0:h:1:6,0:r:1:6,0:b:1:6,0:s:1:6,0:v:1:6,0:h:1:6,\
0:r:1:7,0:b:1:7,0:s:1:7,0:v:1:7,0:h:1:7,0:r:1:7,0:b:1:7,0:s:1:7,0:v:1:7,0:h:1:7,\
0:r:1:8,0:b:1:8,0:s:1:8,0:v:1:8,0:h:1:8,0:r:1:8,0:b:1:8,0:s:1:8,0:v:1:8,0:h:1:8,\
0:r:1:9,0:b:1:9,0:s:1:9,0:v:1:9,0:h:1:9,0:r:1:9,0:b:1:9,0:s:1:9,0:v:1:9,0:h:1:9,"

OUTRO="0:T:1:0,0:T:1:1,0:T:1:2,0:T:1:3,0:T:1:4,0:T:1:5,0:T:1:6,0:T:1:7,0:T:1:8,0:T:1:9"

# Initialize or clear the runtime summary file
SUMMARY_FILE="runtime_summary.csv"
echo "Run_Number,Elapsed_Time(s)" > "$SUMMARY_FILE"

# Function to generate SCRIPT based on Intro, Mid repeated n times, and Outro
generate_script() {
    local run_number=$1
    local script=""
    script+="$INTRO"
    for ((i=1; i<=run_number; i++)); do
        script+="$MID"
    done
    script+="$OUTRO"
    echo "$script"
}

# Function to extract elapsed wall-clock time from time output
extract_elapsed_time() {
    local time_file=$1
    # Extract the line containing "Elapsed (wall clock) time"
    local elapsed_line
    elapsed_line=$(grep "Elapsed (wall clock) time" "$time_file" || true)
    
    if [[ -z "$elapsed_line" ]]; then
        echo "N/A"
    else
        # Extract the time value (e.g., 0:05.23)
        echo "$elapsed_line" | awk -F': ' '{print $2}'
    fi
}

# Start the evaluation runs
for run in $(seq 1 "$TOTAL_RUNS"); do
    echo "====================="
    echo "Starting Run #$run"
    echo "====================="
    
    # Generate the SCRIPT for this run
    SCRIPT=$(generate_script "$run")
    
    # Save the SCRIPT to a temporary file
    SCRIPT_FILE="script_run_${run}.txt"
    echo -n "$SCRIPT" > "$SCRIPT_FILE"
    
    echo "Generated SCRIPT for Run #$run: $SCRIPT_FILE"
    
    # Start the server with /usr/bin/time and redirect time output to a log file
    TIME_LOG="time_run_${run}.txt"
    # Start the server in the background
    # Redirect both stdout and stderr to /dev/null to prevent clutter
    /usr/bin/time -v -o "$TIME_LOG" ./server_mimg -q 1500 -w 1 -p FIFO 2222 &> /dev/null &
    SERVER_PID=$!
    
    echo "Started server with PID $SERVER_PID for Run #$run."
    
    # Give the server a moment to start up
    sleep 2
    
    # Run the client with the generated SCRIPT
    CLIENT_LOG="client_run_${run}.log"
    ./client 2222 -I ./images/ -L "$SCRIPT_FILE" > "$CLIENT_LOG" 2>&1 &
    CLIENT_PID=$!
    
    echo "Started client with PID $CLIENT_PID for Run #$run."
    
    # Wait for the client to finish
    wait "$CLIENT_PID"
    echo "Client for Run #$run completed."
    
    # Wait for the server to finish
    wait "$SERVER_PID"
    echo "Server for Run #$run exited."
    
    # Extract the elapsed time from the time log
    ELAPSED_TIME=$(extract_elapsed_time "$TIME_LOG")
    echo "Run #$run Elapsed Time: $ELAPSED_TIME seconds."
    
    # Append the result to the summary file
    echo "$run,$ELAPSED_TIME" >> "$SUMMARY_FILE"
    
    # Optional: Clean up temporary SCRIPT and logs
    rm -f "$SCRIPT_FILE" "$CLIENT_LOG"
    
    echo "Run #$run completed and logged."
    echo ""
done

echo "All runs completed. Summary:"
cat "$SUMMARY_FILE"

# Optional: Generate a plot using gnuplot
PLOT_SCRIPT="plot_runtime.gnuplot"
cat << EOF > "$PLOT_SCRIPT"
set terminal png size 800,600
set output 'runtime_plot.png'
set title 'Server Runtime vs. Number of Mid Repetitions'
set xlabel 'Run Number (Number of Mid Repetitions)'
set ylabel 'Elapsed Time (seconds)'
set grid
set datafile separator ","
plot '$SUMMARY_FILE' using 1:2 with linespoints title 'Runtime'
EOF

echo "Generating runtime plot with gnuplot..."
gnuplot "$PLOT_SCRIPT"
echo "Plot saved as runtime_plot.png."

# Clean up the gnuplot script
rm -f "$PLOT_SCRIPT"

echo "Evaluation completed. See 'runtime_summary.csv' and 'runtime_plot.png' for results."
