#!/bin/bash

# Kill any existing 'raft' session
tmux kill-session -t raft 2>/dev/null

# Start a new detached session
tmux new-session -d -s raft

# Set up 6 panes in a tiled layout (3x2 grid)
for i in {1..5}; do
    # Build peer list for node $i
    peers=""
    for j in {1..5}; do
        if [ $j -ne $i ]; then
            if [ -n "$peers" ]; then peers="$peers,"; fi
            peers="$peers$j@127.0.0.1:$((7000+j))"
        fi
    done
    # Create a new pane for each node
    tmux split-window -h -t raft:0
    tmux select-layout -t raft:0 tiled
    tmux send-keys -t raft:0.$((i-1)) "build/raftkv --id $i --port $((6000+i)) --raft-port $((7000+i)) --peers \"$peers\" --data data/d$i" Enter
done

# Create the 6th pane for the client
tmux split-window -h -t raft:0
tmux select-layout -t raft:0 tiled
tmux send-keys -t raft:0.5 "sleep 2" Enter
tmux send-keys -t raft:0.5 "build/raftkv-cli localhost 6001" Enter

# Attach to the session
tmux attach -t raft