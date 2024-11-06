import argparse
import re
import matplotlib.pyplot as plt
import numpy as np
import os
import sys

def parse_log_file(log_file):
    """
    Parses the log file to extract latencies for each image operation.

    Parameters:
        log_file (str): Path to the log file.

    Returns:
        list of float: List of latencies in milliseconds.
    """
    latencies = []
    # Enhanced regex pattern to match log lines with optional HASH
    log_pattern = re.compile(
        r'\[#CLIENT#\]\s+R\[\d+\]:\s+Sent:\s+([\d\.]+)\s+Recv:\s+([\d\.]+)\s+Opcode:\s+\S+\s+OW:\s+\d+\s+ClientImgID:\s+\d+\s+ServerImgID:\s+\d+\s+Rejected:\s+\S+(?:\s+HASH:\s+\w+)?'
    )

    matched_lines = 0
    total_lines = 0

    with open(log_file, 'r') as f:
        for line_number, line in enumerate(f, 1):
            total_lines += 1
            match = log_pattern.search(line)
            if match:
                sent_time = float(match.group(1))
                recv_time = float(match.group(2))
                latency = (recv_time - sent_time) * 1000  # Convert to milliseconds
                if latency < 0:
                    print(f"Warning: Negative latency at line {line_number} in {log_file}. Skipping.")
                    continue
                latencies.append(latency)
                matched_lines += 1
            else:
                # Handle lines that do not match the expected format
                print(f"Warning: Line {line_number} in {log_file} does not match the expected format.")
                continue

    print(f"Parsed {matched_lines} out of {total_lines} lines from {log_file}.")
    return latencies

def compute_cdf(latencies):
    """
    Computes the empirical CDF of the latencies.

    Parameters:
        latencies (list of float): List of latencies in milliseconds.

    Returns:
        tuple of (np.ndarray, np.ndarray): Sorted latencies and their corresponding CDF values.
    """
    sorted_latencies = np.sort(latencies)
    cdf = np.arange(1, len(sorted_latencies)+1) / len(sorted_latencies)
    return sorted_latencies, cdf

def plot_cdf(sorted_latencies, cdf, avg_latency, p99_latency, optimization_level, output_dir):
    """
    Plots the CDF of latencies and annotates average and 99th percentile latencies.

    Parameters:
        sorted_latencies (np.ndarray): Sorted latencies.
        cdf (np.ndarray): CDF values.
        avg_latency (float): Average latency.
        p99_latency (float): 99th percentile latency.
        optimization_level (str): Optimization level label (e.g., '-O0').
        output_dir (str): Directory to save the plot.
    """
    plt.figure(figsize=(10, 7))
    plt.step(sorted_latencies, cdf, where='post', label=f'CDF - {optimization_level}')
    plt.xlabel('Latency (ms)', fontsize=14)
    plt.ylabel('Cumulative Probability', fontsize=14)
    plt.title(f'CDF of Image Operation Latencies ({optimization_level})', fontsize=16)
    plt.grid(True)

    # Annotate average latency
    plt.axvline(avg_latency, color='red', linestyle='--', label=f'Average: {avg_latency:.2f} ms')
    plt.text(avg_latency, 0.5, f'Avg: {avg_latency:.2f} ms', color='red', rotation=90,
             verticalalignment='center', horizontalalignment='right', fontsize=12)

    # Annotate 99th percentile latency
    plt.axvline(p99_latency, color='green', linestyle='--', label=f'99th Percentile: {p99_latency:.2f} ms')
    plt.text(p99_latency, 0.99, f'99%: {p99_latency:.2f} ms', color='green', rotation=90,
             verticalalignment='bottom', horizontalalignment='right', fontsize=12)

    plt.legend(fontsize=12)
    plt.tight_layout()

    # Save the plot
    output_path = os.path.join(output_dir, f'cdf_{optimization_level}.png')
    plt.savefig(output_path)
    plt.close()
    print(f"Plot saved to {output_path}")

def main():
    parser = argparse.ArgumentParser(description="Generate CDF plots for image operation latencies.")
    parser.add_argument(
        '--log',
        action='append',
        required=True,
        help="Specify log files with their optimization levels in the format LEVEL=FILE. "
             "Example: --log O0=log_O0.txt --log O1=log_O1.txt --log O2=log_O2.txt"
    )
    parser.add_argument(
        '--output-dir',
        default='.',
        help="Directory to save the generated plots. Defaults to the current directory."
    )
    args = parser.parse_args()

    # Create output directory if it doesn't exist
    if not os.path.isdir(args.output_dir):
        os.makedirs(args.output_dir)
        print(f"Created output directory: {args.output_dir}")

    optimization_logs = {}
    for log_entry in args.log:
        if '=' not in log_entry:
            print(f"Error: Invalid log format '{log_entry}'. Expected LEVEL=FILE.")
            sys.exit(1)
        level, file_path = log_entry.split('=', 1)
        if not os.path.isfile(file_path):
            print(f"Error: Log file '{file_path}' does not exist.")
            sys.exit(1)
        optimization_logs[level] = file_path

    if not optimization_logs:
        print("Error: No log files provided.")
        sys.exit(1)

    for level, log_file in optimization_logs.items():
        print(f"\nProcessing log for optimization level {level}: {log_file}")
        latencies = parse_log_file(log_file)
        if not latencies:
            print(f"No valid latencies found in {log_file}. Skipping plot generation.")
            continue

        sorted_latencies, cdf = compute_cdf(latencies)
        avg_latency = np.mean(latencies)
        p99_latency = np.percentile(latencies, 99)

        print(f"Optimization Level {level}:")
        print(f"  Number of operations: {len(latencies)}")
        print(f"  Average Latency: {avg_latency:.2f} ms")
        print(f"  99th Percentile Latency: {p99_latency:.2f} ms")

        plot_cdf(sorted_latencies, cdf, avg_latency, p99_latency, level, args.output_dir)

if __name__ == "__main__":
    main()

