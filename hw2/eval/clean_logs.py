import os
import re
import sys

def clean_log_file(input_path, output_path):
    """
    Reads a log file, removes lines starting with 'INFO:', and writes the cleaned lines to a new file.

    Parameters:
        input_path (str): Path to the original log file.
        output_path (str): Path to save the cleaned log file.
    """
    try:
        with open(input_path, 'r') as infile, open(output_path, 'w') as outfile:
            for line in infile:
                if not line.startswith('INFO:'):
                    outfile.write(line)
        print(f"Cleaned log saved to '{output_path}'.")
    except Exception as e:
        print(f"Error processing '{input_path}': {e}")

def main():
    # Define directories
    LOG_DIR = 'experiment_logs'
    CLEAN_LOG_DIR = 'clean_logs'

    # Check if the log directory exists
    if not os.path.isdir(LOG_DIR):
        print(f"Error: Directory '{LOG_DIR}' does not exist.")
        sys.exit(1)

    # Create the clean log directory if it doesn't exist
    if not os.path.isdir(CLEAN_LOG_DIR):
        os.makedirs(CLEAN_LOG_DIR)
        print(f"Created directory '{CLEAN_LOG_DIR}' for cleaned logs.")

    # Define the pattern to identify experiment log files
    log_pattern = re.compile(r'^experiment_a\d+\.log$')

    # Iterate through each file in the log directory
    for filename in os.listdir(LOG_DIR):
        if log_pattern.match(filename):
            input_path = os.path.join(LOG_DIR, filename)
            output_path = os.path.join(CLEAN_LOG_DIR, filename)
            print(f"Processing '{filename}'...")
            clean_log_file(input_path, output_path)

    print("All log files have been cleaned and saved in the 'clean_logs' directory.")

if __name__ == "__main__":
    main()
