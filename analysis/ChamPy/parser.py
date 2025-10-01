import os
import re
import pandas as pd

from datetime import timedelta


def merge_simpoints(df):
    def load_weights(filepath):
        weights = {}
        with open(filepath, "r") as f:
            for line in f:
                if line.strip():
                    key, value = line.strip().split(":")
                    weights[key.strip()] = float(value.strip())
        return weights

    def get_weight(name):
        weight = weights.get(name, -1)
        if weight == -1:
            print(
                f"Warning: Weight for {name} not found in weights file. Setting to 1."
            )
            weight = 1
        return weight

    def get_benchmark(name):
        return name.split("-")[0]

    weights = load_weights("weights.txt")

    temp_df = df.copy()
    temp_df["Weight"] = temp_df.index.map(get_weight)
    temp_df["BaseBenchmark"] = temp_df.index.map(get_benchmark)

    # Normalize weights within each benchmark group
    def normalize_group(group):
        total = group["Weight"].sum()
        if total < 1 and total > 0:
            group["Weight"] = group["Weight"] / total
        elif total > 1:
            print(
                f"Warning: Total weight for {group.name} is greater than 1. Do nothing for now."
            )
        elif total == 1:
            print(
                f"Warning: Total weight for {group.name} is equal to 1. No normalization needed."
            )
        else:
            print(
                f"Warning: Somthing went wrong with the weights for {group.name}. Total weight is {total}."
            )
        return group

    temp_df = temp_df.groupby("BaseBenchmark", group_keys=False).apply(normalize_group)

    # Identify metric columns (numeric, excluding weight)
    metric_cols = temp_df.select_dtypes(include="number").columns.difference(["Weight"])

    # Apply weights to metric columns
    for col in metric_cols:
        temp_df[col] = temp_df[col] * temp_df["Weight"]

    # Aggregate by benchmark
    temp_df = temp_df.groupby("BaseBenchmark")[metric_cols].sum().reset_index()
    temp_df["Benchmark"] = temp_df["BaseBenchmark"]
    temp_df = temp_df.set_index("Benchmark").drop(columns=["BaseBenchmark"])

    # Label suite
    temp_df["Suite"] = temp_df.index.map(
        lambda x: (
            "xs"
            if x[0] == "x"
            else (
                "gap"
                if x[0].isalpha()
                else ("2006" if x.split(".")[0].startswith("4") else "2017")
            )
        )
    )

    # Split into DataFrames
    xs = temp_df[temp_df["Suite"] == "xs"].drop(columns=["Suite"])
    gap = temp_df[temp_df["Suite"] == "gap"].drop(columns=["Suite"])
    spec2006_df = temp_df[temp_df["Suite"] == "2006"].drop(columns=["Suite"])
    spec2017_df = temp_df[temp_df["Suite"] == "2017"].drop(columns=["Suite"])

    return xs, gap, spec2006_df, spec2017_df


def parse_champsim_output(path):
    # Handle directory case
    if os.path.isdir(path):
        all_data = []
        for filename in os.listdir(path):

            if (
                filename.startswith("pr.kron")
                or filename.startswith("pr.twitter")
                or filename.startswith("pr.urand")
            ):
                continue

            if filename.startswith("sssp.twitter") or filename.startswith("sssp.urand"):
                continue

            if filename.startswith("tc.twitter"):
                continue

            filepath = os.path.join(path, filename)
            if os.path.isfile(filepath):
                # Parse each file and add the benchmark name to each row
                df = parse_single_file(filepath)
                benchmark_name = os.path.splitext(filename)[0]  # Extract benchmark name
                benchmark_name = benchmark_name.split(".champsimtrace.xz")[
                    0
                ]  # Remove suffix
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

    # Read the file and start parsing
    with open(file_path, "r") as file:
        lines = file.readlines()

    roi_start = False
    for line in lines:
        if "Simulation complete" in line:
            match = re.search(
                r"Simulation time: (\d{2}) hr (\d{2}) min (\d{2}) sec", line
            )
            if match:
                hours, minutes, seconds = map(int, match.groups())
                sim_time = timedelta(hours=hours, minutes=minutes, seconds=seconds)
                data["Simulation Time"] = (
                    sim_time.total_seconds()
                )  # Store as total seconds
                continue
        if "Region of Interest Statistics" in line:
            roi_start = True
            continue
        if not roi_start:
            continue

        # Stop parsing if "DRAM Statistics" is encountered
        if "DRAM Statistics" in line:
            break

        # Parse each line for the relevant stats
        parse_cpu_patterns(line, patterns, data, lines)
        parse_cache_patterns(line, cache_patterns, data)

    # Convert the data dictionary to a DataFrame
    if not data:
        raise ValueError(
            f"No data found in the file: {file_path}. File has been deleted."
        )

    df = pd.DataFrame([data])
    return df


