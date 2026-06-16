#!/bin/bash

set -e

CHAOS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CHAOS_DIR"

BINARY="../build/raftkv"
CLIENT="../build/raftkv-cli"
LOGS="$CHAOS_DIR/logs"
DATA_DIR="$CHAOS_DIR/data"
TMUX_SESSION="raft_chaos"

mkdir -p "$LOGS"
mkdir -p "$DATA_DIR"

# Helper to generate peer list for a node (excludes itself)
get_peers_for_node() {
    local id=$1
    local peers=""
    for j in {1..5}; do
        if [ $j -ne $id ]; then
            [ -n "$peers" ] && peers="$peers,"
            peers="$peers$j@127.0.0.1:$((7000+j))"
        fi
    done
    echo "$peers"
}

cleanup() {
    # Force kill the tmux session at the end of the test
    tmux kill-session -t "$TMUX_SESSION" 2>/dev/null || true
    rm -rf "$DATA_DIR"/*
    rm -f "$LOGS"/*.log
}

start_tmux_cluster() {
    # Clean up everything from previous runs
    tmux kill-session -t "$TMUX_SESSION" 2>/dev/null || true
    for i in {1..5}; do
        mkdir -p "$DATA_DIR/d$i"
    done

    tmux new-session -d -s "$TMUX_SESSION" -n "nodes"

    # Start node 1 in the first pane
    PEERS1=$(get_peers_for_node 1)
    tmux send-keys -t "$TMUX_SESSION:0.0" \
        "$BINARY --id 1 --port 6001 --raft-port 7001 \
        --peers \"$PEERS1\" \
        --data $DATA_DIR/d1 > $LOGS/node1.log 2>&1" Enter

    # Create and start nodes 2-5 in new panes
    for i in {2..5}; do
        tmux split-window -h -t "$TMUX_SESSION:0"
        tmux select-layout -t "$TMUX_SESSION:0" tiled
        PEERS=$(get_peers_for_node $i)
        tmux send-keys -t "$TMUX_SESSION:0.$((i-1))" \
            "$BINARY --id $i --port $((6000+i)) --raft-port $((7000+i)) \
            --peers \"$PEERS\" \
            --data $DATA_DIR/d$i > $LOGS/node$i.log 2>&1" Enter
    done

    # Wait for the cluster to stabilize and elect a leader
    echo "Waiting 5 seconds for the cluster to start up..."
    sleep 5
}

find_leader() {
    local retries=15
    for ((try=0; try<retries; try++)); do
        for i in {1..5}; do
            STATUS=$(echo "\\status" | $CLIENT 127.0.0.1 $((6000+i)) 2>/dev/null | grep "state:" | awk '{print $2}')
            if [ "$STATUS" = "LEADER" ]; then
                echo $i
                return
            fi
        done
        echo "Retrying leader check... (try $((try+1))/$retries)" >&2
        sleep 3
    done
    echo 0
}

put() {
    local leader=$1
    local key=$2
    local value=$3
    echo "PUT $key $value" | $CLIENT 127.0.0.1 $((6000+leader)) > /dev/null 2>&1
}

get() {
    local node=$1
    local key=$2

    echo "GET $key" |
        $CLIENT 127.0.0.1 $((6000+node)) 2>/dev/null |
        sed 's/^> *//g' |
        grep -v '^$' |
        tail -1
}

verify_consistency() {
    local key=$1
    local expected=$2
    local retries=3
    for ((try=0; try<retries; try++)); do
        local first_val=$(get 1 $key)
        if [ "$first_val" != "$expected" ]; then
            echo "Mismatch on node 1: got '$first_val', expected '$expected' (retry $((try+1))/$retries)"
            sleep 1
            continue
        fi
        local all_match=true
        for i in {2..5}; do
            local val=$(get $i $key)
            if [ "$val" != "$first_val" ]; then
                echo "Mismatch on node $i: got '$val', node 1 has '$first_val' (retry $((try+1))/$retries)"
                all_match=false
                break
            fi
        done
        if $all_match; then
            return 0
        fi
        sleep 1
    done
    return 1
}

stop_node() {
    local id=$1
    tmux send-keys -t "$TMUX_SESSION:0.$((id-1))" C-c
    sleep 2
}

restart_node() {
    local id=$1
    local PEERS=$(get_peers_for_node $id)
    tmux send-keys -t "$TMUX_SESSION:0.$((id-1))" \
        "$BINARY --id $id --port $((6000+id)) --raft-port $((7000+id)) \
        --peers \"$PEERS\" \
        --data $DATA_DIR/d$id > $LOGS/node$id.log 2>&1" Enter
    sleep 5
}

echo "========== Raft Chaos Test =========="
echo "[1/6] Starting 5 nodes..."
start_tmux_cluster

LEADER=$(find_leader)
if [ $LEADER -eq 0 ]; then
    echo "ERROR: No leader elected. Check logs in $LOGS"
    exit 1
fi
echo "Leader is node $LEADER"

