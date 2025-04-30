import math
import numpy as np
import pandas as pd


def calculate_means(df):
    """
    Calculates the geometric mean of the IPC column and the arithmetic mean
    for all other columns, appending both as separate rows.
    """
    try:
        # Calculate the arithmetic mean for each column except "IPC" and "Suite"
        amean_row = {}
        for col in df.columns:
            if col != "IPC" and col != "Suite":
                amean_row[col] = df[col].mean()
            else:
                amean_row[col] = np.nan

        # Calculate the geometric mean of the IPC column
        if (df["IPC"] <= 0).any():
            raise ZeroDivisionError(
                "Some IPC values are zero or negative, geometric mean undefined."
            )
        geomean = math.prod(df["IPC"]) ** (1 / len(df["IPC"]))

        # Prepare the "gmean" row with only the IPC column filled
        gmean_row = {col: np.nan for col in df.columns}
        gmean_row["IPC"] = geomean

        # Append both "amean" and "gmean" rows
        df.loc["amean"] = pd.Series(amean_row)
        df.loc["gmean"] = pd.Series(gmean_row)

        # Ensure "amean" and "gmean" are at the end of the DataFrame
        df = df.reindex(list(df.index.drop(["amean", "gmean"])) + ["amean", "gmean"])

    except ZeroDivisionError as e:
        print(f"{e}, exiting...")
        exit(1)
    except KeyError as e:
        print(f"Column '{e}' not found in DataFrame, exiting...")
        exit(1)

    return df


def calculate_speedup(df, baseline):
    """
    Calculates the speedup of the IPC column compared to a baseline.
    """
    if "IPC" not in df.columns or "IPC" not in baseline.columns:
        raise KeyError("IPC column not found in DataFrame or baseline.")
    if baseline["IPC"].iloc[0] == 0:
        raise ZeroDivisionError("Baseline IPC is zero, cannot calculate speedup.")
    # Calculate speedup as a percentage
    return ((df["IPC"] / baseline["IPC"]) - 1) * 100