def define_cpu_patterns():
    """Define regex patterns for capturing cpu statistics."""
    return {
        "IPC": r"cumulative IPC: ([\d.]+)",
        "instructions": r"instructions: (\d+)",
        "cycles": r"cycles: (\d+)",
        "Branch Prediction Accuracy": r"Branch Prediction Accuracy: ([\d.]+)%",
        "MPKI": r"MPKI: ([\d.]+)",
        "Average ROB Occupancy at Mispredict": r"Average ROB Occupancy at Mispredict: ([\d.]+)",
        "MPKI_BRANCH_DIRECT_JUMP": r"MPKI_BRANCH_DIRECT_JUMP: ([\d.]+)",
        "MPKI_BRANCH_INDIRECT": r"MPKI_BRANCH_INDIRECT: ([\d.]+)",
        "MPKI_BRANCH_CONDITIONAL": r"MPKI_BRANCH_CONDITIONAL: ([\d.]+)",
        "MPKI_BRANCH_DIRECT_CALL": r"MPKI_BRANCH_DIRECT_CALL: ([\d.]+)",
        "MPKI_BRANCH_INDIRECT_CALL": r"MPKI_BRANCH_INDIRECT_CALL: ([\d.]+)",
        "MPKI_BRANCH_RETURN": r"MPKI_BRANCH_RETURN: ([\d.]+)",
        "Rescheduled Events": r"Rescheduled Events: (\d+)",
        "RSK_BRANCH_DIRECT_JUMP": r"RSK_BRANCH_DIRECT_JUMP: (\d+)",
        "RSK_BRANCH_INDIRECT": r"RSK_BRANCH_INDIRECT: (\d+)",
        "RSK_BRANCH_CONDITIONAL": r"RSK_BRANCH_CONDITIONAL: (\d+)",
        "RSK_BRANCH_DIRECT_CALL": r"RSK_BRANCH_DIRECT_CALL: (\d+)",
        "RSK_BRANCH_INDIRECT_CALL": r"RSK_BRANCH_INDIRECT_CALL: (\d+)",
        "RSK_BRANCH_RETURN": r"RSK_BRANCH_RETURN: (\d+)",
        "RSK_LOAD": r"RSK_LOAD: (\d+)",
        "RSK_STORE": r"RSK_STORE: (\d+)",
        "RSK_ARITHMETIC": r"RSK_ARITHMETIC: (\d+)",
        "RETIRED_BRANCH_DIRECT_JUMP": r"RETIRED_BRANCH_DIRECT_JUMP: (\d+)",
        "RETIRED_BRANCH_INDIRECT": r"RETIRED_BRANCH_INDIRECT: (\d+)",
        "RETIRED_BRANCH_CONDITIONAL": r"RETIRED_BRANCH_CONDITIONAL: (\d+)",
        "RETIRED_BRANCH_DIRECT_CALL": r"RETIRED_BRANCH_DIRECT_CALL: (\d+)",
        "RETIRED_BRANCH_INDIRECT_CALL": r"RETIRED_BRANCH_INDIRECT_CALL: (\d+)",
        "RETIRED_BRANCH_RETURN": r"RETIRED_BRANCH_RETURN: (\d+)",
        "RETIRED_LOAD": r"RETIRED_LOAD: (\d+)",
        "RETIRED_STORE": r"RETIRED_STORE: (\d+)",
        "RETIRED_ARITHMETIC": r"RETIRED_ARITHMETIC: (\d+)",
        "Unique Loads Retired": r"Unique Loads Retired: (\d+)",
        "Repeated Loads Retired": r"Repeated Loads Retired: (\d+)",
        "L1D Unique Load Hits": r"L1D Unique Load Hits: (\d+)",
        "L1D Repeated Load Hits": r"L1D Repeated Load Hits: (\d+)",
        "L1D Unique Load Misses": r"L1D Unique Load Misses: (\d+)",
        "L1D Repeated Load Misses": r"L1D Repeated Load Misses: (\d+)",
        "L2C Unique Load Hits": r"L2C Unique Load Hits: (\d+)",
        "L2C Repeated Load Hits": r"L2C Repeated Load Hits: (\d+)",
        "L2C Unique Load Misses": r"L2C Unique Load Misses: (\d+)",
        "L2C Repeated Load Misses": r"L2C Repeated Load Misses: (\d+)",
        "LLC Unique Load Hits": r"LLC Unique Load Hits: (\d+)",
        "LLC Repeated Load Hits": r"LLC Repeated Load Hits: (\d+)",
        "LLC Unique Load Misses": r"LLC Unique Load Misses: (\d+)",
        "LLC Repeated Load Misses": r"LLC Repeated Load Misses: (\d+)",
        "Total L1D Hits": r"Total L1D Hits: (\d+)",
        "Total L1D Misses": r"Total L1D Misses: (\d+)",
        "Total L2C Hits": r"Total L2C Hits: (\d+)",
        "Total L2C Misses": r"Total L2C Misses: (\d+)",
        "Total LLC Hits": r"Total LLC Hits: (\d+)",
        "Total LLC Misses": r"Total LLC Misses: (\d+)",
        "Merged Loads": r"Merged Loads: (\d+)",
        "Detected Load Misses": r"Detected Load Misses: (\d+)",
        "Deferred Execution Instructions": r"Deferred Execution Instructions: (\d+)",
        "Scenario 1": r"No Predictor L1D Miss \(Reschedule\): (\d+)",
        "Scenario 2": r"Predicted L1D Hit, Actual L1D Hit \(No Reschedule\): (\d+)",
        "Scenario 3": r"Predicted L1D Hit, Actual L1D Miss \(Reschedule - FP\): (\d+)",
        "Scenario 4": r"Predicted L1D Miss, Actual L2C Hit \(No Reschedule - TN for L1D\): (\d+)",
        "Scenario 5": r"Predicted L1D Miss, Actual L2C Miss \(Reschedule - Deeper Miss\): (\d+)",
        "Scenario 6": r"Predicted L1D Miss, Actual L1D Hit \(Reschedule - FN\): (\d+)",
        "Average ROB Occupancy at rescheduled": r"Average ROB Occupancy at rescheduled: ([\d.]+)",
        "Maximum Instructions Rescheduled in a Single Event": r"Maximum Instructions Rescheduled in a Single Event: (\d+)",
        # Load Predictor Statistics
        "Total Predictions": r"Total Predictions: (\d+)",
        "Predicted Hits": r"Predicted Hits \(Aggressive\): (\d+)",
        "Predicted Misses": r"Predicted Misses \(Non-Aggressive\): (\d+)",
        "Correct Predictions": r"Correct Predictions: (\d+)",
        "Incorrect Predictions": r"Incorrect Predictions: (\d+)",
        "Overall Prediction Accuracy": r"Overall Prediction Accuracy: ([\d.]+)%",
        "True Positives": r"True Positives \(Pred H, Act H\): (\d+)",
        "True Negatives": r"True Negatives \(Pred M, Act M\): (\d+)",
        "False Positives": r"False Positives \(Pred H, Act M\): (\d+)",
        "False Negatives": r"False Negatives \(Pred M, Act H\): (\d+)",
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
        "LLC_AVERAGE_MISS_LATENCY": r"LLC AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # DTLB metrics
        "DTLB_TOTAL": r"cpu0_DTLB TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_LOAD": r"cpu0_DTLB LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_RFO": r"cpu0_DTLB RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_PREFETCH_AHM": r"cpu0_DTLB PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_WRITE": r"cpu0_DTLB WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_TRANSLATION": r"cpu0_DTLB TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "DTLB_PREFETCH": r"cpu0_DTLB PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "DTLB_AVERAGE_MISS_LATENCY": r"cpu0_DTLB AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # ITLB metrics
        "ITLB_TOTAL": r"cpu0_ITLB TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_LOAD": r"cpu0_ITLB LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_RFO": r"cpu0_ITLB RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_PREFETCH_AHM": r"cpu0_ITLB PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_WRITE": r"cpu0_ITLB WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_TRANSLATION": r"cpu0_ITLB TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "ITLB_PREFETCH": r"cpu0_ITLB PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "ITLB_AVERAGE_MISS_LATENCY": r"cpu0_ITLB AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # L1D metrics
        "L1D_TOTAL": r"cpu0_L1D TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_LOAD": r"cpu0_L1D LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_RFO": r"cpu0_L1D RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_PREFETCH_AHM": r"cpu0_L1D PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_WRITE": r"cpu0_L1D WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_TRANSLATION": r"cpu0_L1D TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1D_PREFETCH": r"cpu0_L1D PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "L1D_AVERAGE_MISS_LATENCY": r"cpu0_L1D AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # L1I metrics
        "L1I_TOTAL": r"cpu0_L1I TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_LOAD": r"cpu0_L1I LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_RFO": r"cpu0_L1I RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_PREFETCH_AHM": r"cpu0_L1I PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_WRITE": r"cpu0_L1I WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_TRANSLATION": r"cpu0_L1I TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L1I_PREFETCH": r"cpu0_L1I PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "L1I_AVERAGE_MISS_LATENCY": r"cpu0_L1I AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # L2C metrics
        "L2C_TOTAL": r"cpu0_L2C TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_LOAD": r"cpu0_L2C LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_RFO": r"cpu0_L2C RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_PREFETCH_AHM": r"cpu0_L2C PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_WRITE": r"cpu0_L2C WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_TRANSLATION": r"cpu0_L2C TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "L2C_PREFETCH": r"cpu0_L2C PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "L2C_AVERAGE_MISS_LATENCY": r"cpu0_L2C AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
        # STLB metrics
        "STLB_TOTAL": r"cpu0_STLB TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_LOAD": r"cpu0_STLB LOAD\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_RFO": r"cpu0_STLB RFO\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_PREFETCH_AHM": r"cpu0_STLB PREFETCH\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_WRITE": r"cpu0_STLB WRITE\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_TRANSLATION": r"cpu0_STLB TRANSLATION\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)",
        "STLB_PREFETCH": r"cpu0_STLB PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)",
        "STLB_AVERAGE_MISS_LATENCY": r"cpu0_STLB AVERAGE MISS LATENCY:\s+([\d.]+) cycles",
    }


def parse_cpu_patterns(line, patterns, data, lines=None):
    """Parse initial stats patterns and update the data dictionary."""
    for key, pattern in patterns.items():
        match = re.search(pattern, line)
        if match:
            data[key] = match.group(1)
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

            else:
                # Handle general ACCESS/HIT/MISS patterns for TOTAL, LOAD, RFO, WRITE, TRANSLATION
                access, hits, miss = map(int, match.groups())
                cache_type, metric_type = key.split("_", 1)
                data[f"{cache_type}_{metric_type}_ACCESS"] = access
                data[f"{cache_type}_{metric_type}_HITS"] = hits
                data[f"{cache_type}_{metric_type}_MISS"] = miss