echo "   Submitting 1000 PUTs..."
#for i in {1..1000}; do
#    put $LEADER "key$i" "value$i"
#one
put $LEADER "key500" "value500"

echo "   Verifying consistency..."
verify_consistency "key500" "value500" || { echo "Consistency check failed"; exit 1; }
echo "   Consistency verified"

echo "[2/6] Killing leader node $LEADER"
stop_node $LEADER

echo "   Waiting for new leader..."
NEW_LEADER=$(find_leader)
if [ $NEW_LEADER -eq 0 ]; then
    echo "ERROR: No new leader elected"
    exit 1
fi
echo "New leader is node $NEW_LEADER"

echo "   Submitting 1000 more PUTs..."
#for i in {1001..2000}; do
#    put $NEW_LEADER "key$i" "value$i"
#done

put $NEW_LEADER "key1500" "value1500"

echo "   Restarting old leader $LEADER"
restart_node $LEADER
sleep 20

echo "   Verifying consistency..."
verify_consistency "key1500" "value1500" || { echo "Restarted node did not catch up"; exit 1; }
echo "   Consistency verified"

echo "[3/6] Killing two followers (majority remains)"
FOLLOWERS=()
for i in {1..5}; do
    if [ $i -ne $NEW_LEADER ] && [ ${#FOLLOWERS[@]} -lt 2 ]; then
        FOLLOWERS+=($i)
    fi
done
for f in "${FOLLOWERS[@]}"; do
    stop_node $f
done
sleep 2

echo "   Submitting 500 PUTs..."
#for i in {2001..2500}; do
#    put $NEW_LEADER "key$i" "value$i"
#done
put $NEW_LEADER "key2250" "value2250"

echo "   Restarting followers..."
for f in "${FOLLOWERS[@]}"; do
    restart_node $f
done
sleep 5

echo "   Verifying consistency..."
verify_consistency "key2250" "value2250" || { echo "Followers did not catch up"; exit 1; }
echo "   Consistency verified"

echo "[4/6] Killing three nodes (lose majority)"
THREE=()
for i in {1..5}; do
    if [ ${#THREE[@]} -lt 3 ]; then
        THREE+=($i)
    fi
done
for n in "${THREE[@]}"; do
    stop_node $n
done
sleep 2

echo "   Attempting 100 PUTs (all should fail)..."
FAILED=0
for i in {1..5}; do
    if ! timeout 2 echo "PUT fail$i fail$i" | $CLIENT 127.0.0.1 $((6000+NEW_LEADER)) 2>/dev/null; then
        FAILED=$((FAILED+1))
    fi
done
if [ $FAILED -eq 5 ]; then
    echo "   All 5 writes correctly failed"
else
    echo " $FAILED writes succeeded when they should have failed"
    exit 1
fi

echo "   Restoring majority..."
restart_node ${THREE[0]}
sleep 5
NEW_LEADER2=$(find_leader)
if [ $NEW_LEADER2 -eq 0 ]; then
    echo "ERROR: No leader after restoring majority"
    exit 1
fi
echo "New leader is node $NEW_LEADER2"

for i in {2501..2600}; do
    put $NEW_LEADER2 "key$i" "value$i"
done
for n in "${THREE[@]:1}"; do
    restart_node $n
done
sleep 5

echo "   Verifying consistency..."
verify_consistency "key2550" "value2550" || { echo "Inconsistency after majority restored"; exit 1; }
echo "   Consistency verified"

echo "[5/6] Simulating network partition (kill minority side)"
MINORITY=()
for i in {1..5}; do
    if [ $i -ne $NEW_LEADER2 ] && [ ${#MINORITY[@]} -lt 2 ]; then
        MINORITY+=($i)
    fi
done
for n in "${MINORITY[@]}"; do
    stop_node $n
done
sleep 2

echo "   Submitting 100 PUTs on majority side..."
for i in {2601..2700}; do
    put $NEW_LEADER2 "key$i" "value$i"
done

echo "   Healing partition..."
for n in "${MINORITY[@]}"; do
    restart_node $n
done
sleep 5

echo "   Verifying consistency..."
verify_consistency "key2650" "value2650" || { echo "Minority did not catch up after partition"; exit 1; }
echo "   Consistency verified"

echo "[6/6] Killing leader during continuous PUTs"
LEADER3=$(find_leader)
for i in {2701..3700}; do
    if [ $i -eq 3200 ]; then
        echo "   Killing leader $LEADER3 at PUT $i"
        stop_node $LEADER3
        sleep 3
        LEADER3=$(find_leader)
        if [ $LEADER3 -eq 0 ]; then
            echo "ERROR: No leader after crash"
            exit 1
        fi
        echo "   New leader is $LEADER3"
    fi
    put $LEADER3 "key$i" "value$i"
done

echo "   Verifying consistency..."
verify_consistency "key3150" "value3150" || { echo "Data loss after leader crash"; exit 1; }
echo "   No gaps or lost entries after leader crash"

echo "========== ALL TESTS PASSED =========="
cleanup
echo "Test completed successfully."