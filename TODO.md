# Self TODO List
Goal: IHT Operations without rehashing

[x] done

Goal: IHT Operations with rehashing

[x] done

Goal: Refining my IHT repo

1. Populate function
2. Fix OOM Error (i.e. answer my 5th question?)
3. Fix alignment in Plist and Elist
4. Concise functions
5. Integrate experiment.proto
6. Look into using docker as a build environment

Things to look into:

* Rehashing the same key leads to a similar value. As a result, we are "stuck" using certain paths, causing much more collisions. Fixed by hashing by count-1. Avoid "Given mod A, find mod 2A issue"
* How does Allocate work? Can I provide parameters in it.
* Can I setup a MemoryPool with self as a peer
* How does compare & swap work?
* Read makes a copy? Can I do pointer arithmetic?

Q1: OOM
Q2: Memory Pool with self as a Peer

Issues:

How do I deallocate the EList (upon rehashing) if the owner is not me? Maybe I need to implement a lazy solution?
