import os
import re
import pandas as pd
import sys

def compute_metrics_from_log(log_path):
    """
    Parses a single cleaned experiment log to compute utilization, average response time,
    and average queue length. Additionally, collects individual response times.

    Parameters:
        log_path (str): Path to the cleaned experiment log file.

    Returns:
        tuple:
            utilization (float): Proportion of time the server was busy.
            average_response_time (float): Mean response time in seconds.
            average_queue_length (float): Time-weighted average queue size.
            response_times (list of float): List of individual response times.
    """
    # Regular expressions to match log lines
    QUEUE_REGEX = re.compile(r'^Q:\[(.*?)\]')
    RESP_REGEX = re.compile(r'^R\d+:(\d+\.\d+),(\d+\.\d+),.*')

    queue_snapshots = []
    response_times = []

    last_r_timestamp = None

    with open(log_path, 'r') as file:
        for line in file:
            line = line.strip()

            # Match R line to get timestamp and response time
            resp_match = RESP_REGEX.match(line)
            if resp_match:
                timestamp = float(resp_match.group(1))
                response_time = float(resp_match.group(2))
                response_times.append(response_time)
                last_r_timestamp = timestamp
                continue

            # Match queue snapshot
            queue_match = QUEUE_REGEX.match(line)
            if queue_match and last_r_timestamp is not None:
                queue_content = queue_match.group(1)
                if queue_content:
                    queue_size = len(queue_content.split(','))
                else:
                    queue_size = 0
                queue_snapshots.append((last_r_timestamp, queue_size))
                # Reset last_r_timestamp to avoid associating multiple Q lines with one R line
                last_r_timestamp = None
                continue

    # Compute Utilization and Average Queue Length
    if len(queue_snapshots) < 2:
        utilization = 0.0
        average_queue_length = 0.0
    else:
        # Sort snapshots by timestamp
        queue_snapshots.sort(key=lambda x: x[0])

        total_time = 0.0
        busy_time = 0.0
        weighted_sum = 0.0

        for i in range(len(queue_snapshots) - 1):
            current_time, current_size = queue_snapshots[i]
            next_time, _ = queue_snapshots[i + 1]
            interval = next_time - current_time

            if interval < 0:
                continue  # Skip invalid intervals

            total_time += interval
            if current_size > 0:
                busy_time += interval

            weighted_sum += current_size * interval

        if total_time > 0:
            utilization = busy_time / total_time
            average_queue_length = weighted_sum / total_time
        else:
            utilization = 0.0
            average_queue_length = 0.0

    # Compute Average Response Time
    if response_times:
        average_response_time = sum(response_times) / len(response_times)
    else:
        average_response_time = 0.0

    return utilization, average_response_time, average_queue_length, response_times

def main():
    # Define the directory containing cleaned experiment logs
    LOG_DIR = 'clean_logs'  # Updated to use 'clean_logs' directory

    if not os.path.isdir(LOG_DIR):
        print(f"Error: Directory '{LOG_DIR}' does not exist.")
        sys.exit(1)

    # Prepare a list to store metrics for each experiment
    metrics_list = []

    # Iterate through each cleaned experiment log
    for filename in os.listdir(LOG_DIR):
        if filename.startswith('experiment_a') and filename.endswith('.log'):
            a_value_matches = re.findall(r'experiment_a(\d+)\.log', filename)
            if not a_value_matches:
                print(f"Warning: Filename '{filename}' does not match expected pattern.")
                continue
            a_value = int(a_value_matches[0])
            log_path = os.path.join(LOG_DIR, filename)

            print(f"Processing {filename}...")

            try:
                utilization, average_response_time, average_queue_length, response_times = compute_metrics_from_log(log_path)
                print(f"Metrics for a={a_value}: Utilization={utilization:.4f}, "
                      f"Avg Response Time={average_response_time:.6f}, "
                      f"Avg Queue Length={average_queue_length:.4f}")
            except Exception as e:
                print(f"Error processing {filename}: {e}")
                continue

            # Append metrics to the list
            metrics_list.append({
                'a': a_value,
                'utilization': utilization,
                'average_response_time': average_response_time,
                'average_queue_length': average_queue_length
            })

            # Write individual response times to a separate log file (optional)
            response_log_filename = f"response_times_a{a_value}.log"
            response_log_path = os.path.join(LOG_DIR, response_log_filename)
            try:
                with open(response_log_path, 'w') as resp_log_file:
                    for idx, resp_time in enumerate(response_times):
                        resp_log_file.write(f"Request {idx}: {resp_time}\n")
                print(f"Individual response times for a={a_value} saved to '{response_log_filename}'.")
            except Exception as e:
                print(f"Error writing response times for a={a_value}: {e}")

    if not metrics_list:
        print("No metrics to aggregate. Please check your log files.")
        sys.exit(1)

    # Create a DataFrame from the metrics list
    metrics_df = pd.DataFrame(metrics_list)

    # Sort the DataFrame by 'a'
    metrics_df.sort_values(by='a', inplace=True)

    # Save the aggregated metrics to a CSV file
    metrics_df.to_csv('aggregated_results.csv', index=False)

    print("Aggregated metrics saved to 'aggregated_results.csv'.")

if __name__ == "__main__":
    main()



