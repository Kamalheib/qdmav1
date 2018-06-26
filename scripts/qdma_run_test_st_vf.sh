#!/bin/bash
# Simple run script to test QDMA in VF AXI-ST mode.
# 
# VF AXI-ST Transfer
#	- H2C operation is performed to send data to BRAM on the FPGA. 
#	- C2H operation is performed to reads data from BRAM.
#       -   C2H data is stored in a local file 'out_st_$qid', which will be compared to original file for correctness. 


################################################
# User Configurable Parameters
################################################

pfn=$1 # <Mandatory> PF number 
vfn=$2 # <Mandatory> VF number 
iteration=$3 # [Optional] iterations of C2H tests
size=$4 # [Optional] Size per payload packet
num_pkt=$5 # [Optional] number of payload packet
q_per_vf=4 # IMPORTANT: user must set this based on the design
q_per_vfg=512 # IMPORTANT: user must set this based on the design
num_vf=64 # IMPORTANT: user must set this based on the design

################################################
# Hard Coded Parameters
################################################
usr_bar=2 # For VF, DMA BAR is bar 0. User BAR is bar 2.
vf=0
host_adr_high=4094
mem_div=16384
s_high=$mem_div
infile='./datafile_16bit_pattern.bin'
logfile="loopback$1_$2.log"

################################################
# Input check
################################################
if [ -z $1 ]
then
	echo "ERROR:   Invalid Command Line"
	echo "Example: qdma_run_test_st_vf.sh <pfn> <vfn>  [iteration] [size(in byte)] [num_pkt]"
	echo "Example: qdma_run_test_st_vf.sh <pfn> <vfn> This will run VF ST test in random mode"
	exit 
fi

if [ "$1" == "-h" ]
then
	echo "Example: qdma_run_test_st_vf.sh <pfn> <vfn>  [iteration] [size(in byte)] [num_pkt]"
	echo "Example: qdma_run_test_st_vf.sh <pfn> <vfn> This will run VF ST test in random mode"
	exit 
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
        hst_adr1=0;
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
 	num_pkt=$(( ($RANDOM % 9) +1))
}

function queue_start() {
	echo "---- Queue Start $2 ----"
	dmactl qdma$1 q add idx $2 dir h2c mode $3
	dmactl qdma$1 q start idx $2 dir h2c
	dmactl qdma$1 q add idx $2 dir c2h mode $3
	dmactl qdma$1 q start idx $2 dir c2h 
}

function cleanup_queue() {
	echo "---- Queue Clean up $2 ----"
        dmactl qdma$1 q stop idx $2 dir h2c
        dmactl qdma$1 q del idx $2 dir h2c 
        dmactl qdma$1 q stop idx $2 dir c2h
        dmactl qdma$1 q del idx $2 dir c2h 
}


echo "############################# AXI-ST Start #################################"

for ((i=0; i< $q_per_vf; i++)) do
	# Setup for Queues
	qid=$i
	qbase=$((($pfn+1)*$q_per_vfg - $num_vf*$q_per_vf + $vfn*$q_per_vf))
	hw_qid=$(($qbase + $qid))
	dev_st_c2h="/dev/qdmavf0-ST-$qid"
	dev_st_h2c="/dev/qdmavf0-ST-$qid"
	out="out_st_$qid"
        loop=1

	# Open the Queue for AXI-ST streaming interface.
	queue_start vf$vf $qid st > /dev/null

	while [ "$loop" -le $iteration ]
	do
		# Determine if DMA is targeted @ random host address
		if [ $f_size -eq 1 ]; then
			hst_adr1=0
		else
			randomize_tx_params
		fi

		# if more packets are requested.
		let "tsize= $size*$num_pkt"
 
                echo ""
                echo "########################################################################################"
		echo "#############  H2C ST LOOP $loop : dev=$dev_st_h2c pfn=$pfn vfn=$vfn qid=$qid hw_qid=$hw_qid"
		echo "#############               transfer_size=$tsize pkt_size=$size pkt_count=$num_pkt hst_adr=$hst_adr1"
                echo "########################################################################################"

                #clear match bit before each H2C ST transfer
		dmactl qdmavf0 reg write bar $usr_bar 0x0c 0x01


		# H2C transfer 
		dma_to_device -d $dev_st_h2c -f $infile -s $tsize -o $hst_adr1 &
                re=$?

		wait

	        # Check match bit and QID
		hwqid_match=$(dmactl qdmavf0 reg read bar $usr_bar 0x10 | grep "0x10" | cut -d '=' -f2 | cut -d 'x' -f2 | cut -d '.' -f1)
                code=`echo $hwqid_match | tr 'a-z' 'A-Z'`
		val=`echo "obase=2; ibase=16; $code" | bc`
                if [ $(($val & 0x1)) -ne 1 ];then
                  echo "### ERROR: QID MATCH is $hwqid_match "$hw_qid_hex"1"
                  re=-1
                fi               

                if [ $re == 0 ]; then 
                  echo "######################################################"
		  echo "##############   VF H2C ST PASS QID $qid ################"
                  echo "######################################################"
                else
                  echo "#### ERROR: VF H2C ST FAIL"
                fi

		echo ""
                echo "########################################################################################"
		echo "#############  C2H ST LOOP $loop : dev=$dev_st_c2h pfn=$pfn vfn=$vfn qid=$qid hw_qid=$hw_qid"
		echo "#############               transfer_size=$tsize pkt_size=$size pkt_count=$num_pkt hst_adr=$hst_adr1"
                echo "########################################################################################"

		dmactl qdmavf0 reg write bar $usr_bar 0x0 $hw_qid  # for Queue 0
		dmactl qdmavf0 reg write bar $usr_bar 0x4 $size
		dmactl qdmavf0 reg write bar $usr_bar 0x20 $num_pkt #number of packets
		dmactl qdmavf0 reg write bar $usr_bar 0x08 2 # Must set C2H start before starting transfer

		dma_from_device -d $dev_st_c2h -f $out -o $hst_adr1 -s $tsize &

		wait

		#Check if files is there.
		if [ ! -f $out ]; then
			echo " #### ERROR: Queue $qid output file does not exists ####"
			echo " #### ERROR: Queue $qid output file does not exists ####" >> $logfile
			cleanup_queue vf$vf $qid st
			exit -1
		fi

		# check files size
		filesize=$(stat -c%s "$out")
		if [ $filesize -gt $tsize ]; then
			echo "#### ERROR: Queue $qid output file size does not match, filesize= $filesize ####"
			echo "#### ERROR: Queue $qid output file size does not match, filesize= $filesize ####" >> $logfile
			cleanup_queue vf$vf $qid st
			exit -1 
		fi

		#compare file
		cmp $out $infile -n $tsize
		if [ $? -eq 1 ]; then
			echo "#### Test ERROR. Queue $qid data did not match ####" 
			echo "#### Test ERROR. Queue $qid data did not match ####" >> $logfile
			dmactl qdmavf0 q dump idx $qid mode st dir c2h
			dmactl qdmavf0 reg dump
			cleanup_queue vf$vf $qid st
			exit -1
		else
                        echo "######################################################"
			echo "##############   VF C2H ST PASS QID $qid ################"
                        echo "######################################################"
		fi
		wait
		((loop++))
	done
	cleanup_queue vf$vf $qid st > /dev/null
done
echo "########################## AXI-ST completed ###################################"
exit 0

