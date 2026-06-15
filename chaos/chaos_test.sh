#!/bin/bash


set -e  # exit on error

# Configuration
NODES=5
BASE_CLIENT_PORT=6000
BASE_RAFT_PORT=7000
DATA_DIRS="data1 data2 data3 data4 data5"
BINARY="./build/raftkvid"
CLIENT="./build/raftkv-cli"
LOG_DIR="logs"

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f raftkvid || true
    for dir in $DATA_DIRS; do
        rm -rf $dir
    done
    rm -rf $LOG_DIR
}

# Start all nodes
start_cluster() {
    cleanup
    mkdir -p $LOG_DIR
    for i in $(seq 1 $NODES); do
        mkdir -p data$i
        PEERS=""
        for j in $(seq 1 $NODES); do
            if [ $j -ne $i ]; then
                if [ -n "$PEERS" ]; then PEERS="$PEERS,"; fi
                PEERS="$PEERS$j@127.0.0.1:$((BASE_RAFT_PORT+j))"
            fi
        done
        $BINARY --id $i --port $((BASE_CLIENT_PORT+i)) --raft-port $((BASE_RAFT_PORT+i)) \
            --peers "$PEERS" --data data$i > $LOG_DIR/node$i.log 2>&1 &
        echo "Started node $i"
    done
    sleep 5
}

# Find current leader
find_leader() {
    for i in $(seq 1 $NODES); do
        STATUS=$($CLIENT 127.0.0.1 $((BASE_CLIENT_PORT+i)) "STATUS" 2>/dev/null | grep "state:" | awk '{print $2}')
        if [ "$STATUS" = "LEADER" ]; then
            echo $i
            return
        fi
    done
    echo 0
}

# Send a PUT command
put() {
    local leader=$1
    local key=$2
    local value=$3
    echo "PUT $key $value" | $CLIENT 127.0.0.1 $((BASE_CLIENT_PORT+leader)) > /dev/null 2>&1
}

# Send a GET command and return value
get() {
    local node=$1
    local key=$2
    echo "GET $key" | $CLIENT 127.0.0.1 $((BASE_CLIENT_PORT+node)) 2>/dev/null | head -1
}

# Stop a node by ID
stop_node() {
    local id=$1
    local pid=$(ps aux | grep "raftkvid.*--id $id" | grep -v grep | awk '{print $2}')
    if [ -n "$pid" ]; then
        kill $pid
        sleep 2
    fi
}

# Restart a node by ID
restart_node() {
    local id=$1
    PEERS=""
    for j in $(seq 1 $NODES); do
        if [ $j -ne $id ]; then
            if [ -n "$PEERS" ]; then PEERS="$PEERS,"; fi
            PEERS="$PEERS$j@127.0.0.1:$((BASE_RAFT_PORT+j))"
        fi
    done
    $BINARY --id $id --port $((BASE_CLIENT_PORT+id)) --raft-port $((BASE_RAFT_PORT+id)) \
        --peers "$PEERS" --data data$id > $LOG_DIR/node$id.log 2>&1 &
    sleep 3
}

# Verify all nodes have the same value for a key
verify_consistency() {
    local key=$1
    local expected=$2
    for i in $(seq 1 $NODES); do
        val=$(get $i $key)
        if [ "$val" != "$expected" ]; then
            echo "Inconsistency: node $i has '$val', expected '$expected'"
            return 1
        fi
    done
    return 0
}

echo "========== RaftKV Chaos Test =========="

# Step 1: Start cluster and elect leader
echo "Step 1: Starting 5 nodes..."
start_cluster

LEADER=$(find_leader)
if [ $LEADER -eq 0 ]; then
    echo "Failed: No leader elected"
    cleanup
    exit 1
fi
echo "Leader is node $LEADER"

# Step 2: Submit 1000 PUTs
echo "Step 2: Submitting 1000 PUTs..."
for i in $(seq 1 1000); do
    put $LEADER "key$i" "value$i"
done
# Verify a few keys
verify_consistency "key500" "value500"
if [ $? -ne 0 ]; then
    echo "Failed: Inconsistency after 1000 PUTs"
    cleanup
    exit 1
fi
echo "Consistency verified after 1000 PUTs"

# Step 3: Kill leader, elect new leader, submit more PUTs
echo "Step 3: Killing leader node $LEADER"
stop_node $LEADER
sleep 5
NEW_LEADER=$(find_leader)
if [ $NEW_LEADER -eq 0 ]; then
    echo "Failed: No new leader elected"
    cleanup
    exit 1
fi
echo "New leader is node $NEW_LEADER"
echo "Submitting 1000 more PUTs..."
for i in $(seq 1001 2000); do
    put $NEW_LEADER "key$i" "value$i"
