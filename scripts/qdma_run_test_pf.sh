#!/bin/bash
#
# Simple run script to test QDMA in AXI-MM and AXI-St mode.
# 
# AXI-MM Transfer
#	First H2C operation is performed for 1KBytes, this will write 1Kbytes of data to BRAM on card side. 
#	Then C2H operation is performed for 1KBytes. DMA reads data from BRAM and will transfer
#	to local file 'out_mm0_0', which will be compared to original file for correctness. 
#
# AXI-ST H2C Transfer
#	for H2C Streaming transfer data needs to be a per-defined data. 16 bit incremental data. 
#	Data file is provided with the script. 
#	H2C operation is performed, Data is read from Host memory and send to Card side. There is a data checker
#	on the card side which will check the data for correctness and will log the result in a register.
#	Script then read the register to check for results.
#	
#
# AXI-ST C2H Transfer
#	For C2H operation there is a data generator on the Card side which needs to be setup to generate data.
#	Qid, transfer length and number of paket are written before C2H transfer. Then 
#	C2H transfer is started by writing to register. C2H operation is completed and the data is written to 'out_st0_0"
#	file which then is compared to a per-defined data file. The data generator will only generate pre-defined 
#	data, so data comparison will need to be done with 'datafile_16bit_pattern.bin' file only.
#
# 


pf=0
qid=0
size=1024
num_qs=4


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

infile='./datafile_16bit_pattern.bin'

# Find user bar
tmp=`dmactl qdma0 reg read bar 0 0x10C | grep "0x10c" | cut -d '=' -f2 | cut -d 'x' -f2 | cut -d '.' -f1`
echo "$tmp"
bar_ext=$(printf '%x\n' "$(( 0x$tmp & 0x00000f ))")

if [ $bar_ext -eq 2 ]; then
	usr_bar=1
elif [ $bar_ext -eq 4 ];then
	usr_bar=2
fi
echo "$bar_ext, usr_bar=$usr_bar"


for ((i=0; i< $num_qs; i++)) do
	# Setup for Queues
	qid=$i
	dev_mm_c2h="/dev/qdma$pf-MM-$qid"
	dev_mm_h2c="/dev/qdma$pf-MM-$qid"

	out_mm="out_mm"$pf"_"$qid
	# Open the Queue for AXI-MM streaming interface.
	queue_start $pf $qid mm

	# H2C transfer 
	dma_to_device -d $dev_mm_h2c -f $infile -s $size
	# C2H transfer
	dma_from_device -d $dev_mm_c2h -f $out_mm -s $size
	# Compare file for correctness
	cmp $out_mm $infile -n $size
	if [ $? -eq 1 ]; then
		echo "#### Test ERROR. Queue $qid data did not match ####"
		exit 1
	else
		echo "**** Test pass. Queue $qid"
	fi
	# Close the Queues
	cleanup_queue $pf $qid
done
echo "**** AXI-MM completed ****"



echo "**** AXI-ST H2C Start ****"
# AXI-ST H2C transfer

for ((i=0; i< $num_qs; i++)) do
	# Setup for Queues
	qid=$i
	queue_start $pf $qid st # open the Queue for AXI-ST streaming interface.

	dev_st_h2c="/dev/qdma$pf-ST-$qid"

	# Clear H2C match from previous runs. this register is in card side.
	# MAtch clear register is on offset 0x0C 
	dmactl qdma0 reg write bar $usr_bar 0x0C 0x1 # clear h2c Match register.

	# do H2C Transfer
	dma_to_device -d $dev_st_h2c -f $infile -s $size

	# check for H2C data match. MAtch register is in offset 0x10.
	pass=`dmactl qdma0 reg read bar $usr_bar 0x10 | grep "0x10" | cut -d '=' -f2 | cut -d 'x' -f2 | cut -d '.' -f1`
	# convert hex to bin
	code=`echo $pass | tr 'a-z' 'A-Z'`
  	val=`echo "obase=2; ibase=16; $code" | bc`
  	echo "$val "
	check=1
	if [ $(($val & $check)) -eq 1 ];then
		echo "*** Test passed ***"
	else
		echo "#### ERROR Test failed. pattern did not match ####"
		cleanup_queue $pf $qid
		exit 1
	fi
	cleanup_queue $pf $qid
done

echo "**** AXI-St H2C completed ****"


echo "**** AXI-ST C2H Start ****"
qid=0
out_st="out_st"$pf"_"$qid
num_pkt=1 #number of packets not more then 64


for ((i=0; i< $num_qs; i++)) do
	# Setup for Queues
	qid=$i
	# Each PF is assigned with 32 Queues. PF0 has queue 0 to 31, PF1 has 32 to 63 
	
	# Write QID in offset 0x00 
	hw_qid=$(($qid + $(($pf*512)) ))
	dmactl qdma$pf reg write bar $usr_bar 0x0 $hw_qid  # for Queue 0

	# open the Queue for AXI-ST streaming interface.
	queue_start $pf $qid st 

	dev_st_c2h="/dev/qdma$pf-ST-$qid"
	let "tsize= $size*$num_pkt" # if more packets are requested.
	
	# Write transfer size to offset 0x04
	dmactl qdma$pf reg write bar $usr_bar 0x4 $size

	# Write number of packets to offset 0x20
	dmactl qdma$pf reg write bar $usr_bar 0x20 $num_pkt 

	# Write to offset 0x80 bit [1] to trigger C2H data generator. 
	dmactl qdma$pf reg write bar $usr_bar 0x08 2
	# do C2H transfer 
	dma_from_device -d $dev_st_c2h -f $out_st -s $tsize

	cmp $out_st $infile -n $tsize
	if [ $? -eq 1 ]; then
		echo "#### Test ERROR. Queue $2 data did not match ####" 
		dmactl qdma$pf q dump idx $qid dir c2h
		dmactl qdma$pf reg dump > reg_dump_after_error.txt
		cleanup_queue $pf $qid
		exit 1
	else
		echo "**** Test pass. Queue $qid"
	fi
	# Close the Queues
	cleanup_queue $pf $qid
done
echo "**** AXI-St C2H completed ****"

exit 0


