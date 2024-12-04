#!/usr/bin/env python3

import matplotlib.pyplot as plt
import numpy as np

# Define the log file name
log_file = 'blur_sharpen_results.txt'

# Initialize lists to store event counts
blur_counts = []
sharpen_counts = []

# Open and read the log file
with open(log_file, 'r') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue  # Skip empty lines

        # Split the line into fields
        # The format is specified as:
        # T<thread ID> R<req. ID>:<sent ts>,<img_op>,<overwrite>,<client img_id>,<server img_id>,<receipt ts>,<start ts>,<compl. ts>,<event name>,<event count>
        try:
            # Split on space to separate T<thread ID> and the rest
            thread_part, rest = line.split(' ', 1)
            # Split on ':' to separate R<req. ID> and the rest
            req_part, rest = rest.split(':', 1)
            # Now split the rest of the line by commas
            fields = rest.split(',')
            # Extract fields
            sent_ts = float(fields[0])
            img_op = fields[1]
            overwrite = int(fields[2])
            client_img_id = int(fields[3])
            server_img_id = int(fields[4])
            receipt_ts = float(fields[5])
            start_ts = float(fields[6])
            compl_ts = float(fields[7])
            event_name = fields[8]
            event_count = int(fields[9])

            # Check if event_name matches the desired event (e.g., L1MISS)
            # Since your data has 'INSTR', adjust accordingly
            # if event_name != 'L1MISS':
            #     continue  # Skip events that are not L1MISS

            # For this example, we'll use 'INSTR'
            if event_name != 'INSTR':
                continue  # Skip events that are not INSTR

            # Collect counts based on the operation
            if img_op == 'IMG_BLUR':
                blur_counts.append(event_count)
            elif img_op == 'IMG_SHARPEN':
                sharpen_counts.append(event_count)
            else:
                continue  # Skip other operations if any

        except Exception as e:
            print(f"Error parsing line: {line}")
            print(e)
            continue  # Skip malformed lines

# Now, we have two datasets: blur_counts and sharpen_counts

# Determine appropriate bin size
# You can adjust the number of bins or bin edges based on your data
# For example, let's use 20 bins between the min and max counts

# Combine both datasets to get overall min and max for binning
all_counts = blur_counts + sharpen_counts
min_count = min(all_counts)
max_count = max(all_counts)

# Create bins
num_bins = 20
bins = np.linspace(min_count, max_count, num_bins + 1)

# Plot the histograms
plt.figure(figsize=(10, 6))

# Normalize the counts by setting density=True
plt.hist(blur_counts, bins=bins, alpha=0.5, label='IMG_BLUR', density=True, edgecolor='black')
plt.hist(sharpen_counts, bins=bins, alpha=0.5, label='IMG_SHARPEN', density=True, edgecolor='black')

# Add labels and title
plt.xlabel('Event Count (e.g., L1MISS or INSTR)')
plt.ylabel('Normalized Frequency')
plt.title('Distribution of Event Counts for IMG_BLUR and IMG_SHARPEN Operations')
plt.legend()

# Show grid
plt.grid(axis='y', alpha=0.75)

# Show the plot
plt.show()

# Optionally, save the plot to a file
# plt.savefig('event_count_distribution.png')
