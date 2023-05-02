# Self TODO List
Goal: IHT Operations without rehashing

[x] done

Goal: IHT Operations with rehashing

[x] done

Goal: Refining my IHT repo

1. Populate function
2. Concise functions
3. Look into using docker as a build environment
4. Alignment warnings in INIT
5. Add more threads/nodes
6. Fix safety in lock
7. Reallocation should be 2n-1 and not 2n
8. Fix deallocation issue with EList

TMRW:
Make memory pool shared among threads
Rework to start using loopback... (maybe start with just the lock for now? Just to fix safety in lock)
Add metrics and export startegy

Things to look into:

* Rehashing the same key leads to a similar value. As a result, we are "stuck" using certain paths, causing much more collisions. Fixed by hashing by count-1. Avoid "Given mod A, find mod 2A issue". Update this to a better way later.
* How does Allocate work? Can I provide parameters in it.
* Can I setup a MemoryPool with self as a peer
* How does compare & swap work?
* Read makes a copy? Can I do pointer arithmetic?

Q1: Memory Pool with self as a Peer

Issues:

How do I deallocate the EList (upon rehashing) if the owner is not me? Maybe I need to implement a lazy solution?
