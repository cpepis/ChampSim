"""Shared plotting utilities for ChampSim analysis notebooks."""

import plotly.graph_objects as go

# ---------------------------------------------------------------------------
# Suite definitions
# ---------------------------------------------------------------------------
SUITES = ["lcf", "gaps", "spec"]
SUITE_DISPLAY = {"lcf": "LCF", "gaps": "GAP", "spec": "SPEC"}

# ---------------------------------------------------------------------------
# ISPASS IEEE two-column format constants
# US Letter 8.5×11", margins 0.75" sides / 1.0" top-bottom, col gap 0.2"
# Canvas at 72 DPI so 1 px ≈ 1 pt at print size; scale=4 for ~288 eff. DPI
# One column = 3.4" → 245 px, two columns = 7.0" → 504 px
# Min font 10pt for body text (IEEE requirement)
# ---------------------------------------------------------------------------
ISPASS_SCALE = 2
ISPASS_FONT_SIZE = 38
ISPASS_BENCHMARK_FONT_SIZE = 30
ISPASS_AXIS_TITLE_FONT_SIZE = 32
ISPASS_FONT_FAMILY = "Arial, sans-serif"

ISPASS_ONE_COL_WIDTH = 1100
ISPASS_ONE_COL_HEIGHT = 500

ISPASS_TWO_COL_WIDTH = 2200
ISPASS_TWO_COL_HEIGHT = 500

# ---------------------------------------------------------------------------
# Figure defaults (use ISPASS presets above for paper figures)
# ---------------------------------------------------------------------------
FIG_WIDTH = ISPASS_TWO_COL_WIDTH
FIG_HEIGHT_SHORT = ISPASS_TWO_COL_HEIGHT
FIG_HEIGHT_TALL = 350
FIG_SCALE = ISPASS_SCALE
FIG_FONT_SIZE = ISPASS_FONT_SIZE
FIG_BENCHMARK_FONT_SIZE = ISPASS_BENCHMARK_FONT_SIZE
FIG_AXIS_TITLE_FONT_SIZE = ISPASS_AXIS_TITLE_FONT_SIZE
FIG_FONT_FAMILY = ISPASS_FONT_FAMILY

# ---------------------------------------------------------------------------
# Bar style constants
# ---------------------------------------------------------------------------
WP_BAR_COLOR = "darkorange"

# ED-WP bars: no fill, hatched, with colored outline
WP_BAR_MARKER = dict(
    color="rgba(0,0,0,0)",
    line=dict(color=WP_BAR_COLOR, width=2),
    pattern=dict(shape="/", fgcolor=WP_BAR_COLOR),
)

# ---------------------------------------------------------------------------
# Benchmark short‑name mapping (used for x‑axis tick labels)
# ---------------------------------------------------------------------------
BENCHMARK_SHORT_NAMES = {
    # LCF
    "web-search": "web",
    "media-stream": "media",
    "specjbb": "jbb",
    "wikipedia": "wiki",
    "finagle-http": "f-http",
    "speedometer2.0": "speedo",
    "data-serving": "data",
    "kafka": "kafka",
    "tpcc": "tpcc",
    "cassandra": "cass",
    "finagle-chirper": "f-chirp",
    "verilator-bolted": "vrlatr",
    "tomcat": "tmct",
    # GAP
    "sssp": "sssp",
    "pr": "pr",
    "prspmv": "prspmv",
    "cc": "cc",
    "bc": "bc",
    "ccsv": "ccsv",
    "bfs": "bfs",
    "tc": "tc",
    # SPEC
    "548.exchange2": "ex2",
    "525.x264": "x264",
    "531.deepsjeng": "sjeng",
    "557.xz": "xz",
    "541.leela": "leela",
    "505.mcf": "mcf",
}


def short_name(benchmark):
    """Return the short display name for *benchmark*, falling back to itself."""
    return BENCHMARK_SHORT_NAMES.get(benchmark, benchmark)


