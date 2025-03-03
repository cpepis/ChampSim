#!/bin/bash

# rm -rf .csconfig && rm -rf bin && mkdir -p temp/cp-data && mkdir -p temp/wp-data && mkdir -p temp/wpa-data
# ./config.sh configs/champsim_config.json configs/no.json
make -j32

# for trace in $(ls ../Traces/TAGE/*/*.gz); do
    # trace="../Traces/TAGE_SC_L/SPEC/523.xalancbmk.gz"
    # trace="../Traces/TAGE_SC_L/LCF/cassandra.gz"
    trace="../Traces/TAGE_SC_L/SPEC/505.mcf.gz"
    trace_name=$(basename $trace)
    trace_name=${trace_name%.gz}
    echo "Running $trace_name"

    w=1000000
    s=10000000

    echo "Running Correct Path default"
    ./bin/champsim-default --warmup-instructions $w --simulation-instructions $s ${trace} >temp/cp-data/${trace_name}-default.txt

    echo "Running Wrong Path default"
    ./bin/champsim-default --warmup-instructions $w --simulation-instructions $s --wrong-path ${trace} >temp/wp-data/${trace_name}-default.txt

    # echo "Running Wrong Path Aware default"
    # ./bin/champsim-default --warmup-instructions $w --simulation-instructions $s --wrong-path --wpa ${trace} >temp/wpa-data/${trace_name}-default.txt
# done
