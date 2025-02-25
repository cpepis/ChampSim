#!/bin/bash

# rm -rf .cs && rm -rf bin && mkdir -p temp/cp-data && mkdir -p temp/wp-data && mkdir -p temp/wpa-data
# ./config.sh configs/champsim_config.json configs/no.json && make -j32
make -j32
# for trace in $(ls ../Traces/TAGE/*/*.gz); do
    trace="../Traces/TAGE_SC_L/SPEC/557.xz.gz"
    trace_name=$(basename $trace)
    trace_name=${trace_name%.gz}
    echo "Running $trace_name"

    warmup=1000
    sim=100000

    echo "Running Correct Path default"
    ./bin/champsim-default --warmup-instructions $warmup --simulation-instructions $sim ${trace} >temp/cp-data/${trace_name}-default.txt

    # echo "Running Wrong Path default"
    # ./bin/champsim-default --warmup-instructions $warmup --simulation-instructions $sim --wrong-path ${trace} >temp/wp-data/${trace_name}-default.txt

    # echo "Running Wrong Path Aware default"
    # ./bin/champsim-default --warmup-instructions $warmup --simulation-instructions $sim --wrong-path --wpa ${trace} >temp/wpa-data/${trace_name}-default.txt
# done