# ---------------------------------------------------------------------------
# Layout helpers
# ---------------------------------------------------------------------------
_AXIS_COMMON = dict(
    showgrid=True,
    gridcolor="lightgray",
    gridwidth=0.5,
    zeroline=True,
    zerolinecolor="black",
    zerolinewidth=2,
    griddash="dot",
)


def base_layout(height=FIG_HEIGHT_SHORT, font_size=FIG_FONT_SIZE, **overrides):
    """Return a standard Plotly layout dict (white bg, grid, margins, font).

    Extra keyword arguments are merged on top (use nested dicts for xaxis /
    yaxis overrides).
    """
    layout = dict(
        plot_bgcolor="white",
        margin=dict(l=0, r=0, t=30, b=15, autoexpand=True),
        font=dict(family=FIG_FONT_FAMILY, size=font_size),
        xaxis=dict(
            tickangle=-45,
            ticklabelstandoff=-10,
            tickmode="array",
            tickfont=dict(size=FIG_BENCHMARK_FONT_SIZE),
            zeroline=False,
            **{k: v for k, v in _AXIS_COMMON.items() if k not in ("zeroline", "zerolinecolor", "zerolinewidth")},
        ),
        yaxis=dict(
            title=dict(font=dict(size=FIG_AXIS_TITLE_FONT_SIZE)),
            tickfont=dict(size=FIG_AXIS_TITLE_FONT_SIZE),
            automargin=True,
            **{**_AXIS_COMMON, "gridwidth": 2},
        ),
        barmode="group",
        legend=dict(
            orientation="h",
            yanchor="bottom",
            y=1.05,
            xanchor="center",
            x=0.5,
        ),
    )
    # Merge overrides (two levels deep for sub‑dicts like xaxis/yaxis).
    # When a caller passes title="string" but the base has title=dict(...),
    # promote the string to dict(text=string) so the font settings survive.
    for key, val in overrides.items():
        if isinstance(val, dict) and key in layout and isinstance(layout[key], dict):
            for k2, v2 in val.items():
                base_v2 = layout[key].get(k2)
                if isinstance(base_v2, dict) and isinstance(v2, str):
                    layout[key][k2] = {**base_v2, "text": v2}
                elif isinstance(base_v2, dict) and isinstance(v2, dict):
                    layout[key][k2] = {**base_v2, **v2}
                else:
                    layout[key][k2] = v2
        else:
            layout[key] = val

    # Inject axis title font into any yaxis/yaxis2/... that has a title
    _title_font = dict(font=dict(size=FIG_AXIS_TITLE_FONT_SIZE))
    _tick_font = dict(size=FIG_AXIS_TITLE_FONT_SIZE)
    for key in list(layout):
        if key.startswith("yaxis") and isinstance(layout[key], dict):
            t = layout[key].get("title")
            if isinstance(t, str):
                layout[key]["title"] = {**_title_font, "text": t}
            elif isinstance(t, dict) and "font" not in t:
                layout[key]["title"] = {**_title_font, **t}
            if "tickfont" not in layout[key]:
                layout[key]["tickfont"] = _tick_font

    return layout


def save_fig(
    fig,
    name,
    width=FIG_WIDTH,
    height=FIG_HEIGHT_SHORT,
    scale=FIG_SCALE,
    save_html=True,
    fmt="pdf",
):
    """Write *fig* to ``pdf/<name>.<fmt>`` (and optionally ``html/<name>.html``)."""
    fig.write_image(f"pdf/{name}.{fmt}", width=width, height=height, scale=scale)
    if save_html:
        fig.write_html(f"html/{name}.html", include_plotlyjs="cdn")


# ---------------------------------------------------------------------------
# Data extraction helpers
# ---------------------------------------------------------------------------


