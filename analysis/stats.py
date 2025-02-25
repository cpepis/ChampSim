import os
import re
import math
import pandas as pd

import warnings
warnings.simplefilter("ignore", FutureWarning)

def group_by(df, string):
    """
    Groups the DataFrame by the specified string.
    """
    # Filter based on the index containing the specific pattern
    df = df[df.index.astype(str).str.contains(f"champsim-{string}$", regex=True)].copy()

    # Reset the index to make "Benchmark" a column, then modify it
    df = df.reset_index()
    df["Benchmark"] = df["Benchmark"].str.replace(f"-champsim-{string}", "", regex=True)

    # Set "Benchmark" as the new index again
    df = df.set_index("Benchmark").sort_index()

    return df


def calculate_means(df):
    """
    Calculates the geometric mean of the IPC column and the arithmetic mean
    for all other columns, appending both as separate rows.
    """
    try:
        # Calculate the arithmetic mean for each column except "IPC"
        amean_row = {
            col: (df[col].mean() if col != "IPC" else None) for col in df.columns
        }

        # Calculate the geometric mean of the IPC column
        geomean = math.prod(df["IPC"]) ** (1 / len(df["IPC"]))

        # Prepare the "gmean" row with only the IPC column filled
        gmean_row = {col: (geomean if col == "IPC" else None) for col in df.columns}

        # Append both "amean" and "gmean" rows
        df.loc["amean"] = amean_row
        df.loc["gmean"] = gmean_row

        # Ensure "amean" and "gmean" are at the end of the DataFrame
        df = df.reindex(list(df.index.drop(["amean", "gmean"])) + ["amean", "gmean"])

    except ZeroDivisionError:
        print("Some IPC values are zero, exiting...")
        exit(1)

    return df


def parse_champsim_output(path):
    # Handle directory case
    if os.path.isdir(path):
        all_data = []
        for filename in os.listdir(path):
            filepath = os.path.join(path, filename)
            if os.path.isfile(filepath):
                # Parse each file and add the benchmark name to each row
                df = parse_single_file(filepath)
                benchmark_name = os.path.splitext(filename)[0]  # Extract benchmark name
                df["Benchmark"] = benchmark_name  # Add benchmark column
                all_data.append(df)
        # Concatenate all data into a single DataFrame and set Benchmark as index
        return pd.concat(all_data, ignore_index=True).set_index("Benchmark")

    # Handle single file case
    elif os.path.isfile(path):
        df = parse_single_file(path)
        benchmark_name = os.path.splitext(os.path.basename(path))[0]
        df["Benchmark"] = benchmark_name
        return df.set_index("Benchmark")

    else:
        raise FileNotFoundError(
            f"The path {path} does not exist or is not a valid file/directory."
        )


def parse_single_file(file_path):
    data = {}

    # Define patterns for key-value pairs
    patterns = define_cpu_patterns()
    cache_patterns = define_cache_patterns()

    # Read the file and start parsing after "Region of Interest Statistics"
    with open(file_path, "r") as file:
        lines = file.readlines()

    roi_start = False
    for line in lines:
        if "Region of Interest Statistics" in line:
            roi_start = True
            continue
        if not roi_start:
            continue

        # Stop parsing if "DRAM Statistics" is encountered
        if "DRAM Statistics" in line:
            break

        # Parse each line for the relevant stats
        parse_cpu_patterns(line, patterns, data)
        parse_cache_patterns(line, cache_patterns, data)

    # Convert the data dictionary to a DataFrame
    df = pd.DataFrame([data])
    return df


