if [ "$1" == "" ]; then
	echo "Expected argument (powersave/ performance)"
	exit
fi

max_proc=$(cat /proc/cpuinfo | grep processor | tail -n 1 | cut -d ":" -f 2)
mode=$1

echo Max proc: $max_proc
echo Setting mode: $mode

for i in $(seq 0 $max_proc)
do
	echo $mode | sudo tee -a /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor > /dev/null
	echo cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor = $(cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor)
done



