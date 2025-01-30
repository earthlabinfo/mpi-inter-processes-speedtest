# mpi-inter-processes-speedtest

あくまでノード間通信テスト用のサンプルで、出力自体に意味は無い。  
実行時、無駄な通信をノード間で流すので、ネットワーク負荷に注意。
このコードの一部または全部に生成AIの出力を含む可能性があります。
動作については保証しかねますので、実行される方の責任において使用してください。

## compile with oneAPI <2024

```bash
source /opt/intel/oneapi/setvars.sh
mpiicx -std=c99 -o mpi_test-throughput mpi_test-throughput.c
```

## run by interactive

```bash
source /opt/intel/oneapi/setvars.sh
mpirun -np 12 -ppn 1 -hosts n001,n002,n003 ./mpi_test-throughput
```

出力例
```bash
Starting cross-node ring communication.
Rank 0 (n001) will send data to Rank 1 (n002)
Rank 1 (n002) will send data to Rank 2 (n003)
Rank 3 (n001) will send data to Rank 0 (n001)
Rank 2 (n003) will send data to Rank 3 (n001)
Rank 3 -> Rank 0: Intra-node communication done.
  Elapsed time = 0.473478 sec, Bandwidth = 1056.015267 MB/s
Rank 2@n003-> Rank 3@n001: Inter-node communication done.
  Elapsed time = 0.528382 sec, Bandwidth = 946.284363 MB/s
Rank 0@n001-> Rank 1@n002: Inter-node communication done.
  Elapsed time = 0.647966 sec, Bandwidth = 771.645818 MB/s
Rank 1@n002-> Rank 2@n003: Inter-node communication done.
  Elapsed time = 0.649042 sec, Bandwidth = 770.366027 MB/s
All ranks have participated in the cross-node ring communication.
Total time: 1.649262 sec
Inter-node communications included a 1-second delay after each transfer.
```

- 参加ノードはn00[1-3]の3台。
- ppnを1意外にした場合は、ノード内通信が発生する可能性があるので1を推奨。
- npを大きくしすぎるとメモリを大量に消費するので、ノード数+1程度から始めることを推奨。

## run by openpbs

想定環境は以下のとおり。
- openpbsが動作しており、ログインユーザのhomeが各ノードで共有されている
- ノード間をinteractiveな入力なしでSSHできるよう設定されている
- 使用可能なノードが3台以上ある

```bash
qsub job_mpi-test.sh
110.n001
```

結果
```
tracejob 110.n001

Job: 110.n001

12/25/2024 22:27:45  L    Considering job to run
12/25/2024 22:27:45  S    enqueuing into workq, state Q hop 1
12/25/2024 22:27:45  S    Job Queued at request of earthtest@n001, owner = earthtest@n001, job name = job_mpi-test.sh, queue = workq
12/25/2024 22:27:45  S    Job Run at request of Scheduler@n001 on exec_vnode (n001:ncpus=1)+(n002:ncpus=1)+(n003:ncpus=1)
12/25/2024 22:27:45  L    Job run
12/25/2024 22:27:45  M    nprocs:  251, cantstat:  0, nomem:  0, skipped:  174, cached:  0
12/25/2024 22:27:45  M    Started, pid = 5520
12/25/2024 22:27:48  M    task 00000001 terminated
12/25/2024 22:27:48  M    Terminated
12/25/2024 22:27:49  M    task 00000001 cput=00:00:01
12/25/2024 22:27:49  M    kill_job
12/25/2024 22:27:49  M    n001 cput=00:00:01 mem=1988kb
12/25/2024 22:27:49  M    n002 cput=00:00:01 mem=0kb
12/25/2024 22:27:49  M    n003 cput=00:00:01 mem=0kb
12/25/2024 22:27:49  M    Obit sent
12/25/2024 22:27:50  S    Obit received momhop:1 serverhop:1 state:R substate:42
12/25/2024 22:27:50  M    copy file request received
12/25/2024 22:27:51  M    Staged 2/2 items out over 0:00:01
12/25/2024 22:27:51  M    no active tasks
12/25/2024 22:27:52  S    Exit_status=0 resources_used.cpupercent=83 resources_used.cput=00:00:03 resources_used.mem=1988kb resources_used.ncpus=3
                          resources_used.vmem=20624kb resources_used.walltime=00:00:04
12/25/2024 22:27:52  M    delete job request received
12/25/2024 22:27:52  S    dequeuing from workq, state E
12/25/2024 22:27:52  M    kill_job
```

```
cat job_mpi-test.sh.o110

:: initializing oneAPI environment ...
   110.n001.SC: BASH_VERSION = 4.4.20(1)-release
   args: Using "$@" for setvars.sh arguments: --force
:: advisor -- latest
:: ccl -- latest
:: compiler -- latest
:: dal -- latest
:: debugger -- latest
:: dev-utilities -- latest
:: dnnl -- latest
:: dpcpp-ct -- latest
:: dpl -- latest
:: ipp -- latest
:: ippcp -- latest
:: mkl -- latest
:: mpi -- latest
:: tbb -- latest
:: vtune -- latest
:: oneAPI environment initialized ::

Starting cross-node ring communication.
Rank 0 (n001) will send data to Rank 1 (n002)
Rank 1 (n002) will send data to Rank 2 (n003)
Rank 2 (n003) will send data to Rank 0 (n001)
Rank 0@n001-> Rank 1@n002: Inter-node communication done.
  Elapsed time = 0.606129 sec, Bandwidth = 824.907301 MB/s
Rank 1@n002-> Rank 2@n003: Inter-node communication done.
  Elapsed time = 0.612049 sec, Bandwidth = 816.927486 MB/s
Rank 2@n003-> Rank 0@n001: Inter-node communication done.
  Elapsed time = 0.613421 sec, Bandwidth = 815.100536 MB/s
All ranks have participated in the cross-node ring communication.
Total time: 1.613755 sec
Inter-node communications included a 1-second delay after each transfer.
```
