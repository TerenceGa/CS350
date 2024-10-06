# log_analysis_updated.py

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import re
import sys
import numpy as np
from scipy import stats

def parse_log_line(line):
    """
    Parses a single log line starting with 'R' and extracts the fields.
    Returns a dictionary with the extracted data.
    """
    # Regular expression to match the R lines
    match = re.match(r'R(\d+):([\d\.]+),([\d\.]+),([\d\.]+),([\d\.]+),([\d\.]+)', line)
    if match:
        request_id = int(match.group(1))
        sent_timestamp = float(match.group(2))
        request_length = float(match.group(3))
        receipt_timestamp = float(match.group(4))
        start_time = float(match.group(5))
        completion_time = float(match.group(6))
        return {
            'RequestID': request_id,
            'SentTimestamp': sent_timestamp,
            'RequestLength': request_length,
            'ReceiptTimestamp': receipt_timestamp,
            'StartTime': start_time,
            'CompletionTime': completion_time
        }
    else:
        return None

def read_log_file(file_path):
    """
    Reads the log file and extracts data from lines starting with 'R'.
    Returns a pandas DataFrame with the extracted data.
    """
    data = []
    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if line.startswith('R'):
                parsed = parse_log_line(line)
                if parsed:
                    data.append(parsed)
    df = pd.DataFrame(data)
    return df

def calculate_inter_arrival_times(df):
    """
    Calculates inter-arrival times based on ReceiptTimestamp.
    Adds a new column 'InterArrivalTime' to the DataFrame.
    """
    df = df.sort_values('ReceiptTimestamp').reset_index(drop=True)
    df['InterArrivalTime'] = df['ReceiptTimestamp'].diff()
    return df

def plot_inter_arrival_times(data):
    """
    Plots a histogram and density plot for inter-arrival times.
    Saves the plot to 'inter_arrival_times.png'.
    """
    plt.figure(figsize=(10, 6))
    sns.histplot(data, bins=50, kde=True, color='skyblue', edgecolor='black')
    plt.title('Inter-Arrival Times Distribution', fontsize=16)
    plt.xlabel('Inter-Arrival Time (seconds)', fontsize=14)
    plt.ylabel('Frequency', fontsize=14)
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.tight_layout()
    plt.savefig('inter_arrival_times.png')
    plt.close()
    print("Inter-arrival times plot saved as 'inter_arrival_times.png'")

def plot_service_times(df):
    """
    Plots the service times distribution.
    Decides whether to use a histogram, bar plot, or box plot based on data variance.
    Saves the plot accordingly.
    """
    service_length = df['RequestLength']
    variance = service_length.var()
    print(f"Service Times Variance: {variance}")

    if variance < 1e-6:
        # Deterministic - use bar plot or box plot
        # Bar Plot
        plt.figure(figsize=(6, 4))
        sns.countplot(x='RequestLength', data=df, color='skyblue', edgecolor='black')
        plt.title('Service Times Distribution (Deterministic)', fontsize=16)
        plt.xlabel('Service Time (seconds)', fontsize=14)
        plt.ylabel('Frequency', fontsize=14)
        plt.grid(True, linestyle='--', alpha=0.6)
        plt.tight_layout()
        plt.savefig('service_times_bar.png')
        plt.close()
        print("Service times bar plot saved as 'service_times_bar.png'")
    else:
        # Variable - use histogram
        plt.figure(figsize=(10, 6))
        sns.histplot(service_length, bins=50, kde=True, color='skyblue', edgecolor='black')
        plt.title('Service Times Distribution', fontsize=16)
        plt.xlabel('Service Time (seconds)', fontsize=14)
        plt.ylabel('Frequency', fontsize=14)
        plt.grid(True, linestyle='--', alpha=0.6)
        plt.tight_layout()
        plt.savefig('service_times.png')
        plt.close()
        print("Service times plot saved as 'service_times.png'")

def print_summary_statistics(df):
    print("\n--- Summary Statistics ---")
    print(df[['InterArrivalTime', 'RequestLength']].describe())

def main():
    if len(sys.argv) != 2:
        print("Usage: python log_analysis_updated.py <path_to_log_file>")
        sys.exit(1)

    log_file = sys.argv[1]
    print(f"Reading log file: {log_file}")

    # Step 1: Read and parse the log file
    df = read_log_file(log_file)
    if df.empty:
        print("No valid 'R' lines found in the log file.")
        sys.exit(1)
    print(f"Total requests parsed: {len(df)}")

    # Step 2: Calculate inter-arrival times
    df = calculate_inter_arrival_times(df)
    # Drop the first row which has NaN for InterArrivalTime
    df = df.dropna(subset=['InterArrivalTime']).reset_index(drop=True)
    print("Inter-arrival times calculated.")

    # Step 3: Plot Inter-Arrival Times
    plot_inter_arrival_times(df['InterArrivalTime'])

    # Step 4: Plot Service Times
    plot_service_times(df)

    # Step 5: Print Summary Statistics
    print_summary_statistics(df)

    # Optional: Save the processed data to a CSV for further analysis
    df.to_csv('processed_log_data_updated.csv', index=False)
    print("Processed data saved to 'processed_log_data_updated.csv'.")

if __name__ == "__main__":
    main()

