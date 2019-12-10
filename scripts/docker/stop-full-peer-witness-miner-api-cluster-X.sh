#!/bin/bash

export subnet=172.20.0.
witnesses="$(cat ./contrib/credentials/witnesses.txt | awk -F' ' '{print $1}')"
witnesses=($witnesses)
export peerCount=${1:-${#witnesses[@]}}
keys="$(cat ./contrib/credentials/witnesses.txt | awk -F' ' '{print $2}')"
keys=($keys)
nodescount=0
for ((i=0;i<$peerCount;i++)) ; do
	if [[ ! -z "${witnesses[$i]}" ]] ; then
		echo "#### LOOP "$i" ####"
		echo "stopping docker container wenode"$i" with witness "${witnesses[$i]}
		# docker run --env USE_FULLNODE=1 --env WITNESS_NAME=${witnesses[$i]} --env PRIVATE_KEY=${keys[$i]} --env MINER_NAME=${witnesses[$i]} -d --name wenode$i weyoume/wenode
		docker stop wenode$i & 
		((nodescount++))
		sleep 1
	fi
done

echo "ALL DONE :D you stopped "$nodescount" nodes out of "$peerCount" desired"