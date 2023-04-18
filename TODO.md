# Self TODO List
Goal: IHT Operations without rehashing

[x] done

Goal: IHT Operations with rehashing

[x] done

Goal: Refining my IHT repo

1. Populate function
2. Concise functions
3. Integrate experiment.proto
4. Look into using docker as a build environment
5. Alignment warnings in INIT

Things to look into:

* Rehashing the same key leads to a similar value. As a result, we are "stuck" using certain paths, causing much more collisions. Fixed by hashing by count-1. Avoid "Given mod A, find mod 2A issue". Update this to a better way later.
* How does Allocate work? Can I provide parameters in it.
* Can I setup a MemoryPool with self as a peer
* How does compare & swap work?
* Read makes a copy? Can I do pointer arithmetic?

Q1: Memory Pool with self as a Peer

Issues:

How do I deallocate the EList (upon rehashing) if the owner is not me? Maybe I need to implement a lazy solution?
