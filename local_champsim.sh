#!/bin/bash

validate_trace_file() {
    # Check if the trace file exists
    if [ ! -f "$trace_file" ]; then
        echo "Error: Trace file '$trace_file' not found."
        exit 1
    fi

    # Check if the trace file is ending with champsimtrace.xz
    if [[ "$trace_file" != *champsimtrace.xz ]]; then
        echo "Error: Trace file '$trace_file' does not end with champsimtrace.xz."
        exit 1
    fi

    trace_name=$(basename "$trace_file" .champsimtrace.xz)
}

# Check if the user provided a command
if [ "$1" == "clean" ]; then
    echo "Cleaning up..."
    rm -rf .csconfig
    rm -rf temp
    rm -rf bin
    echo "Configuring Champsim..."
    ./config.sh champsim_config.json
    echo "Done."
    exit 0
elif [ "$1" == "kill" ]; then
    echo "Killing all champsim processes..."
    pkill -f champsim
    exit 0
fi

# Check if the user provided a trace file
if [ -z "$1" ]; then
    echo "Error: No trace file provided."
    echo "Usage: $0 <trace_file> [warmup-instructions] [simulation-instructions] [output-file]"
    exit 1
fi

trace_file=$1
validate_trace_file

# Check if the user provided warmup and simulation instructions
if [ -z "$2" ]; then
    w=1000000
else
    w=$2
fi

if [ -z "$3" ]; then
    s=10000000
else
    s=$3
fi

# Check if the user provided an output file
if [ -z "$4" ]; then
    output_file="${trace_name}.log"
else
    output_file="${trace_name}-$4.log"
fi

rm -rf bin
make -j$(nproc)

mkdir -p temp/bsl # baseline
mkdir -p temp/rsk # rescheduling
mkdir -p temp/rsk-branch # rescheduling only after a branch
mkdir -p temp/rsk-predictor # rescheduling with predictor
mkdir -p temp/rsk-branch-predictor # rescheduling with predictor only after a branch

echo "Running Champsim with the following parameters:"
echo "Trace file: $trace_file"
echo "Warmup instructions: $w"
echo "Simulation instructions: $s"
echo "Output file: $output_file"

# Construct the command to run Champsim
bin/champsim --warmup-instructions $w --simulation-instructions $s ${trace_file} >temp/bsl/${trace_name}.log &
bin/champsim --warmup-instructions $w --simulation-instructions $s --rsk ${trace_file} >temp/rsk/${trace_name}.log &
bin/champsim --warmup-instructions $w --simulation-instructions $s --rsk --rsk-branch ${trace_file} >temp/rsk-branch/${trace_name}.log &
bin/champsim --warmup-instructions $w --simulation-instructions $s --rsk --rsk-predictor ${trace_file} >temp/rsk-predictor/${trace_name}.log &
bin/champsim --warmup-instructions $w --simulation-instructions $s --rsk --rsk-branch --rsk-predictor ${trace_file} >temp/rsk-branch-predictor/${trace_name}.log &