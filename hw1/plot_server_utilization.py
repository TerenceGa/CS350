import matplotlib.pyplot as plt

# Data
arrival_rate = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]
cpu_utilization = [7, 15, 23, 31, 39, 47, 55, 63, 71, 79, 86, 94]

# Plotting
plt.figure(figsize=(10, 6))
plt.plot(arrival_rate, cpu_utilization, marker='o', linestyle='-')
plt.title('Server Utilization vs. Arrival Rate')
plt.xlabel('Arrival Rate (-a parameter)')
plt.ylabel('Server Utilization (%)')
plt.grid(True)
plt.show()
