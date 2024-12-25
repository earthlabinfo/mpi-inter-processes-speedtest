#!/bin/sh
#PBS  -m be
#PBS -l nodes=3:ppn=1
#PBS -l ncpus=3

cd $PBS_O_WORKDIR

source /opt/intel/oneapi/setvars.sh  --force
mpirun  ./mpi_test-throughput
