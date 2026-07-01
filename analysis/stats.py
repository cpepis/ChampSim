import os
import re
import math
import pandas as pd

import warnings

warnings.simplefilter("ignore", FutureWarning)

from datetime import timedelta

# mispredict_penalty from champsim_config.json
RESTEER_PENALTY_CYCLES = 12

weights = {
    "505.mcf_r_00": 0.073125,
    "505.mcf_r_01": 0.167787,
    "505.mcf_r_02": 0.053478,
    "505.mcf_r_03": 0.034881,
    "505.mcf_r_04": 0.095293,
    "505.mcf_r_05": 0.553688,
    "505.mcf_r_06": 0.021748,
    "525.x264_r_00": 0.051166,
    "525.x264_r_01": 0.107270,
    "525.x264_r_02": 0.205452,
    "525.x264_r_03": 0.220269,
    "525.x264_r_04": 0.268866,
    "525.x264_r_05": 0.040893,
    "525.x264_r_06": 0.106085,
    "531.deepsjeng_r_00": 0.095342,
    "531.deepsjeng_r_01": 0.144630,
    "531.deepsjeng_r_02": 0.088145,
    "531.deepsjeng_r_03": 0.133521,
    "531.deepsjeng_r_04": 0.082408,
    "531.deepsjeng_r_05": 0.067856,
    "531.deepsjeng_r_06": 0.194701,
    "531.deepsjeng_r_07": 0.000626,
    "531.deepsjeng_r_08": 0.192771,
    "541.leela_r_00": 0.000391,
    "541.leela_r_01": 0.157962,
    "541.leela_r_02": 0.124884,
    "541.leela_r_03": 0.022524,
    "541.leela_r_04": 0.192065,
    "541.leela_r_05": 0.251185,
    "541.leela_r_06": 0.024527,
    "541.leela_r_07": 0.226462,
    "548.exchange2_r_00": 0.084607,
    "548.exchange2_r_01": 0.165750,
    "548.exchange2_r_02": 0.021238,
    "548.exchange2_r_03": 0.040095,
    "548.exchange2_r_04": 0.119466,
    "548.exchange2_r_05": 0.307961,
    "548.exchange2_r_06": 0.260883,
    "557.xz_r_00": 0.116841,
    "557.xz_r_01": 0.046737,
    "557.xz_r_02": 0.351867,
    "557.xz_r_03": 0.004298,
    "557.xz_r_04": 0.480258,
}