def define_cpu_patterns():
    """Define regex patterns for capturing cpu statistics."""
    return {
        "IPC": r"cumulative IPC: ([\d.]+)",
        "wrong_path_insts": r"wrong_path_insts: (\d+)",
        "wrong_path_insts_skipped": r"wrong_path_insts_skipped: (\d+)",
        "wrong_path_insts_executed": r"wrong_path_insts_executed: (\d+)",
        "instr_foot_print": r"instr_foot_print: (\d+)",
        "data_foot_print": r"data_foot_print: (\d+)",
        "is_prefetch_insts": r"is_prefetch_insts: (\d+)",
        "is_prefetch_skipped": r"is_prefetch_skipped: (\d+)",
        "Branch_Prediction_Accuracy": r"Branch Prediction Accuracy: ([\d.]+)%",
        "MPKI": r"MPKI: ([\d.]+)",
        "Average_ROB_Occupancy_at_Mispredict": r"Average ROB Occupancy at Mispredict: ([\d.]+)",
        "Loads_Count": r"Loads: Count (\d+)",
        "Loads_Issued": r"Loads: Count \d+ Issued (\d+)",
        "Fetch_Idle_Cycles": r"Fetch Idle Cycles (\d+)",
        "Fetch_Blocked_Cycles": r"Fetch Blocked Cycles (\d+)",
        "IFetch_Failed_Events": r"IFetch Failed Events (\d+)",
        "Fetch_Buffer_Not_Empty": r"Fetch Buffer Not Empty (\d+)",
        "Decode_Idle_Cycles": r"Decode Idle Cycles (\d+)",
        "Dispatch_Idle_Cycles": r"Dispatch Idle Cycles (\d+)",
        "Execute_Idle_Cycles": r"Execute Idle Cycles (\d+)",
        "Execute_None_Cycles": r"Execute None Cycles (\d+)",
        "Execute_Head_Not_Ready_Cycles": r"Execute Head Not Ready Cycles (\d+)",
        "Execute_Head_Not_Completed_Cycles": r"Execute Head Not Completed Cycles (\d+)",
        "Execute_Pending_Cycles": r"Execute Pending Cycles (\d+)",
        "Execute_Load_Blocked_Cycles": r"Execute Load Blocked Cycles (\d+)",
        "Scheduler_Idle_Cycles": r"Scheduler Idle Cycles (\d+)",
        "Scheduler_None_Cycles": r"Scheduler None Cycles (\d+)",
        "ROB_Idle_Cycles": r"ROB Idle Cycles (\d+)",
        "LQ_Full_Events": r"LQ Full Events (\d+)",
        "SQ_Full_Events": r"SQ Full Events (\d+)",
        "Non_Branch_Squashes": r"Non Branch Squashes (\d+)",
        "Loads": r"Loads: (\d+)",
        "Loads_Success": r"Loads Success: (\d+)",
        "Loads_Executed": r"Loads Executed: (\d+)",
        "Loads_Retired": r"Loads Retired: (\d+)",
        "Stores": r"Stores: (\d+)",
        "BRANCH_DIRECT_JUMP": r"BRANCH_DIRECT_JUMP:\s*([\d.eE-]+)",
        "BRANCH_INDIRECT": r"BRANCH_INDIRECT:\s*([\d.eE-]+)",
        "BRANCH_CONDITIONAL": r"BRANCH_CONDITIONAL:\s*([\d.eE-]+)",
        "BRANCH_DIRECT_CALL": r"BRANCH_DIRECT_CALL:\s*([\d.eE-]+)",
        "BRANCH_INDIRECT_CALL": r"BRANCH_INDIRECT_CALL:\s*([\d.eE-]+)",
        "BRANCH_RETURN": r"BRANCH_RETURN:\s*([\d.eE-]+)",
    }


def define_cache_patterns():
    """Define regex patterns for capturing cache metrics."""
    return {
        # LLC metrics
        "LLC_TOTAL": r"LLC TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "LLC_LOAD": r"LLC LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "LLC_RFO": r"LLC RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "LLC_PREFETCH_AHM": r"LLC PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "LLC_WRITE": r"LLC WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "LLC_TRANSLATION": r"LLC TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "LLC_PREFETCH": r"LLC PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "LLC_WRONG_PATH": r"LLC WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "LLC_AVERAGE_MISS_LATENCY": r"LLC AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # DTLB metrics
        "DTLB_TOTAL": r"cpu0_DTLB TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_LOAD": r"cpu0_DTLB LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_RFO": r"cpu0_DTLB RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_PREFETCH_AHM": r"cpu0_DTLB PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_WRITE": r"cpu0_DTLB WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_TRANSLATION": r"cpu0_DTLB TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_PREFETCH": r"cpu0_DTLB PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "DTLB_WRONG_PATH": r"cpu0_DTLB WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "DTLB_AVERAGE_MISS_LATENCY": r"cpu0_DTLB AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # ITLB metrics
        "ITLB_TOTAL": r"cpu0_ITLB TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_LOAD": r"cpu0_ITLB LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_RFO": r"cpu0_ITLB RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_PREFETCH_AHM": r"cpu0_ITLB PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_WRITE": r"cpu0_ITLB WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_TRANSLATION": r"cpu0_ITLB TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_PREFETCH": r"cpu0_ITLB PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "ITLB_WRONG_PATH": r"cpu0_ITLB WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "ITLB_AVERAGE_MISS_LATENCY": r"cpu0_ITLB AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # L1D metrics
        "L1D_TOTAL": r"cpu0_L1D TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_LOAD": r"cpu0_L1D LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_RFO": r"cpu0_L1D RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_PREFETCH_AHM": r"cpu0_L1D PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_WRITE": r"cpu0_L1D WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_TRANSLATION": r"cpu0_L1D TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_PREFETCH": r"cpu0_L1D PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "L1D_WRONG_PATH": r"cpu0_L1D WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "L1D_AVERAGE_MISS_LATENCY": r"cpu0_L1D AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # L1I metrics
        "L1I_TOTAL": r"cpu0_L1I TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_LOAD": r"cpu0_L1I LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_RFO": r"cpu0_L1I RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_PREFETCH_AHM": r"cpu0_L1I PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_WRITE": r"cpu0_L1I WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_TRANSLATION": r"cpu0_L1I TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_PREFETCH": r"cpu0_L1I PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "L1I_WRONG_PATH": r"cpu0_L1I WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "L1I_AVERAGE_MISS_LATENCY": r"cpu0_L1I AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # L2C metrics
        "L2C_TOTAL": r"cpu0_L2C TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_LOAD": r"cpu0_L2C LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_RFO": r"cpu0_L2C RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_PREFETCH_AHM": r"cpu0_L2C PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_WRITE": r"cpu0_L2C WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_TRANSLATION": r"cpu0_L2C TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_PREFETCH": r"cpu0_L2C PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "L2C_WRONG_PATH": r"cpu0_L2C WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "L2C_AVERAGE_MISS_LATENCY": r"cpu0_L2C AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # STLB metrics
        "STLB_TOTAL": r"cpu0_STLB TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_LOAD": r"cpu0_STLB LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_RFO": r"cpu0_STLB RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_PREFETCH_AHM": r"cpu0_STLB PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_WRITE": r"cpu0_STLB WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_TRANSLATION": r"cpu0_STLB TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_PREFETCH": r"cpu0_STLB PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "STLB_WRONG_PATH": r"cpu0_STLB WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s+POLLUTUION:\s+(\d+)\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)",
        "STLB_AVERAGE_MISS_LATENCY": r"cpu0_STLB AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
    }


