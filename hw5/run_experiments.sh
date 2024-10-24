#!/bin/bash

# Number of runs for each experiment
NUM_RUNS=10

# Server parameters
WORKERS=2
QUEUE_SIZE=100
PORT=2222

# Client parameters
NUM_REQUESTS=1500
SERVICE_TIME=20

# Utilization levels
LOW_UTILIZATION_ARRIVAL=10
HIGH_UTILIZATION_ARRIVAL=40

# Policies
POLICIES=("FIFO" "SJN")

# Output file for results
OUTPUT_FILE="experiment_results.txt"

# Function to run experiments
run_experiment() {
    local utilization=$1
    local arrival_rate=$2

    echo "Running experiments at $utilization utilization with arrival rate $arrival_rate ms" | tee -a $OUTPUT_FILE
    echo "------------------------------------------------------------" | tee -a $OUTPUT_FILE

    for policy in "${POLICIES[@]}"; do
        echo "Policy: $policy" | tee -a $OUTPUT_FILE
        runtimes=()

        for ((i=1; i<=NUM_RUNS; i++)); do
            echo "Run $i/$NUM_RUNS"

            # Start the server in the background and redirect stderr to capture /usr/bin/time output
            /usr/bin/time -v ./server_pol -w $WORKERS -q $QUEUE_SIZE -p $policy $PORT 2> time_output.txt &
            SERVER_PID=$!

            # Give the server a moment to start
            sleep 1

            # Run the client
            ./client -a $arrival_rate -s $SERVICE_TIME -n $NUM_REQUESTS $PORT

            # Wait for the server to finish
            wait $SERVER_PID

            # Extract the Elapsed (wall clock) time
            elapsed_time=$(grep "Elapsed (wall clock) time" time_output.txt | awk '{print $8}')
            runtimes+=($elapsed_time)
            echo "Elapsed Time: $elapsed_time" | tee -a $OUTPUT_FILE

            # Clean up
            rm time_output.txt
        done

        # Compute the average runtime
        total_time=0
        for time in "${runtimes[@]}"; do
            # Convert HH:MM:SS or MM:SS to seconds
            IFS=: read -r h m s <<< "$time"
            if [ -z "$s" ]; then
                s=$m
                m=$h
                h=0
            fi
            total_seconds=$(echo "$h * 3600 + $m * 60 + $s" | bc)
            total_time=$(echo "$total_time + $total_seconds" | bc)
        done
        average_time=$(echo "scale=2; $total_time / ${#runtimes[@]}" | bc)
        echo "Average Elapsed Time for $policy: $average_time seconds" | tee -a $OUTPUT_FILE
        echo "" | tee -a $OUTPUT_FILE
    done
}

# Clear previous results
> $OUTPUT_FILE

# Run experiments for low utilization
run_experiment "Low" $LOW_UTILIZATION_ARRIVAL

# Run experiments for high utilization
run_experiment "High" $HIGH_UTILIZATION_ARRIVAL

echo "Experiments completed. Results are saved in $OUTPUT_FILE."
