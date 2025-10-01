import plotly.graph_objects as go


FONT_SIZE = 30


def filter_benchmarks(benchmarks, data, exclude=None):
    """
    Filters out benchmarks that are not in the provided data.
    If 'exclude' is provided, it will also exclude those benchmarks.
    """
    if exclude is None:
        exclude = []

    filtered_benchmarks = [b for b in benchmarks if b in data and b not in exclude]
    filtered_data = [data[b] for b in filtered_benchmarks]

    return filtered_benchmarks, filtered_data


def plot_speedup(benchmarks, speedup_data):
    """
    Plots the speedup data using Plotly, excluding 'amean'.
    """

    filtered_benchmarks, filtered_speedup_data = filter_benchmarks(
        benchmarks, speedup_data, exclude=["amean"]
    )

    # Plot
    fig = go.Figure()
    fig.add_trace(go.Bar(x=filtered_benchmarks, y=filtered_speedup_data))

    # Update layout
    fig.update_layout(
        plot_bgcolor="white",
        barmode="group",
        margin=dict(l=0, r=0, t=0, b=0),
        xaxis=dict(
            title="Benchmark",
            tickangle=-45,
            tickfont=dict(family="Arial, sans-serif", size=FONT_SIZE),
        ),
        yaxis=dict(
            title="Speedup (%)",
            showgrid=True,
            gridcolor="LightGray",
            gridwidth=1,
            griddash="dot",
            tickfont=dict(family="Arial, sans-serif", size=FONT_SIZE),
        ),
        font=dict(family="Arial, sans-serif", size=FONT_SIZE),
        legend=dict(
            font=dict(family="Arial, sans-serif", size=FONT_SIZE),
        ),
    )

    return fig


def plot_stat(benchmarks, stat_data, title=None, exclude=None):
    """
    Plots the statistical data using Plotly, excluding 'amean'.
    """

    if exclude is None:
        exclude = []

    assert title is not None, "Title must be provided for the plot."

    filtered_benchmarks, filtered_stat_data = filter_benchmarks(
        benchmarks, stat_data, exclude=exclude
    )

    # Plot
    fig = go.Figure()
    fig.add_trace(go.Bar(x=filtered_benchmarks, y=filtered_stat_data))

    # Update layout
    fig.update_layout(
        barmode="group",
        plot_bgcolor="white",
        margin=dict(l=0, r=0, t=0, b=0),
        xaxis=dict(
            title="Benchmark",
            tickangle=-45,
            tickfont=dict(family="Arial, sans-serif", size=FONT_SIZE),
        ),
        yaxis=dict(
            title=title,
            showgrid=True,
            gridcolor="LightGray",
            gridwidth=1,
            griddash="dot",
            tickfont=dict(family="Arial, sans-serif", size=FONT_SIZE),
        ),
        font=dict(family="Arial, sans-serif", size=FONT_SIZE),
        legend=dict(
            font=dict(family="Arial, sans-serif", size=FONT_SIZE),
        ),
    )

    return fig
