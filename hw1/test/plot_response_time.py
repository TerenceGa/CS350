import matplotlib.pyplot as plt

# Average response times (ms) for a_values from 1 to 12
average_response_time = [
    86.8661,   # a_value = 1
    95.6635,   # a_value = 2
    104.7672,  # a_value = 3
    115.0954,  # a_value = 4
    128.6175,  # a_value = 5
    145.1660,  # a_value = 6
    168.4155,  # a_value = 7
    197.7283,  # a_value = 8
    250.1889,  # a_value = 9
    328.7268,  # a_value = 10
    442.3273,  # a_value = 11
    826.5613   # a_value = 12
]

# CPU utilization percentages for a_values from 1 to 12
cpu_utilization = [
    7,   # a_value = 1
    15,  # a_value = 2
    23,  # a_value = 3
    31,  # a_value = 4
    39,  # a_value = 5
    47,  # a_value = 6
    55,  # a_value = 7
    63,  # a_value = 8
    71,  # a_value = 9
    79,  # a_value = 10
    86,  # a_value = 11
    94   # a_value = 12
]

plt.figure(figsize=(10, 6))
plt.plot(cpu_utilization, average_response_time, marker='o', linestyle='-')
plt.title('Average Response Time vs. Server Utilization')
plt.xlabel('Server Utilization (%)')
plt.ylabel('Average Response Time (ms)')
plt.grid(True)
plt.show()
