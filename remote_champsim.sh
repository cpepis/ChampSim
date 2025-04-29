#!/bin/bash
exec > >(tee jobs/jobs.log) 2>&1

job_submition() {
    # Define the file path
    file_path="jobs/run.job"

    # Backup the original file
    backup_file="${file_path}.bak"
    cp "$file_path" "$backup_file"

    # Check if the temporary line is in the file
    if ! grep -q "## champsim_command" "$file_path"; then
        echo "Error: The ChampSim temporary line is not in the file '${file_path}'."
        exit -1
    fi

    # Replace
    sed -i "s|#account|${ACCOUNT}|" "$file_path"
    sed -i "s|#email|${EMAIL}|" "$file_path"
    sed -i "s|## champsim_command|champsim_command=$champsim_command|" "$file_path"

    # Submit the job
    job_name="${trace_name}"
    output_file="results/${1}/${trace_name}.txt"
    if [ ! -s "$output_file" ]; then
        echo "Submitting ${job_name}"
        sbatch -Q -J "${job_name}" --output=/dev/null --error=/dev/null "$file_path" >/dev/null
    else
        if ! grep -q "ChampSim completed all CPUs" "${output_file}"; then
            echo "Check this file (missing completion message): ${output_file}"
        fi
    fi

    # Revert the changes by restoring the backup
    cp "$backup_file" "$file_path"
}

wait_for_job_slots() {
    local max_jobs=490
    local num_jobs=$(squeue -u "$USER" | wc -l)

    # Subtract header line
    num_jobs=$((num_jobs - 1))

    while [ "$num_jobs" -ge "$max_jobs" ]; do
        echo "[$(date)] Job queue is full ($num_jobs >= $max_jobs). Waiting..."
        sleep 60
        num_jobs=$(squeue -u "$USER" | wc -l)
        num_jobs=$((num_jobs - 1))
    done
}

start_watcher() {
    # Define the file path
    file_path="jobs/watcher.job"

    # Backup the original file
    backup_file="${file_path}.bak"
    cp "$file_path" "$backup_file"

    # Replace
    sed -i "s|#account|${ACCOUNT}|" "$file_path"
    sed -i "s|#email|${EMAIL}|" "$file_path"

    if [ $(squeue -u $USER | grep watcher | wc -l) -eq 0 ]; then
        echo "Starting Watcher"
        sbatch -Q -J watcher --output=/dev/null --error=/dev/null $file_path >/dev/null
    fi

    # Revert the changes by restoring the backup
    cp "$backup_file" "$file_path"
}

# Start watcher
start_watcher

w=10000000
s=100000000

mkdir -p results/bsl # baseline
mkdir -p results/rsk # rescheduling

for trace in $(ls ../traces/*.xz); do
    trace_name=$(basename $trace)
    trace_name=${trace_name%.gz}
    echo "Processing ${trace_name}"

    champsim_command="'./bin/champsim --warmup-instructions ${w} --simulation-instructions ${s} ${trace} >results/bsl/${trace_name}.txt'"
    job_submition "bsl"
    wait_for_job_slots

    champsim_command="'./bin/champsim --warmup-instructions ${w} --simulation-instructions ${s} --rsk ${trace} >results/rsk/${trace_name}.txt'"
    job_submition "rsk"
    wait_for_job_slots
done
