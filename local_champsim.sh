# ./config.sh champsim_config.json
make -j32

w=10000000
s=100000000

bin/champsim --warmup-instructions $w --simulation-instructions $s ../traces/400.perlbench-41B.champsimtrace.xz >trace.log &
bin/champsim --warmup-instructions $w --simulation-instructions $s --scheduling-flush ../traces/400.perlbench-41B.champsimtrace.xz >sf-trace.log &
bin/champsim --warmup-instructions $w --simulation-instructions $s --scheduling-flush --reschedule-only-branches ../traces/400.perlbench-41B.champsimtrace.xz >sfb-trace.log &

# sed -n '/Warmup finished/,$p' sf-trace.log > sf-trace.log.tmp && mv sf-trace.log.tmp sf-trace.log
