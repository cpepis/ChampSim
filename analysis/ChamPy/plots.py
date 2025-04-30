import plotly.graph_objects as go


def plot_speedup(benchmarks, speedup_data):
    """
    Plots the speedup data using Plotly, excluding 'amean'.
    """

    # Filter out 'amean'
    filtered = [(b, s) for b, s in zip(benchmarks, speedup_data) if b != "amean"]
    filtered_benchmarks, filtered_speedup_data = zip(*filtered)

    # Plot
    fig = go.Figure()
    fig.add_trace(go.Bar(x=filtered_benchmarks, y=filtered_speedup_data))

    # Update layout
    fig.update_layout(
        xaxis_title="Benchmark",
        yaxis_title="Speedup (%)",
        barmode="group",
        xaxis_tickangle=-45,
    )

    return fig
