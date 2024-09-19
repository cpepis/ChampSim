make
echo "Done make, now running..."
bin/champsim --warmup-instructions 1000 --simulation-instructions 1000 ../cp-traces/400.perlbench-41B.champsimtrace.xz > trace.log
sed -n '/Warmup finished/,$p' trace.log > trace.log.tmp && mv trace.log.tmp trace.log

grep "\[cpu0_L1D\]" trace.log > l1d_misses.log
grep "\[SF\]" trace.log > sf_misses.log