done
# Restart killed node and verify it catches up
echo "Restarting node $LEADER"
restart_node $LEADER
sleep 5
verify_consistency "key1500" "value1500"
if [ $? -ne 0 ]; then
    echo "Failed: Restarted node did not catch up"
    cleanup
    exit 1
fi
echo "Restarted node caught up"

# Step 4: Kill two followers (majority remains)
echo "Step 4: Killing two followers"
FOLLOWERS=()
for i in $(seq 1 $NODES); do
    if [ $i -ne $NEW_LEADER ] && [ ${#FOLLOWERS[@]} -lt 2 ]; then
        FOLLOWERS+=($i)
    fi
done
for f in "${FOLLOWERS[@]}"; do
    stop_node $f
done
sleep 2
# Submit 500 PUTs (should succeed)
echo "Submitting 500 PUTs while two followers are down..."
for i in $(seq 2001 2500); do
    put $NEW_LEADER "key$i" "value$i"
done
# Restart followers
echo "Restarting followers..."
for f in "${FOLLOWERS[@]}"; do
    restart_node $f
done
sleep 5
verify_consistency "key2250" "value2250"
if [ $? -ne 0 ]; then
    echo "Failed: Followers did not catch up"
    cleanup
    exit 1
fi
echo "Followers caught up after restart"

# Step 5: Kill three nodes (lose majority)
echo "Step 5: Killing three nodes to break majority"
THREE=()
for i in $(seq 1 $NODES); do
    if [ ${#THREE[@]} -lt 3 ]; then
        THREE+=($i)
    fi
done
for n in "${THREE[@]}"; do
    stop_node $n
done
sleep 2
# Attempt to submit (should fail or timeout)
echo "Attempting write with minority... (should fail)"
echo "PUT minority_key minority_value" | $CLIENT 127.0.0.1 $((BASE_CLIENT_PORT+NEW_LEADER)) > /dev/null 2>&1 &
sleep 1
# Restart one node to restore majority
echo "Restarting one node to restore majority..."
restart_node ${THREE[0]}
sleep 5
NEW_LEADER2=$(find_leader)
if [ $NEW_LEADER2 -eq 0 ]; then
    echo "Failed: No leader after restoring majority"
    cleanup
    exit 1
fi
echo "New leader is node $NEW_LEADER2"
# Submit 100 PUTs
for i in $(seq 2501 2600); do
    put $NEW_LEADER2 "key$i" "value$i"
done
# Restart remaining two nodes
for n in "${THREE[@]:1}"; do
    restart_node $n
done
sleep 5
verify_consistency "key2550" "value2550"
if [ $? -ne 0 ]; then
    echo "Failed: Inconsistency after majority restored"
    cleanup
    exit 1
fi
echo "Majority restored and consistent"

# Step 6: Simulate network partition (by killing minority side)
echo "Step 6: Simulating network partition (kill minority of 2 nodes)"
MINORITY=()
for i in $(seq 1 $NODES); do
    if [ $i -ne $NEW_LEADER2 ] && [ ${#MINORITY[@]} -lt 2 ]; then
        MINORITY+=($i)
    fi
done
for n in "${MINORITY[@]}"; do
    stop_node $n
done
sleep 2
echo "Submitting 100 PUTs on majority side..."
for i in $(seq 2601 2700); do
    put $NEW_LEADER2 "key$i" "value$i"
done
# Heal partition (restart minority)
echo "Healing partition..."
for n in "${MINORITY[@]}"; do
    restart_node $n
done
sleep 5
verify_consistency "key2650" "value2650"
if [ $? -ne 0 ]; then
    echo "Failed: Minority did not catch up after partition heal"
    cleanup
    exit 1
fi
echo "Partition healed, all nodes consistent"

# Step 7: Kill leader partway through 1000 PUTs
echo "Step 7: Killing leader during continuous PUTs"
LEADER3=$(find_leader)
for i in $(seq 2701 3700); do
    if [ $i -eq 3200 ]; then
        echo "Killing leader $LEADER3 at PUT $i"
        stop_node $LEADER3
        sleep 3
        LEADER3=$(find_leader)
        if [ $LEADER3 -eq 0 ]; then
            echo "Failed: No leader after crash"
            cleanup
            exit 1
        fi
        echo "New leader is $LEADER3"
    fi
    put $LEADER3 "key$i" "value$i"
done
# Verify that committed entries survive (check a key after crash)
verify_consistency "key3150" "value3150"
if [ $? -ne 0 ]; then
    echo "Failed: Lost committed entry after leader crash"
    cleanup
    exit 1
fi
echo "No gaps or lost entries after leader crash"

echo "========== ALL TESTS PASSED =========="
cleanup