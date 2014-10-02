#!/bin/sh

usage()
{
	cat << END_OF_HELP
<master node>
Fetch split configuration from a master node.

NOTE: A configuration variable called "fetch_name" is
required in the object_config section of merlin.cfg on the
poller. The variable should be set to the name of the poller,
as seen by the master.

The variable can also be called "oconf_fetch_name" and must
then reside inside the "daemon" section of merlin.cfg on the
poller.
END_OF_HELP
exit
}

source=
while test "$#" -ne 0; do
	case "$1" in
	--help|-h)
		usage
	;;
	*)
		echo "Fetch config from node: $1"
		oconf_ssh_port=false
		source=$(mon node show "$1" | sed -n 's/^ADDRESS=//p')
		oconf_ssh_port=$(mon node show "$1" | sed -n 's/^OCONF_SSH_PORT=//p')
		fetch_name=$(mon node show "$1" | sed -n 's/^OCONF_FETCH_NAME=//p')
		if [ $oconf_ssh_port ]; then
			if [ ! $oconf_ssh_port -eq $oconf_ssh_port 2>/dev/null ]; then
				source="$source"
			else
				source="$source:$oconf_ssh_port"
			fi
		fi
	;;
	esac
	shift
done

ssh_prefix=
scp_prefix=
dest=

for dest in $source; do
	master=$(echo $dest | cut -d':' -f 1)
	oconf_ssh_port=$(echo $dest | cut -d':' -f 2)

	if [ $oconf_ssh_port -eq $oconf_ssh_port 2>/dev/null ]; then
		ssh_prefix="-p $oconf_ssh_port"
		scp_prefix="-P $oconf_ssh_port"
	else
		ssh_prefix=
		scp_prefix=
	fi

if [ $dest ]; then
	echo "Fetching config from $source"
	echo "ssh $ssh_prefix root@$master mon oconf nodesplit"
	ssh $ssh_prefix root@$master mon oconf nodesplit

	echo "Verify if a update is needed"
	local_md5=$(cat /opt/monitor/etc/oconf/from-master.cfg | md5sum)
	remote_md5=$(ssh $ssh_prefix root@$master cat /var/cache/merlin/config/$fetch_name.cfg | md5sum)

	if [ "$local_md5" == "$remote_md5" ]; then
		echo "Same md5sum, no need for a fetch, exiting"
		exit
	else
		echo "scp -p $scp_prefix root@$master:/var/cache/merlin/config/$fetch_name.cfg /opt/monitor/etc/oconf/from-master.cfg"
		scp -p $scp_prefix root@$master:/var/cache/merlin/config/$fetch_name.cfg /opt/monitor/etc/oconf/from-master.cfg
		echo "Restarting local instance"
		mon restart
	fi
fi
done
