import re
import matplotlib.pyplot as plt

# Initialize counters and lists
total_requests = 0
rejected_requests = 0
rejection_times = []

# Regular expression pattern for the report lines
report_line_pattern = re.compile(
    r'\[#CLIENT#\] R\[(\d+)\]: Sent: ([\d\.]+) Recv: ([\d\.]+) Exp: ([\d\.]+) Len: ([\d\.]+) Rejected: (Yes|No)'
)

# Name of your log file
log_file = 'server_lim_log2.txt'  # Replace with your actual filename

# Flag to indicate when we've reached the report section
in_report_section = False

# Read and process the log file
with open(log_file, 'r') as f:
    for line in f:
        line = line.strip()
        
        # Check if we've reached the report section
        if '==== REPORT ====' in line:
            in_report_section = True
            continue  # Skip the report header line

        if in_report_section:
            # Parse the report lines
            report_match = report_line_pattern.match(line)
            if report_match:
                total_requests += 1
                request_id = int(report_match.group(1))
                sent_timestamp = float(report_match.group(2))
                recv_timestamp = float(report_match.group(3))
                exp_timestamp = float(report_match.group(4))
                length = float(report_match.group(5))
                rejected = report_match.group(6)

                if rejected == 'Yes':
                    rejected_requests += 1
                    rejection_times.append(sent_timestamp)
            else:
                # Ignore lines that don't match the report pattern
                continue

# Calculate the ratio of rejected requests over the total
if total_requests > 0:
    rejection_ratio = rejected_requests / total_requests
else:
    rejection_ratio = 0

print(f"Total Requests: {total_requests}")
print(f"Rejected Requests: {rejected_requests}")
print(f"Rejection Ratio: {rejection_ratio:.2%}")

# Calculate inter-rejection times
inter_rejection_times = []
if len(rejection_times) > 1:
    # Sort the rejection times to ensure chronological order
    rejection_times.sort()
    inter_rejection_times = [
        t2 - t1 for t1, t2 in zip(rejection_times[:-1], rejection_times[1:])
    ]

# Plot the distribution of inter-rejection times
if inter_rejection_times:
    plt.figure(figsize=(10, 6))
    plt.hist(inter_rejection_times, bins=30, edgecolor='black')
    plt.xlabel('Inter-Rejection Time (seconds)')
    plt.ylabel('Frequency')
    plt.title('Distribution of Inter-Rejection Times')
    plt.grid(True)
    plt.show()
else:
    print("Not enough rejection events to plot inter-rejection times.")