def get_sorted_benchmarks(
    results,
    suite,
    config="default",
    version="wp",
    include_amean=True,
    include_gmean=False,
):
    """Return benchmark names for *suite* sorted by MPKI (ascending).

    Parameters
    ----------
    results : dict
        The global results dict keyed like ``"{suite}_{version}_{config}_df"``.
    suite, config, version : str
        Selectors for the dataframe.
    include_amean / include_gmean : bool
        Whether to append amean / gmean at the end.

    Returns
    -------
    list[str]
        Sorted benchmark names (without suite prefix).
    """
    key = f"{suite}_{version}_{config}_df"
    df = results[key]
    mpki = df["MPKI"]
    benchmarks = [bm for bm in mpki.index if bm not in ("amean", "gmean")]
    benchmarks_sorted = sorted(benchmarks, key=lambda x: mpki[x])
    if include_gmean and "gmean" in mpki.index:
        benchmarks_sorted.append("gmean")
    if include_amean and "amean" in mpki.index:
        benchmarks_sorted.append("amean")
    return benchmarks_sorted


def get_metric_across_suites(
    results,
    metric_fn,
    config="default",
    version="wp",
    include_amean=True,
    include_gmean=False,
    amean_prefix=True,
    gmean_prefix=True,
    sort_by_mpki=True,
):
    """Extract a metric from all three suites, sorted by MPKI.

    Parameters
    ----------
    results : dict
    metric_fn : callable(results, suite, benchmarks_sorted) -> list[float]
        Given the results dict, the suite name, and the ordered benchmark
        list, return a list of metric values in the same order.
    config, version : str
    include_amean, include_gmean : bool
    amean_prefix, gmean_prefix : bool
        If True, labels are prefixed with ``SUITE_`` (e.g. ``LCF_amean``).
    sort_by_mpki : bool

    Returns
    -------
    all_benchmarks : list[str]  – display names
    all_values : list[float]
    suite_boundaries : list[float]  – x positions for vertical dividers
    suite_labels : list[tuple(float, str)]  – (x_center, label)
    """
    all_benchmarks = []
    all_values = []
    suite_boundaries = []
    suite_labels = []
    x_position = 0

    for suite in SUITES:
        benchmarks = get_sorted_benchmarks(
            results,
            suite,
            config=config,
            version=version,
            include_amean=include_amean,
            include_gmean=include_gmean,
        )
        # Build display‑name list
        labeled = []
        for bm in benchmarks:
            if bm == "amean" and amean_prefix:
                labeled.append(f"{SUITE_DISPLAY[suite]}_amean")
            elif bm == "gmean" and gmean_prefix:
                labeled.append(f"{SUITE_DISPLAY[suite]}_gmean")
            else:
                labeled.append(bm)

        all_benchmarks.extend(labeled)
        all_values.extend(metric_fn(results, suite, benchmarks))

        suite_labels.append(
            (
                x_position + len(benchmarks) / 2,
                SUITE_DISPLAY[suite],
            )
        )
        x_position += len(benchmarks)
        suite_boundaries.append(x_position - 0.5)

    return all_benchmarks, all_values, suite_boundaries, suite_labels


def add_suite_dividers(fig, boundaries, labels, font_size=None):
    """Add vertical dashed lines and suite‑name annotations to *fig*.

    *boundaries* and *labels* come from :func:`get_metric_across_suites`.
    Only adds dividers between suites (skips the last boundary).
    """
    if font_size is None:
        font_size = FIG_FONT_SIZE
    for boundary in boundaries[:-1]:
        fig.add_vline(x=boundary, line_width=2, line_dash="dash", line_color="gray")
    for xpos, label in labels:
        fig.add_annotation(
            x=xpos,
            y=0.95,
            text=label,
            showarrow=False,
            xref="x",
            yref="paper",
            font=dict(size=font_size, color="black"),
        )


def get_display_names(all_benchmarks):
    """Return short display names for a list of benchmark names.

    Suite-prefixed means (e.g. ``LCF_amean``) keep their prefix so that
    Plotly treats them as distinct categorical x-values.
    """
    display = []
    for bm in all_benchmarks:
        if "_amean" in bm or "_gmean" in bm:
            display.append(bm)
        else:
            display.append(short_name(bm))
    return display