def parse_cpu_patterns(line, patterns, data):
    """Parse initial stats patterns and update the data dictionary."""
    for key, pattern in patterns.items():
        match = re.search(pattern, line)
        if match:
            try:
                data[key] = float(match.group(1))
            except ValueError:
                raise ValueError(f"Failed to parse {key} in line: {line}")


def parse_cache_patterns(line, cache_patterns, data):
    """Parse cache patterns and update the data dictionary with access, hits, and misses."""
    for key, pattern in cache_patterns.items():
        match = re.search(pattern, line)
        if match:
            if key.endswith("AVERAGE_MISS_LATENCY"):
                # Handle average miss latency metrics
                data[key] = float(match.group(1))

            elif key.endswith("PREFETCH_AHM"):
                # Handle PREFETCH ACCESS/HIT/MISS patterns
                access, hits, miss = map(int, match.groups())
                cache_type = key.split("_")[0]  # Extract the cache type, e.g., "LLC"
                data[f"{cache_type}_PREFETCH_ACCESS"] = access
                data[f"{cache_type}_PREFETCH_HIT"] = hits
                data[f"{cache_type}_PREFETCH_MISS"] = miss

            elif key.endswith("PREFETCH"):
                # Handle PREFETCH REQUESTED/ISSUED/USEFUL/USELESS patterns
                requested, issued, useful, useless = map(int, match.groups())
                cache_type = key.split("_")[0]  # Extract the cache type, e.g., "LLC"
                data[f"{cache_type}_PREFETCH_REQUESTED"] = requested
                data[f"{cache_type}_PREFETCH_ISSUED"] = issued
                data[f"{cache_type}_PREFETCH_USEFUL"] = useful
                data[f"{cache_type}_PREFETCH_USELESS"] = useless

            elif key.endswith("WRONG_PATH"):
                # Handle WRONG-PATH metrics
                values = list(map(int, match.groups()))
                wrong_path_fields = [
                    "ACCESS",
                    "LOAD",
                    "USEFULL",
                    "FILL",
                    "USELESS",
                    "POLLUTION",
                    "WP_FILL",
                    "WP_MISS",
                    "CP_FILL",
                    "CP_MISS",
                ]
                cache_type = key.split("_")[0]  # Extract the cache type, e.g., "LLC"
                for i, field in enumerate(wrong_path_fields):
                    data[f"{cache_type}_WRONG_PATH_{field}"] = values[i]

            else:
                # Handle general ACCESS/HIT/MISS patterns for TOTAL, LOAD, RFO, WRITE, TRANSLATION
                access, hits, miss = map(int, match.groups())
                cache_type, metric_type = key.split("_", 1)
                data[f"{cache_type}_{metric_type}_ACCESS"] = access
                data[f"{cache_type}_{metric_type}_HITS"] = hits
                data[f"{cache_type}_{metric_type}_MISS"] = miss
