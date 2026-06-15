#!/bin/bash
set -euo pipefail

# Fixed-name containers
STATIC_CONTAINERS=(minio trino-coordinator)

# Discover worker containers via compose service label
get_workers() {
    docker ps --filter "label=com.docker.compose.service=worker" \
              --format '{{.Names}}'
}

get_containers() {
    printf '%s\n' "${STATIC_CONTAINERS[@]}"
    get_workers
}

get_veth() {
    local container=$1
    # Get the container's eth0 ifindex peer
    local ifindex=$(docker exec $container sh -c "cat /sys/class/net/eth0/iflink")
    # Match it to a host interface
    grep -l "^$ifindex$" /sys/class/net/*/ifindex 2>/dev/null | awk -F'/' '{print $5}'
}

reset_tc() {
    while IFS= read -r c; do
        [[ -z "$c" ]] && continue
        local veth=$(get_veth $c)
        sudo tc qdisc del dev $veth root 2>/dev/null || true
        echo "  ✓ reset $c ($veth)"
    done < <(get_containers)
}

reconfigure() {
    local bandwidth=$1

    if [[ $bandwidth == *gbit ]]; then
        bw_num=${bandwidth%gbit}
        burst_bytes=$(awk "BEGIN {printf \"%d\", ($bw_num * 1000000000 / 8) * 0.1}")
    elif [[ $bandwidth == *mbit ]]; then
        bw_num=${bandwidth%mbit}
        burst_bytes=$(awk "BEGIN {printf \"%d\", ($bw_num * 1000000 / 8) * 0.1}")
    else
        echo "Unsupported bandwidth format: $bandwidth" >&2
        exit 1
    fi

    if (( burst_bytes < 32768 )); then
        burst="32k"
    elif (( burst_bytes < 1048576 )); then
        burst="$((burst_bytes / 1024))k"
    else
        burst="$((burst_bytes / 1048576))m"
    fi

    echo "Applying: bandwidth=$bandwidth, burst=$burst"
    while IFS= read -r c; do
        [[ -z "$c" ]] && continue
        local veth=$(get_veth $c)
        echo "  $c -> $veth"
        sudo tc qdisc del dev $veth root 2>/dev/null || true
        sudo tc qdisc add dev $veth root handle 1: htb default 10 r2q 1
        sudo tc class add dev $veth parent 1: classid 1:10 htb rate $bandwidth burst $burst cburst $burst quantum 1514
        sudo tc qdisc add dev $veth parent 1:10 handle 10: netem delay 100us
        echo "  ✓ $c ($veth)"
    done < <(get_containers)
}

case "${1:?Usage: $0 <bandwidth|reset>}" in
    reset) reset_tc ;;
    *)     reconfigure "$1" ;;
esac
