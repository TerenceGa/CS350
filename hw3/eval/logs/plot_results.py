import os
import re
import matplotlib.pyplot as plt

# Data structures to store results
data = {
    'd0': {},
    'd1': {}
}

# Service time is 0.05 seconds (from request_length in the logs)
service_time = 0.05

# Regular expression to parse filenames
filename_regex = re.compile(r'server_log_d(?P<distribution>\d+)_arr(?P<arrival_rate>\d+)_.*')

# Get list of all log files in the current directory
log_files = [f for f in os.listdir('.') if f.startswith('server_log')]

for log_file in log_files:
    match = filename_regex.match(log_file)
    if match:
        distribution = 'd' + match.group('distribution')
        arrival_rate = int(match.group('arrival_rate'))

        # Initialize data structures if not already done
        if arrival_rate not in data[distribution]:
            data[distribution][arrival_rate] = {
                'response_times': [],
                'utilization': arrival_rate * service_time
            }

        # Read the log file
        with open(log_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('R'):
                    # Parse the line
                    # Format: R<request_id>:<sent_timestamp>,<request_length>,<receipt_timestamp>,<start_time>,<completion_time>
                    try:
                        request_id_part, times_part = line.split(':')
                        request_id = int(request_id_part[1:])  # Skip the 'R' character
                        times = times_part.split(',')
                        sent_timestamp = float(times[0])
                        request_length = float(times[1])  # Should be 0.05
                        receipt_timestamp = float(times[2])
                        start_time = float(times[3])
                        completion_time = float(times[4])

                        # Compute response time
                        response_time = completion_time - sent_timestamp

                        # Append to response_times list
                        data[distribution][arrival_rate]['response_times'].append(response_time)
                    except Exception as e:
                        print(f"Error parsing line in {log_file}: {line}")
                        print(e)
                        continue

# Now, compute average response times and prepare data for plotting
arrival_rates = sorted(set(arr for dist in data.values() for arr in dist.keys()))
utilizations = [rate * service_time for rate in arrival_rates]

avg_response_times_d0 = []
avg_response_times_d1 = []

for arrival_rate in arrival_rates:
    # Distribution d0
    if arrival_rate in data['d0']:
        response_times = data['d0'][arrival_rate]['response_times']
        avg_response_time = sum(response_times) / len(response_times)
        avg_response_times_d0.append(avg_response_time)
    else:
        avg_response_times_d0.append(None)

    # Distribution d1
    if arrival_rate in data['d1']:
        response_times = data['d1'][arrival_rate]['response_times']
        avg_response_time = sum(response_times) / len(response_times)
        avg_response_times_d1.append(avg_response_time)
    else:
        avg_response_times_d1.append(None)

# Plotting
plt.figure(figsize=(10, 6))
plt.plot(utilizations, avg_response_times_d0, 'o-', label='Distribution 0')
plt.plot(utilizations, avg_response_times_d1, 's-', label='Distribution 1')
plt.xlabel('Server Utilization')
plt.ylabel('Average Response Time (seconds)')
plt.title('Average Response Time vs Server Utilization')
plt.legend()
plt.grid(True)
plt.show()


