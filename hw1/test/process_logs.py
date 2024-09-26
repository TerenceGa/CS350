import re
import numpy as np
import glob

# Initialize data structures
results = []

# Pattern to match all server log files
log_files = sorted(glob.glob('server_log_a*.txt'))

for log_file in log_files:
    # Extract the 'a' value from the filename
    match_a_value = re.search(r'server_log_a(\d+)\.txt', log_file)
    if match_a_value:
        a_value = int(match_a_value.group(1))
    else:
        continue  # Skip files that don't match the pattern

    print(f'Processing log file for a={a_value}: {log_file}')

    # Initialize a list to store response times
    response_times = []

    # Open and read the log file
    with open(log_file, 'r') as file:
        for line in file:
            line = line.strip()
            # Check if the line represents a request entry
            if line.startswith('R'):
                # Adjusted regex to allow for spaces
                match = re.match(r'R\d+:\s*(\d+\.\d+),\s*(\d+\.\d+),\s*(\d+\.\d+),\s*(\d+\.\d+)', line)
                if match:
                    number1 = float(match.group(1))
                    number2 = float(match.group(2))
                    number3 = float(match.group(3))
                    number4 = float(match.group(4))

                    # Calculate response time: number4 - number1
                    response_time = number4 - number1

                    # Append to the list
                    response_times.append(response_time)
                else:
                    print(f"Line format incorrect: {line}")

    # Convert response_times to a NumPy array
    response_times = np.array(response_times)

    # Compute statistics
    if response_times.size > 0:
        average_response_time = np.mean(response_times)
        max_response_time = np.max(response_times)
        min_response_time = np.min(response_times)
        std_deviation = np.std(response_times)
    else:
        average_response_time = max_response_time = min_response_time = std_deviation = 0

    # Store the results
    results.append({
        'a_value': a_value,
        'average_response_time': average_response_time,
        'max_response_time': max_response_time,
        'min_response_time': min_response_time,
        'std_deviation': std_deviation,
        'num_requests': response_times.size
    })

# Sort the results by 'a_value'
results.sort(key=lambda x: x['a_value'])

# Print a header
print('\nSummary of Results:')
print('{:<8} {:<20} {:<20} {:<20} {:<20} {:<15}'.format(
    'a_value', 'Average (ms)', 'Max (ms)', 'Min (ms)', 'Std Dev (ms)', 'Num Requests'))

# Print the results
for res in results:
    print('{:<8} {:<20.4f} {:<20.4f} {:<20.4f} {:<20.4f} {:<15}'.format(
        res['a_value'],
        res['average_response_time'] * 1000,
        res['max_response_time'] * 1000,
        res['min_response_time'] * 1000,
        res['std_deviation'] * 1000,
        res['num_requests']
    ))
