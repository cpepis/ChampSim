./config.sh champsim_config.json && make
echo "Done make, now running..."
bin/champsim --warmup-instructions 10000000 --simulation-instructions 100000000 ../traces/400.perlbench-41B.champsimtrace.xz >trace.log
bin/champsim --warmup-instructions 10000000 --simulation-instructions 100000000 --scheduling-flush ../traces/400.perlbench-41B.champsimtrace.xz >sf-trace.log

# sed -n '/Warmup finished/,$p' sf-trace.log > sf-trace.log.tmp && mv sf-trace.log.tmp sf-trace.log
