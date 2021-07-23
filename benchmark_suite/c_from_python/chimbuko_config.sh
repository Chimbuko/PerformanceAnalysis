#Note: This configuration file is sourced into the bash environment for Chimbuko startup scripts, thus the user must follow correct shell conventions
#Please do not remove any of the variables!

#IMPORTANT NOTE: Variables that cannot be left as default are marked as <------------ ***SET ME***

service_node_iface=eth0 #network interface upon which communication to the service node is performed <------------ ***SET ME***

####################################
#Options for visualization module
####################################
use_viz=1 #enable or disable the visualization
viz_root=/opt/chimbuko/viz    #the root directory of the visualization module  <------------ ***SET ME (if using viz)***
viz_worker_port=6379  #the port on which to run the redis server for the visualization backend
viz_port=5002 #the port on which to run the webserver
export C_FORCE_ROOT=1 #required only for docker runs, allows celery to execute properly as root user  <----------------- *** SET ME (if using Docker)

############################################################
#General options for Chimbuko backend (pserver, ad, provdb)
############################################################
backend_root="infer"  #The root install directory of the PerformanceAnalysis libraries. If set to "infer" it will be inferred from the path of the executables
chimbuko_services="infer" #The location of the Chimbuko service script. If set to "infer" it will be inferred from backend_root

####################################
#Options for the provenance database
####################################
use_provdb=1 #enable or disable the provDB. If disabled the provenance data will be written as JSON ASCII into the ${provdb_writedir} set below
provdb_extra_args="" #any extra command line arguments to pass
provdb_nshards=4  #number of database shards
provdb_engine="ofi+tcp;ofi_rxm"  #the OFI libfabric provider used for the Mochi stack
provdb_port=5000 #the port of the provenance database
provdb_nthreads=4 #number of worker threads; should be >= the number of shards
provdb_writedir=chimbuko/provdb #the directory in which the provenance database is written. Chimbuko creates chimbuko/provdb which can be used as a default
provdb_commit_freq=10000   #frequency ms at which the provenance database is committed to disk. If set to 0 it will commit only at the end

#With "verbs" provider (used for infiniband, iWarp, etc) we need to also specify the domain, which can be found by running fi_info (on a compute node)
provdb_domain=mlx5_0 #only needed for verbs provider   <------------ ***SET ME (if using verbs)***


####################################
#Options for the parameter server
####################################
use_pserver=1 #enable or disable the pserver
pserver_extra_args="" #any extra command line arguments to pass
pserver_port=5559  #port for parameter server
pserver_nt=2 #number of worker threads

####################################
#Options for the AD module
####################################
ad_extra_args="-perf_outputpath chimbuko/logs -perf_step 1" #any extra command line arguments to pass. Note: chimbuko/logs is automatically created by services script
ad_win_size=5  #number of events around an anomaly to store; provDB entry size is proportional to this so keep it small!
ad_alg="sstd" #the anomaly detection algorithm. Valid values are "hbos" and "sstd"
ad_outlier_hbos_threshold=0.99  #the percentile of events outside of which are considered anomalies by the HBOS algorithm
ad_outlier_sstd_sigma=12 #number of standard deviations that defines an outlier in the SSTD algorithm

####################################
#Options for TAU
####################################
export TAU_ADIOS2_ENGINE=SST    #online communication engine (alternative BP4 although this goes through the disk system and may be slower unless the BPfiles are stored on a burst disk)
export TAU_ADIOS2_ONE_FILE=FALSE  #a different connetion file for each rank
export TAU_ADIOS2_PERIODIC=1   #enable/disable ADIOS2 periodic output
export TAU_ADIOS2_PERIOD=1000000  #period in us between ADIOS2 io steps
export TAU_THREAD_PER_GPU_STREAM=1  #force GPU streams to appear as different TAU virtual threads
export TAU_THROTTLE=0  #enable/disable throttling of short-running functions

export TAU_MAKEFILE=/opt/tau2/x86_64/lib/Makefile.tau-papi-mpi-pthread-pdt-python-adios2  #The TAU makefile to use <------------ ***SET ME***
TAU_EXEC="tau_exec -T papi,mpi,pthread,pdt,python,adios2 -adios2_trace"   #how to execute tau_exec; the -T arguments should mirror the makefile name  <------------ ***SET ME***
TAU_PYTHON="tau_python -tau-python-interpreter=python3 -adios2_trace  -tau-python-args=-u"  #how to execute tau_python. Note that passing -u to python forces it to not buffer stdout so we can pipe it to tee in realtime

export EXE_NAME=python3.6  #the name of the executable (without path) <------------ ***SET ME***

TAU_ADIOS2_PATH=chimbuko/adios2  #path where the adios2 files are to be stored. Chimbuko services creates the directory chimbuko/adios2 in the working directory and this should be used by default
TAU_ADIOS2_FILE_PREFIX=tau-metrics  #the prefix of tau adios2 files; full filename is ${TAU_ADIOS2_PREFIX}-${EXE_NAME}-${RANK}.bp  













###########################################################################
# NON-USER VARIABLES BELOW  = DON'T MODIFY THESE!!
###########################################################################
#Extra processing
export TAU_ADIOS2_FILENAME="${TAU_ADIOS2_PATH}/${TAU_ADIOS2_FILE_PREFIX}"

if [[ ${backend_root} == "infer" ]]; then
    backend_root=$( readlink -f $(which provdb_admin | sed 's/provdb_admin//')/../ )
fi

if [[ ${chimbuko_services} == "infer" ]]; then
    chimbuko_services="${backend_root}/scripts/launch/run_services.sh"
    if [ ! -f "${chimbuko_services}" ]; then
	echo "Could not infer service script location: service script does not exist at ${chimbuko_services}!"
	exit 1
    fi
fi
