#!/bin/bash
#
# Simple run script to test QDMA in VF AXI-MM mode.
# 
# VF AXI-MM Transfer
#	- H2C operation is performed to send data to BRAM on the FPGA. 
#	- C2H operation is performed to reads data from BRAM.
#       -   C2H data is stored in a local file 'out_mm0_$qid', which will be compared to original file for correctness. 

################################################
# User Configurable Parameters
################################################
iteration=$1 # [Optional] Iterations 
size=$2 # [Optional] Size per payload packet
q_per_vf=4

################################################
# Hard Coded Parameters
################################################
vf=0
host_adr_high=4094
mem_div=16384
s_high=$mem_div
infile='./datafile_16bit_pattern.bin'


################################################
# Input check
################################################
if [ "$1" == "-h" ]; then
	echo "Example: qdma_run_test_st_vf.sh [iteration] [size(in byte)]"
	echo "Example: qdma_run_test_st_vf.sh This will run VF MM test in random mode"
        exit
fi

if [ -z $2 ] || [ $# -eq 0 ] ; then
	echo "Run VF MM test in random mode"
	sleep 3
fi

if [ -z $iteration ]; then
	iteration=1 
fi

if [ ! -z $size ]; then 
	f_size=1
else
   	f_size=0
fi


#################################################
# Helper Functions
################################################

function randomize_tx_params() {
        #random host address between 0 to 3094
        hst_adr1=$RANDOM
        let "hst_adr1 %= $host_adr_high"
        # byte size
        f1=$RANDOM
	let "f1 %= 2"
        if [ $f1 -eq 0 ]; then
		size=($RANDOM % 64 + 1) ## for random number between 1 and 64
        else
                size=$((($RANDOM<<15) | ($RANDOM+1)))
        fi
        let "size %= $s_high"
	even=$size
        let "even %= 2"
        if [ $even -eq 1 ];then
                let "size = $size+1"
        fi
}


function queue_start() {
	echo "---- Queue Start $2 ----"
	dmactl qdma$1 q add idx $2 mode $3 dir bi
	dmactl qdma$1 q start idx $2 dir bi
}

function cleanup_queue() {
	echo "---- Queue Clean up $2 ----"
        dmactl qdma$1 q stop idx $2 dir bi
        dmactl qdma$1 q del idx $2 dir bi
}

echo "**** AXI-MM Start ****"

for ((i=0; i< $q_per_vf; i++)) do
	# Setup for Queues
	qid=$i
	dev_mm_c2h="/dev/qdmavf0-MM-$qid"
	dev_mm_h2c="/dev/qdmavf0-MM-$qid"
        loop=1

	out_mm="out_mm0_"$qid
	# Open the Queue for AXI-MM streaming interface.
	queue_start vf$vf $qid mm

	while [ "$loop" -le $iteration ]
	do
		# Determine if DMA is targeted @ random host address
		if [ $f_size -eq 1 ]; then
			hst_adr1=0
		else
			randomize_tx_params
		fi

		# H2C transfer 
		dma_to_device -d $dev_mm_h2c -f $infile -s $size -o $hst_adr1

		# C2H transfer
		dma_from_device -d $dev_mm_c2h -f $out_mm -s $size -o $hst_adr1

		# Compare file for correctness
		cmp $out_mm $infile -n $size

		if [ $? -eq 1 ]; then
			echo "#### Test ERROR. Queue $qid data did not match ####"
			exit 1
		else
			echo "**** Test pass. Queue $qid"
		fi

		wait

		((loop++))
	done
	# Close the Queues
	cleanup_queue vf$vf $qid
done
echo "**** AXI-MM completed ****"