def get_tick_labels(all_benchmarks):
    """Return tick labels: same as display names but without suite prefix on means."""
    labels = []
    for bm in all_benchmarks:
        if "_amean" in bm or "_gmean" in bm:
            labels.append(bm.split("_", 1)[-1])
        else:
            labels.append(short_name(bm))
    return labels


def apply_short_names(fig, all_benchmarks):
    """Set x‑axis tick labels to short benchmark names."""
    display = get_display_names(all_benchmarks)
    fig.update_layout(
        xaxis=dict(
            tickvals=list(range(len(all_benchmarks))),
            ticktext=display,
        )
    )


def plot_cp_wp_speedup(
    results,
    prefetchers,
    cache_level,
    suites=None,
    y_range=None,
    font_size=None,
    annotations_cp=None,
    annotations_wp=None,
):
    """Create CP and WP speedup bar charts for a set of prefetchers/policies.

    Parameters
    ----------
    results : dict
    prefetchers : dict
        ``{key: {"label": str, "color": str, ...}, ...}``
    cache_level : str
        Used in filenames only (e.g. "l1i", "l1d", "l2c", "llc").
    suites : list[str] or None
        Defaults to all three suites.
    y_range : list or None
    font_size : int or None
    annotations_cp, annotations_wp : list[dict] or None
        Each dict is passed to ``fig.add_annotation``.

    Returns
    -------
    fig_cp, fig_wp : go.Figure
    """
    if suites is None:
        suites = SUITES
    if font_size is None:
        font_size = FIG_FONT_SIZE * 2  # small_size=30 equivalent
    suite_labels_display = [SUITE_DISPLAY.get(s, s.upper()) for s in suites]

    pattern_shapes = ["", "/", "\\", "x", "-", "|", "+", "."]

    speedups = {pf: {"cp": [], "wp": []} for pf in prefetchers}

    for suite in suites:
        cp_default = results[f"{suite}_cp_default_df"]["IPC"]["gmean"]
        wp_default = results[f"{suite}_wp_default_df"]["IPC"]["gmean"]
        for pf in prefetchers:
            cp_pf = results[f"{suite}_cp_{pf}_df"]["IPC"]["gmean"]
            wp_pf = results[f"{suite}_wp_{pf}_df"]["IPC"]["gmean"]
            speedups[pf]["cp"].append(((cp_pf / cp_default) - 1) * 100)
            speedups[pf]["wp"].append(((wp_pf / wp_default) - 1) * 100)

    figs = {}
    for group, y_title in [
        ("cp", "Speedup (%) - No-WP"),
        ("wp", "Speedup (%) - ED-WP"),
    ]:
        fig = go.Figure()
        for i, (pf, props) in enumerate(prefetchers.items()):
            fig.add_trace(
                go.Bar(
                    x=suite_labels_display,
                    y=speedups[pf][group],
                    name=props["label"],
                    marker_color=props["color"],
                    marker=dict(
                        pattern=dict(shape=pattern_shapes[i % len(pattern_shapes)])
                    ),
                    showlegend=(group == "cp"),
                )
            )

        # Add vertical dividers between suites (outside the prefetcher loop)
        for j in range(1, len(suite_labels_display)):
            fig.add_vline(x=j - 0.5, line_width=2, line_dash="dash", line_color="gray")

        y_kwargs = dict(title=y_title)
        if y_range is not None:
            y_kwargs["range"] = y_range
        fig.update_layout(
            **base_layout(
                font_size=font_size,
                yaxis=y_kwargs,
                xaxis=dict(tickangle=0),
                margin=dict(l=20, r=20, t=50, b=50),
                legend=dict(
                    orientation="h", yanchor="bottom", y=1.1, xanchor="center", x=0.5
                ),
            )
        )

        ann_list = annotations_cp if group == "cp" else annotations_wp
        if ann_list:
            for ann in ann_list:
                fig.add_annotation(**ann)

        figs[group] = fig

    return figs["cp"], figs["wp"], speedups
