make
echo "Done make, now running..."
bin/champsim --warmup-instructions 1000 --simulation-instructions 1000 ../traces/400.perlbench-41B.champsimtrace.xz >trace.log
bin/champsim --warmup-instructions 1000 --simulation-instructions 1000 --scheduling-flush ../traces/400.perlbench-41B.champsimtrace.xz >sf-trace.log
