import pandas as pd
import matplotlib.pyplot as plt
import sys
import os

def main():
    # Define the path to the aggregated results CSV
    csv_path = 'aggregated_results.csv'
    
    # Check if the CSV file exists
    if not os.path.exists(csv_path):
        print(f"Error: '{csv_path}' does not exist in the current directory.")
        sys.exit(1)
    
    # Read the aggregated results into a DataFrame
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading '{csv_path}': {e}")
        sys.exit(1)
    
    # Validate required columns
    required_columns = {'a', 'utilization', 'average_response_time', 'average_queue_length'}
    if not required_columns.issubset(df.columns):
        print(f"Error: CSV file must contain the following columns: {required_columns}")
        sys.exit(1)
    
    # Sort the DataFrame by 'utilization' for a smoother plot
    df = df.sort_values(by='utilization')
    
    # Extract data
    utilizations = df['utilization']
    avg_response_times = df['average_response_time']
    avg_queue_lengths = df['average_queue_length']
    
    # Create a figure and a primary axis
    fig, ax1 = plt.subplots(figsize=(12, 8))
    
    # Plot Average Response Time on the primary y-axis
    color_response = 'tab:blue'
    ax1.set_xlabel('Server Utilization (Fraction)', fontsize=12)
    ax1.set_ylabel('Average Response Time (s)', color=color_response, fontsize=12)
    line1, = ax1.plot(utilizations, avg_response_times, marker='o', linestyle='-', color=color_response, label='Average Response Time')
    ax1.tick_params(axis='y', labelcolor=color_response)
    ax1.tick_params(axis='x', labelsize=10)
    
    # Create a secondary y-axis for Average Queue Length
    ax2 = ax1.twinx()
    color_queue = 'tab:red'
    ax2.set_ylabel('Average Queue Length', color=color_queue, fontsize=12)
    line2, = ax2.plot(utilizations, avg_queue_lengths, marker='s', linestyle='--', color=color_queue, label='Average Queue Length')
    ax2.tick_params(axis='y', labelcolor=color_queue)
    
    # Combine legends from both axes
    lines = [line1, line2]
    labels = [line.get_label() for line in lines]
    ax1.legend(lines, labels, loc='upper left', fontsize=12)
    
    # Add title and grid
    plt.title('Average Response Time and Queue Length vs Server Utilization', fontsize=14)
    plt.grid(True, which='both', linestyle='--', linewidth=0.5, alpha=0.7)
    
    # Adjust layout for better spacing
    fig.tight_layout()
    
    # Save the plot as an image file
    output_plot = 'metrics_dual_axis_plot.png'
    plt.savefig(output_plot)
    print(f"Plot saved as '{output_plot}'.")
    
    # Display the plot
    plt.show()

if __name__ == "__main__":
    main()

