job_submition() {
    # Define the file path
    data_directory="${1}-data"
    logs_directory="${1}-logs"
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

    mkdir -p "${data_directory}"
    mkdir -p "${logs_directory}"

    sbatch -Q -J ${job_name} --output=${logs_directory}/%x.out --error=${logs_directory}/%x.err $file_path

    # Revert the changes by restoring the backup
    cp "$backup_file" "$file_path"
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
        sbatch -Q -J watcher --output=/dev/null --error=/dev/null $file_path
    fi

    # Revert the changes by restoring the backup
    cp "$backup_file" "$file_path"
}

for trace in $(ls ../traces/*.xz); do
    trace_name=$(basename $trace)
    trace_name=${trace_name%.gz}
    echo "Processing ${trace_name}"

    champsim_command="'./bin/champsim --warmup-instructions 10000000 --simulation-instructions 100000000 ${trace} > bl-data/${trace_name}.txt'"
    job_submition "bl"

    champsim_command="'./bin/champsim --warmup-instructions 10000000 --simulation-instructions 100000000 --scheduling-flush ${trace} > sf-data/${trace_name}.txt'"
    job_submition "sf"
done

start_watcher
