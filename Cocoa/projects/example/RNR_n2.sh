#!/bin/bash
#SBATCH --job-name=rnr_n2
#SBATCH -q regular
#SBATCH -o ./projects/example/results/rnr_n2/%x_%A.out
#SBATCH -e ./projects/example/results/rnr_n2/%x_%A.err
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=7
#SBATCH --mail-user=diogo.henrique@unesp.br
#SBATCH --mail-type=ALL
#SBATCH -t 7-00:00:00

#OpenMP Settings:
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_PLACES=threads
export OMP_PROC_BIND=close

cd $SLURM_SUBMIT_DIR

conda activate cocoa
source start_cocoa

goo-job-nanny mpirun -n ${SLURM_NTASKS} --bind-to core --map-by numa:pe=${OMP_NUM_THREADS} cobaya-run ./projects/example/MCMC_RNR_n2.yaml -f