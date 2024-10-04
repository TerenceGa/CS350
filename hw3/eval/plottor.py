import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Replace 'server_log_d1_20231004_123456.csv' with your actual log file
log_file = 'server_log_d1_20231004_123456.csv'

# Load the log file
df = pd.read_csv(log_file)

# Assuming the log has columns: RequestID, SentTimestamp, RequestLength, ReceiptTimestamp, StartTime, CompletionTime

# Calculate Inter-Arrival Times
df['InterArrival'] = df['ReceiptTimestamp'].diff()

# Plot Inter-Arrival Times
plt.figure(figsize=(10, 6))
sns.histplot(df['InterArrival'].dropna(), bins=50, kde=True)
plt.title('Inter-Arrival Times Distribution')
plt.xlabel('Time (seconds)')
plt.ylabel('Frequency')
plt.show()

# Plot Service Times
plt.figure(figsize=(10, 6))
sns.histplot(df['RequestLength'], bins=50, kde=True)
plt.title('Service Times Distribution')
plt.xlabel('Time (seconds)')
plt.ylabel('Frequency')
plt.show()
