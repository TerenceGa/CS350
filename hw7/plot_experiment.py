import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import re
import os
from sklearn.linear_model import LinearRegression
import numpy as np

# Configuration
CLIENT_LOG = "client_log.txt"    # Path to the client log file
SERVER_LOG = "server_log.txt"    # Path to the server log file
OUTPUT_PLOT = "instruction_vs_request_length.png"  # Output plot filename
EXCLUDED_OPERATION = "IMG_REGISTER"  # Operation to exclude from plotting (if any)
SCALE_INSTRUCTION_COUNT = True  # Whether to scale Instruction_Count for better visualization

def parse_client_log(file_path):
    """
    Parses the client log file and extracts Request_ID, Opcode, Sent, and Recv timestamps.

    Args:
        file_path (str): Path to the client log file.

    Returns:
        pandas.DataFrame: DataFrame containing the extracted data.
    """
    data = {
        "Request_ID": [],
        "Opcode": [],
        "Sent": [],
        "Recv": []
    }

    # Regular expression to match client log lines
    client_log_pattern = re.compile(
        r"\[#CLIENT#\]\s+R\[(\d+)\]:\s+Sent:\s+([\d.]+)\s+Recv:\s+([\d.]+)\s+Opcode:\s+(\w+)"
    )

    with open(file_path, 'r') as file:
        for line in file:
            match = client_log_pattern.search(line)
            if match:
                request_id = int(match.group(1))
                sent = float(match.group(2))
                recv = float(match.group(3))
                opcode = match.group(4)

                data["Request_ID"].append(request_id)
                data["Opcode"].append(opcode)
                data["Sent"].append(sent)
                data["Recv"].append(recv)

    df = pd.DataFrame(data)
    # Calculate Request Length as difference between Recv and Sent
    df["Request_Length"] = df["Recv"] - df["Sent"]
    return df

def parse_server_log(file_path):
    """
    Parses the server log file and extracts Request_ID, Opcode, and Instruction_Count.

    Only lines containing 'INSTR' are considered.

    Args:
        file_path (str): Path to the server log file.

    Returns:
        pandas.DataFrame: DataFrame containing the extracted data.
    """
    data = {
        "Request_ID": [],
        "Opcode": [],
        "Instruction_Count": []
    }

    # Regular expression to match server log lines containing 'INSTR'
    server_log_pattern = re.compile(
        r"T\d+\s+R(\d+):[\d.]+,(\w+),.*?,INSTR,(\d+)"
    )

    with open(file_path, 'r') as file:
        for line in file:
            match = server_log_pattern.search(line)
            if match:
                request_id = int(match.group(1))
                opcode = match.group(2)
                instruction_count = int(match.group(3))

                data["Request_ID"].append(request_id)
                data["Opcode"].append(opcode)
                data["Instruction_Count"].append(instruction_count)

    df = pd.DataFrame(data)
    return df

def merge_logs(client_df, server_df):
    """
    Merges client and server DataFrames on Request_ID and Opcode.

    Args:
        client_df (pandas.DataFrame): DataFrame from client log.
        server_df (pandas.DataFrame): DataFrame from server log.

    Returns:
        pandas.DataFrame: Merged DataFrame containing Request_Length and Instruction_Count.
    """
    merged_df = pd.merge(client_df, server_df, on=["Request_ID", "Opcode"], how="inner")
    return merged_df

def plot_data(merged_df, output_file, excluded_op=None, scale_instruction=False):
    """
    Generates scatter plots of Instruction_Count vs. Request_Length for each Opcode.

    Args:
        merged_df (pandas.DataFrame): Merged DataFrame containing necessary data.
        output_file (str): Filename for the saved plot.
        excluded_op (str, optional): Opcode to exclude from plotting.
        scale_instruction (bool): Whether to scale Instruction_Count for better visualization.
    """
    # Exclude specified operation if provided
    if excluded_op:
        merged_df = merged_df[merged_df["Opcode"].str.upper() != excluded_op.upper()]

    if merged_df.empty:
        print("No data available after excluding the specified operation.")
        return

    # Optional: Scale Instruction_Count
    if scale_instruction:
        merged_df["Instruction_Count_Scaled"] = merged_df["Instruction_Count"] / 1e6  # Scale to millions
        x_label = "Instruction Count (Millions)"
        x_column = "Instruction_Count_Scaled"
    else:
        x_label = "Instruction Count"
        x_column = "Instruction_Count"

    # Get unique operations
    operations = merged_df["Opcode"].unique()
    num_operations = len(operations)

    # Determine subplot layout (3 plots per row)
    plots_per_row = 3
    num_rows = (num_operations + plots_per_row - 1) // plots_per_row

    # Set the visual style
    sns.set(style="whitegrid")

    # Create subplots
    fig, axes = plt.subplots(num_rows, plots_per_row, figsize=(plots_per_row * 6, num_rows * 5))
    axes = axes.flatten()  # Flatten in case of multiple rows

    for idx, operation in enumerate(operations):
        ax = axes[idx]
        op_data = merged_df[merged_df["Opcode"] == operation]

        # Scatter plot
        sns.scatterplot(
            x=x_column,
            y="Request_Length",
            data=op_data,
            ax=ax,
            color='blue',
            alpha=0.6
        )

        # Linear regression
        X = op_data[x_column].values.reshape(-1, 1)
        y = op_data["Request_Length"].values
        if len(X) > 1:
            model = LinearRegression()
            model.fit(X, y)
            y_pred = model.predict(X)
            ax.plot(op_data[x_column], y_pred, color='red', label='Best Fit Line')
            slope = model.coef_[0]
            intercept = model.intercept_
            r_squared = model.score(X, y)
            ax.set_title(f"{operation}\nSlope: {slope:.6f}, Intercept: {intercept:.6f}, R²: {r_squared:.2f}")
        else:
            ax.set_title(f"{operation}\nInsufficient data for regression")

        ax.set_xlabel(x_label)
        ax.set_ylabel("Request Length (s)")
        ax.legend()

    # Hide any unused subplots
    for j in range(idx + 1, len(axes)):
        fig.delaxes(axes[j])

    plt.tight_layout()
    plt.savefig(output_file)
    print(f"Plots saved to '{output_file}'")
    plt.show()

def main():
    # Check if log files exist
    if not os.path.exists(CLIENT_LOG):
        print(f"Client log file '{CLIENT_LOG}' not found.")
        return
    if not os.path.exists(SERVER_LOG):
        print(f"Server log file '{SERVER_LOG}' not found.")
        return

    # Parse logs
    print("Parsing client log...")
    client_df = parse_client_log(CLIENT_LOG)
    print(f"Client log parsed: {len(client_df)} entries found.")

    print("Parsing server log...")
    server_df = parse_server_log(SERVER_LOG)
    print(f"Server log parsed: {len(server_df)} entries found.")

    # Merge logs
    print("Merging client and server data...")
    merged_df = merge_logs(client_df, server_df)
    print(f"Merged data contains {len(merged_df)} entries.")

    if merged_df.empty:
        print("No matching entries found between client and server logs.")
        return

    # Print number of data points per Opcode
    print("\nNumber of data points per Opcode:")
    print(merged_df['Opcode'].value_counts())

    # Generate plots
    print("\nGenerating plots...")
    plot_data(merged_df, OUTPUT_PLOT, EXCLUDED_OPERATION, SCALE_INSTRUCTION_COUNT)

if __name__ == "__main__":
    main()
