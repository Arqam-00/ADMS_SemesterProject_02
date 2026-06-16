# ADMS_SemesterProject_02

- This Project is on Raft

- Raft is a consensues Algorithm
In this Project we Implemented Raft on 5 Nodes , These Nodes are individual servers that are interconnected with each other and the data is replicated among the servers such as if a server crashes , the data is not lost.
 
- How Raft Achieves This
When a Transaction or command is sent to a server , No matter from what Node it came to , it goes directly to the leader of current Term , the leader is responsible for serielizing these commands , The leader orders what command to be applied to the state machine , but before the command is Applied to state machine , a log of that command is created and is stored in a persistent storage after the Leader Orders the command to be applied to rest of nodes that also keep the log of that command and these nodes then send a msg that the command has been applied and if majority of the nodes apply that certain command , then leader declares that command's log to be at the index it currently is and no other log is allowed to take that index. This way even if a server crashes or a new node is made , it can catch up to current nodes by asking for that log and applying it to its state machine

- How leaders are Voted
    If a leader crashes , the nodes do not know what command to replicate and how , so for serialization and consistency , they momentarily stop receiving any commands and do a voting to determin the next leader , The next leader to be selected depends on two factors:
    1) how far the commited index
    2) how lucky or unlucky the node is
    Each node gets a randome timeout 300-500 ms
    When this much time has passed after last harbeat by leader(that informs of the leader is still alive) , the node becomes a candidate and asks for vote from all other nodes , if it gets the majority vote then the Term is changes and it becomes the Leader of this term , if more than one becomes candidate and nether gets majority vote then terms is changed and no one is leader for that term and elections are held till a Leader Emerges.



- How to run a cluster in this current implimentation
commands:
nessecary permissions:
    chmod +x Builder.sh
    chmod +x Start_cluster.sh
    chmod +x chaos/chaos_test.sh

Building:
    Builder.sh has neccessary commands to build the binary files
    Start_cluster the uses tmux to Start a cluster of 5 nodes and a client window

Recommended:
    tmux extension for navigating

To test the extent of its power:
    chaos/chaos_test.sh

quick check:
    chaos/mini.sh