def merge_simpoints(df):
    def get_weight(name):
        simpoint = "_".join(name.split("_")[:3]).split("-")[0]
        return weights.get(simpoint, 0)

    def get_algorithm(name):
        return "-".join(name.split("-")[2:])

    def get_benchmark(name):
        return name.split("_")[0]

    cache_prefixes = ["LLC", "DTLB", "ITLB", "L1D", "L1I", "L2C", "STLB"]
    branch_type_cols = [
        "direct_jumps",
        "indirect_branches",
        "conditional_branches",
        "direct_calls",
        "indirect_calls",
        "returns",
        "other_branches",
    ]
    branch_mpki_cols = [
        "BRANCH_DIRECT_JUMP",
        "BRANCH_INDIRECT",
        "BRANCH_CONDITIONAL",
        "BRANCH_DIRECT_CALL",
        "BRANCH_INDIRECT_CALL",
        "BRANCH_RETURN",
    ]

    # Build cache maps once (used for both synth creation and recomputation)
    all_latency_denom = {}
    all_cp_latency = {}
    all_pollution = {}
    for prefix in cache_prefixes:
        all_latency_denom.update(
            {
                f"{prefix}_AVERAGE_MISS_LATENCY": f"{prefix}_TOTAL_MISS",
                f"{prefix}_AVERAGE_WP_MISS_LATENCY": f"{prefix}_POLLUTION_WP_MISS",
                f"{prefix}_AVERAGE_CP_MISS_LATENCY": f"{prefix}_POLLUTION_CP_MISS",
                f"{prefix}_AVERAGE_INSTR_MISS_LATENCY": f"{prefix}_INSTR_REQ_MISS",
                f"{prefix}_AVERAGE_WP_INSTR_MISS_LATENCY": f"{prefix}_INSTR_REQ_WP_MISS",
                f"{prefix}_AVERAGE_DATA_MISS_LATENCY": f"{prefix}_DATA_REQ_MISS",
                f"{prefix}_AVERAGE_WP_DATA_MISS_LATENCY": f"{prefix}_DATA_REQ_WP_MISS",
            }
        )
        all_cp_latency.update(
            {
                f"{prefix}_AVERAGE_CP_INSTR_MISS_LATENCY": (
                    f"{prefix}_INSTR_REQ_MISS",
                    f"{prefix}_INSTR_REQ_WP_MISS",
                ),
                f"{prefix}_AVERAGE_CP_DATA_MISS_LATENCY": (
                    f"{prefix}_DATA_REQ_MISS",
                    f"{prefix}_DATA_REQ_WP_MISS",
                ),
            }
        )
        all_pollution[f"{prefix}_POLLUTION_AVG_POLLUTION"] = (
            f"{prefix}_POLLUTION_SAMPLES"
        )

    # --- Step 1: Extract metadata ---
    temp_df = df.copy()
    temp_df["Weight"] = temp_df.index.map(get_weight)
    temp_df["Algorithm"] = temp_df.index.map(get_algorithm)
    temp_df["BaseBenchmark"] = temp_df.index.map(get_benchmark)

    # Warn about zero weights
    zero_weight_rows = temp_df[temp_df["Weight"] == 0]
    if not zero_weight_rows.empty:
        warnings.warn(
            f"Unknown simpoints with weight 0 (will be dropped from merge): "
            f"{list(zero_weight_rows.index)}"
        )

    # Sum Simulation_Time without weighting (total wall-clock time across simpoints)
    sim_time_sums = None
    if "Simulation_Time" in temp_df.columns:
        sim_time_sums = (
            temp_df.groupby(["BaseBenchmark", "Algorithm"])["Simulation_Time"]
            .sum()
            .reset_index()
        )
        temp_df = temp_df.drop(columns=["Simulation_Time"])

    # --- Step 2: Create synthetic numerators for ratio columns ---
    if all(c in temp_df.columns for c in branch_type_cols):
        temp_df["_total_branches"] = sum(temp_df[c] for c in branch_type_cols)
    if "MPKI" in temp_df.columns and "instructions" in temp_df.columns:
        temp_df["_total_mispredictions"] = (
            temp_df["MPKI"] * temp_df["instructions"] / 1000
        )

    ratio_cols = []

    if "IPC" in temp_df.columns:
        ratio_cols.append("IPC")

    if "MPKI" in temp_df.columns:
        ratio_cols.append("MPKI")

    if (
        "Branch_Prediction_Accuracy" in temp_df.columns
        and "_total_branches" in temp_df.columns
    ):
        ratio_cols.append("Branch_Prediction_Accuracy")
        temp_df["_correct_predictions"] = (
            temp_df["Branch_Prediction_Accuracy"] * temp_df["_total_branches"] / 100
        )

    if (
        "Average_ROB_Occupancy_at_Mispredict" in temp_df.columns
        and "_total_mispredictions" in temp_df.columns
    ):
        ratio_cols.append("Average_ROB_Occupancy_at_Mispredict")
        temp_df["_total_rob_occupancy"] = (
            temp_df["Average_ROB_Occupancy_at_Mispredict"]
            * temp_df["_total_mispredictions"]
        )

    for col in branch_mpki_cols:
        if col in temp_df.columns and "instructions" in temp_df.columns:
            ratio_cols.append(col)
            temp_df[f"_synth_{col}"] = temp_df[col] * temp_df["instructions"] / 1000

    if "Resteer_Penalty" in temp_df.columns:
        ratio_cols.append("Resteer_Penalty")

    for lat_col, denom_col in all_latency_denom.items():
        if lat_col in temp_df.columns and denom_col in temp_df.columns:
            ratio_cols.append(lat_col)
            temp_df[f"_synth_{lat_col}"] = temp_df[lat_col] * temp_df[denom_col]

    for lat_col, (miss_col, wp_miss_col) in all_cp_latency.items():
        if (
            lat_col in temp_df.columns
            and miss_col in temp_df.columns
            and wp_miss_col in temp_df.columns
        ):
            ratio_cols.append(lat_col)
            temp_df[f"_denom_{lat_col}"] = temp_df[miss_col] - temp_df[wp_miss_col]
            temp_df[f"_synth_{lat_col}"] = (
                temp_df[lat_col] * temp_df[f"_denom_{lat_col}"]
            )

    for avg_col, samples_col in all_pollution.items():
        if avg_col in temp_df.columns and samples_col in temp_df.columns:
            ratio_cols.append(avg_col)
            temp_df[f"_synth_{avg_col}"] = temp_df[avg_col] * temp_df[samples_col]

    # --- Step 3: Weight count columns and sum by group ---
    ratio_cols = [c for c in ratio_cols if c in temp_df.columns]

    metric_cols = temp_df.select_dtypes(include="number").columns.difference(
        ["Weight"] + ratio_cols
    )

    for col in metric_cols:
        temp_df[col] = temp_df[col] * temp_df["Weight"]

    temp_df = (
        temp_df.groupby(["BaseBenchmark", "Algorithm"])[metric_cols].sum().reset_index()
    )

    # --- Step 4: Recompute ratio columns from weighted sums ---
    if (
        "IPC" in ratio_cols
        and "instructions" in temp_df.columns
        and "total_cycles" in temp_df.columns
    ):
        temp_df["IPC"] = temp_df["instructions"] / temp_df["total_cycles"]

    if (
        "MPKI" in ratio_cols
        and "_total_mispredictions" in temp_df.columns
        and "instructions" in temp_df.columns
    ):
        temp_df["MPKI"] = (
            temp_df["_total_mispredictions"] / temp_df["instructions"] * 1000
        )

    if (
        "Branch_Prediction_Accuracy" in ratio_cols
        and "_correct_predictions" in temp_df.columns
        and "_total_branches" in temp_df.columns
    ):
        temp_df["Branch_Prediction_Accuracy"] = (
            temp_df["_correct_predictions"] / temp_df["_total_branches"] * 100
        )

    if (
        "Average_ROB_Occupancy_at_Mispredict" in ratio_cols
        and "_total_rob_occupancy" in temp_df.columns
        and "_total_mispredictions" in temp_df.columns
    ):
        temp_df["Average_ROB_Occupancy_at_Mispredict"] = (
            temp_df["_total_rob_occupancy"] / temp_df["_total_mispredictions"]
        )

    for col in branch_mpki_cols:
        synth = f"_synth_{col}"
        if (
            col in ratio_cols
            and synth in temp_df.columns
            and "instructions" in temp_df.columns
        ):
            temp_df[col] = temp_df[synth] / temp_df["instructions"] * 1000

    if (
        "Resteer_Penalty" in ratio_cols
        and "Resteer_Events" in temp_df.columns
        and "total_cycles" in temp_df.columns
    ):
        temp_df["Resteer_Penalty"] = (
            temp_df["Resteer_Events"]
            * RESTEER_PENALTY_CYCLES
            / temp_df["total_cycles"]
            * 100
        )

    for lat_col, denom_col in all_latency_denom.items():
        synth = f"_synth_{lat_col}"
        if synth in temp_df.columns and denom_col in temp_df.columns:
            temp_df[lat_col] = temp_df[synth] / temp_df[denom_col].replace(
                0, float("nan")
            )

    for lat_col in all_cp_latency:
        synth = f"_synth_{lat_col}"
        denom_synth = f"_denom_{lat_col}"
        if synth in temp_df.columns and denom_synth in temp_df.columns:
            temp_df[lat_col] = temp_df[synth] / temp_df[denom_synth].replace(
                0, float("nan")
            )

    for avg_col, samples_col in all_pollution.items():
        synth = f"_synth_{avg_col}"
        if synth in temp_df.columns and samples_col in temp_df.columns:
            temp_df[avg_col] = temp_df[synth] / temp_df[samples_col].replace(
                0, float("nan")
            )

    # --- Step 5: Clean up and rebuild index ---
    drop_cols = [c for c in temp_df.columns if c.startswith("_")]
    temp_df = temp_df.drop(columns=drop_cols)

    # Restore unweighted Simulation_Time sum
    if sim_time_sums is not None:
        temp_df = temp_df.merge(sim_time_sums, on=["BaseBenchmark", "Algorithm"])

    temp_df["Benchmark"] = (
        temp_df["BaseBenchmark"] + "-champsim-" + temp_df["Algorithm"]
    )
    temp_df = temp_df.set_index("Benchmark").drop(
        columns=["BaseBenchmark", "Algorithm"]
    )

    return temp_df


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
    if "IPC" not in df.columns:
        warnings.warn("IPC column missing, skipping mean calculation.")
        return df

    if df.empty:
        warnings.warn("Empty DataFrame, skipping mean calculation.")
        return df

    amean_row = {col: (df[col].mean() if col != "IPC" else None) for col in df.columns}

    geomean = math.prod(df["IPC"]) ** (1 / len(df["IPC"]))

    gmean_row = {col: (geomean if col == "IPC" else None) for col in df.columns}

    df.loc["amean"] = amean_row
    df.loc["gmean"] = gmean_row

    df = df.reindex(list(df.index.drop(["amean", "gmean"])) + ["amean", "gmean"])

    return df


def parse_champsim_output(path):
    # Handle directory case
    if os.path.isdir(path):
        all_data = []
        for filename in os.listdir(path):
            filepath = os.path.join(path, filename)
            if os.path.isfile(filepath):

                # Skip files for SPEC CPU 2017 benchmarks
                if (
                    filename.startswith("500.perlbench")
                    or filename.startswith("520.omnetpp")
                    or filename.startswith("523.xalancbmk")
                    or filename.startswith("xapian")
                ):
                    continue

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


def _compile_patterns(pattern_dict):
    """Pre-compile a dict of {key: pattern_str} into a list of (key, compiled_regex)."""
    return [(key, re.compile(pattern)) for key, pattern in pattern_dict.items()]


def parse_single_file(file_path):
    data = {}

    # Read the file and start parsing after "Region of Interest Statistics"
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
                data["Simulation_Time"] = (
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
        parse_cpu_patterns(line, _compiled_cpu_patterns, data)
        parse_cache_patterns(line, _compiled_cache_patterns, data)

    if not data:
        warnings.warn(
            f"No data parsed from {file_path}. File may be incomplete or missing ROI section."
        )

    # Convert the data dictionary to a DataFrame
    df = pd.DataFrame([data])
    return df


def define_cpu_patterns():
    """Define regex patterns for capturing cpu statistics."""
    return {
        "IPC": r"cumulative IPC: ([\d.]+)",
        "instructions": r"instructions: (\d+)",
        "total_cycles": r"cycles: (\d+)",
        "wp_cycles": r"wp_cycles: (\d+)",
        "wrong_path_insts": r"wrong_path_insts: (\d+)",
        "wrong_path_insts_skipped": r"wrong_path_insts_skipped: (\d+)",
        "wrong_path_insts_executed": r"wrong_path_insts_executed: (\d+)",
        "instr_foot_print": r"instr_foot_print: (\d+)",
        "data_foot_print": r"(?<!addr_)data_foot_print: (\d+)",
        "data_addr_foot_print": r"data_addr_foot_print: (\d+)",
        "is_prefetch_insts": r"is_prefetch_insts: (\d+)",
        "is_prefetch_skipped": r"is_prefetch_skipped: (\d+)",
        "Branch_Prediction_Accuracy": r"Branch Prediction Accuracy: ([\d.]+)%",
        "MPKI": r"MPKI: ([\d.]+)",
        "Average_ROB_Occupancy_at_Mispredict": r"Average ROB Occupancy at Mispredict: ([\d.]+)",
        "direct_jumps": r"direct_jumps: (\d+)",
        "indirect_branches": r"indirect_branches: (\d+)",
        "conditional_branches": r"conditional_branches: (\d+)",
        "direct_calls": r"^direct_calls: (\d+)",
        "indirect_calls": r"^indirect_calls: (\d+)",
        "returns": r"returns: (\d+)",
        "other_branches": r"other_branches: (\d+)",
        "loads": r"loads: (\d+)",
        "stores": r"stores: (\d+)",
        "arithmetic": r"arithmetic: (\d+)",
        "total_instructions": r"total_instructions: (\d+)",
        "Fetch_Idle_Cycles": r"^Fetch Idle Cycles\s+(\d+)",
        "Decode_Idle_Cycles": r"^Decode Idle Cycles\s+(\d+)",
        "Dispatch_Idle_Cycles": r"^Dispatch Idle Cycles\s+(\d+)",
        "Schedule_Idle_Cycles": r"^Schedule Idle Cycles\s+(\d+)",
        "Execute_Idle_Cycles": r"^Execute Idle Cycles\s+(\d+)",
        "Retire_Idle_Cycles": r"^Retire Idle Cycles\s+(\d+)",
        "Fetch_Starve_Cycles": r"^Fetch Starve Cycles\s+(\d+)",
        "Decode_Starve_Cycles": r"^Decode Starve Cycles\s+(\d+)",
        "Dispatch_Starve_Cycles": r"^Dispatch Starve Cycles\s+(\d+)",
        "Schedule_Starve_Cycles": r"^Schedule Starve Cycles\s+(\d+)",
        "Execute_Starve_Cycles": r"^Execute Starve Cycles\s+(\d+)",
        "Retire_Starve_Cycles": r"^Retire Starve Cycles\s+(\d+)",
        "Total_Fetch_Instructions": r"Total Fetch Instructions\s+(\d+)",
        "Total_Decode_Instructions": r"Total Decode Instructions\s+(\d+)",
        "Total_Dispatch_Instructions": r"Total Dispatch Instructions\s+(\d+)",
        "Total_Schedule_Instructions": r"Total Schedule Instructions\s+(\d+)",
        "Total_Execute_Instructions": r"Total Execute Instructions\s+(\d+)",
        "Total_Retire_Instructions": r"Total Retire Instructions\s+(\d+)",
        "Resteer_Events": r"Resteer Events (\d+)",
        "Resteer_Penalty": r"Resteer Penalty ([\d.]+)",
        "WP_Not_Available_Count": r"WP Not Available Count (\d+) Cycles (\d+) \(([\d.]+)%\)",
        "WP_Not_Available_Cycles": r"WP Not Available Count \d+ Cycles (\d+) \(([\d.]+)%\)",
        "Loads_Count": r"Loads: Count (\d+)",
        "Loads_Issued": r"Loads: Count \d+ Issued (\d+)",
        "Fetch_Blocked_Cycles": r"Fetch Blocked Cycles (\d+)",
        "IFetch_Failed_Events": r"IFetch Failed Events (\d+)",
        "Fetch_Buffer_Not_Empty": r"Fetch Buffer Not Empty (\d+)",
        "Execute_None_Cycles": r"Execute None Cycles (\d+)",
        "Execute_Head_Not_Ready_Cycles": r"Execute Head Not Ready Cycles (\d+)",
        "Execute_Head_Not_Completed_Cycles": r"Execute Head Not Completed Cycles (\d+)",
        "Execute_Pending_Cycles": r"Execute Pending Cycles (\d+)",
        "Execute_Load_Blocked_Cycles": r"Execute Load Blocked Cycles (\d+)",
        "Scheduler_None_Cycles": r"Scheduler None Cycles (\d+)",
        "LQ_Full_Events": r"LQ Full Events (\d+)",
        "SQ_Full_Events": r"SQ Full Events (\d+)",
        "Non_Branch_Squashes": r"Non Branch Squashes (\d+)",
        "ROB_Full_Cycles": r"ROB Full Cycles (\d+)",
        "ROB_Empty_Cycles": r"ROB Empty Cycles (\d+)",
        "ROB_Full_Events": r"ROB Full Events (\d+)",
        "ROB_Empty_Events": r"ROB Empty Events (\d+)",
        "Loads_Success": r"Loads Success: (\d+)",
        "Loads_Executed": r"Loads Executed: (\d+)",
        "Loads_Retired": r"Loads Retired: (\d+)",
        "BRANCH_DIRECT_JUMP": r"BRANCH_DIRECT_JUMP:\s*([\d.eE-]+)",
        "BRANCH_INDIRECT": r"BRANCH_INDIRECT:\s*([\d.eE-]+)",
        "BRANCH_CONDITIONAL": r"BRANCH_CONDITIONAL:\s*([\d.eE-]+)",
        "BRANCH_DIRECT_CALL": r"BRANCH_DIRECT_CALL:\s*([\d.eE-]+)",
        "BRANCH_INDIRECT_CALL": r"BRANCH_INDIRECT_CALL:\s*([\d.eE-]+)",
        "BRANCH_RETURN": r"BRANCH_RETURN:\s*([\d.eE-]+)",
        "Execute_Only_WP_Cycles": r"Execute Only WP Cycles (\d+)",
        "Execute_Only_CP_Cycles": r"Execute Only CP Cycles (\d+)",
        "Execute_CP_WP_Cycles": r"Execute CP WP Cycles (\d+)",
        "Execute_ROB_Empty_Cycles": r"Execute nothing ROB Empty Cycles (\d+)",
        "Execute_ROB_Empty_Repair_Cycles": r"Execute nothing ROB Empty Repair Cycles (\d+)",
        "Execute_ROB_Empty_Fetch_Stalled_No_WP_Cycles": r"Execute nothing ROB Empty Fetch Stalled No WP Cycles (\d+)",
        "Execute_ROB_Not_Ready_Not_Full_New_Added_Cycles": r"Execute nothing ROB Not Ready Not Full New Added Cycles (\d+)",
        "Execute_ROB_Not_Ready_Not_Full_No_New_Added_Cycles": r"Execute nothing ROB Not Ready Not Full No New Added Cycles (\d+)",
        "Execute_ROB_Not_Ready_Full_New_Added_Cycles": r"Execute nothing ROB Not Ready Full New Added Cycles (\d+)",
        "Execute_ROB_Not_Ready_Full_No_New_Added_Cycles": r"Execute nothing ROB Not Ready Full No New Added Cycles (\d+)",
        "Execute_Other_IDK_Cycles": r"Execute nothing Other IDK Cycles (\d+)",
        "Execute_Total_Cycles": r"Execute Total Cycles Cycles (\d+)",
    }


def define_cache_patterns():
    """Define regex patterns for capturing cache metrics."""
    caches = [
        ("LLC", "LLC"),
        ("DTLB", "cpu0_DTLB"),
        ("ITLB", "cpu0_ITLB"),
        ("L1D", "cpu0_L1D"),
        ("L1I", "cpu0_L1I"),
        ("L2C", "cpu0_L2C"),
        ("STLB", "cpu0_STLB"),
    ]
    ahm = r"\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)"
    req = r":\s+(\d+)\s+HIT:\s+(\d+)\s+MISS:\s+(\d+)\s+WP_REQ:\s+(\d+)\s+WP_HIT:\s+(\d+)\s+WP_MISS:\s+(\d+)"

    patterns = {}
    for prefix, name in caches:
        # ACCESS/HIT/MISS patterns
        for metric in ["TOTAL", "LOAD", "RFO", "WRITE", "TRANSLATION"]:
            patterns[f"{prefix}_{metric}"] = rf"{name} {metric}{ahm}"
        patterns[f"{prefix}_PREFETCH_AHM"] = rf"{name} PREFETCH{ahm}"

        # PREFETCH REQUESTED/ISSUED/USEFUL/USELESS
        patterns[f"{prefix}_PREFETCH"] = (
            rf"{name} PREFETCH REQUESTED:\s+(\d+)\s+ISSUED:\s+(\d+)"
            rf"\s+USEFUL:\s+(\d+)\s+USELESS:\s+(\d+)"
        )

        # WRONG-PATH
        patterns[f"{prefix}_WRONG_PATH"] = (
            rf"{name} WRONG-PATH ACCESS:\s+(\d+)\s+LOAD:\s+(\d+)"
            rf"\s+USEFULL:\s+(\d+)\s+FILL:\s+(\d+)\s+USELESS:\s+(\d+)\s"
        )

        # POLLUTION
        patterns[f"{prefix}_POLLUTION"] = (
            rf"{name} POLLUTION:\s+([\d.]+)\s+SAMPLES:\s+(\d+)"
            rf"\s+WP_FILL:\s+(\d+)\s+WP_MISS:\s+(\d+)"
            rf"\s+CP_FILL:\s+(\d+)\s+CP_MISS:\s+(\d+)"
        )

        # INSTR REQ / DATA REQ
        patterns[f"{prefix}_INSTR_REQ"] = rf"{name} INSTR REQ{req}"
        patterns[f"{prefix}_DATA_REQ"] = rf"{name} DATA REQ{req}"

        # AVERAGE LATENCY variants
        latency_types = [
            "MISS",
            "WP MISS",
            "CP MISS",
            "INSTR MISS",
            "WP INSTR MISS",
            "CP INSTR MISS",
            "DATA MISS",
            "WP DATA MISS",
            "CP DATA MISS",
        ]
        for lt in latency_types:
            key_suffix = lt.replace(" ", "_")
            patterns[f"{prefix}_AVERAGE_{key_suffix}_LATENCY"] = (
                rf"{name} AVERAGE {lt} LATENCY:\s+([\d.]+) cycles"
            )

    return patterns


_compiled_cpu_patterns = _compile_patterns(define_cpu_patterns())
_compiled_cache_patterns = _compile_patterns(define_cache_patterns())

_PREFETCH_FIELDS = ["REQUESTED", "ISSUED", "USEFUL", "USELESS"]
_WRONG_PATH_FIELDS = ["ACCESS", "LOAD", "USEFULL", "FILL", "USELESS"]
_POLLUTION_FIELDS = [
    "AVG_POLLUTION",
    "SAMPLES",
    "WP_FILL",
    "WP_MISS",
    "CP_FILL",
    "CP_MISS",
]
_REQ_FIELDS = ["REQ", "HIT", "MISS", "WP_REQ", "WP_HIT", "WP_MISS"]


def parse_cpu_patterns(line, compiled_patterns, data):
    """Parse initial stats patterns and update the data dictionary."""
    for key, pattern in compiled_patterns:
        match = pattern.search(line)
        if match:
            try:
                data[key] = float(match.group(1))
            except ValueError:
                raise ValueError(f"Failed to parse {key} in line: {line}")


def parse_cache_patterns(line, compiled_cache_patterns, data):
    """Parse cache patterns and update the data dictionary with access, hits, and misses."""
    for key, pattern in compiled_cache_patterns:
        match = pattern.search(line)
        if match:
            cache_type = key.split("_")[0]
            if key.endswith("LATENCY"):
                data[key] = float(match.group(1))
            elif key.endswith("PREFETCH_AHM"):
                access, hits, miss = map(int, match.groups())
                data[f"{cache_type}_PREFETCH_ACCESS"] = access
                data[f"{cache_type}_PREFETCH_HIT"] = hits
                data[f"{cache_type}_PREFETCH_MISS"] = miss
            elif key.endswith("PREFETCH"):
                for field, val in zip(_PREFETCH_FIELDS, map(int, match.groups())):
                    data[f"{cache_type}_PREFETCH_{field}"] = val
            elif key.endswith("WRONG_PATH"):
                for field, val in zip(_WRONG_PATH_FIELDS, map(int, match.groups())):
                    data[f"{cache_type}_WRONG_PATH_{field}"] = val
            elif key.endswith("POLLUTION"):
                for field, val in zip(_POLLUTION_FIELDS, map(float, match.groups())):
                    data[f"{cache_type}_POLLUTION_{field}"] = val
            elif key.endswith("INSTR_REQ") or key.endswith("DATA_REQ"):
                req_type = "INSTR_REQ" if key.endswith("INSTR_REQ") else "DATA_REQ"
                for field, val in zip(_REQ_FIELDS, map(int, match.groups())):
                    data[f"{cache_type}_{req_type}_{field}"] = val
            else:
                _, metric_type = key.split("_", 1)
                access, hits, miss = map(int, match.groups())
                data[f"{cache_type}_{metric_type}_ACCESS"] = access
                data[f"{cache_type}_{metric_type}_HITS"] = hits
                data[f"{cache_type}_{metric_type}_MISS"] = miss
            break
