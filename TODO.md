# Self TODO List

[x] Version 1: Get IHT Operations without rehashing. Implications of this are no need to manage remote/local PLists.

Version 2: IHT Operations with rehashing
1. Implement rehash and refactor functions
2. Move away from static PLists
3. Improve workload driver
4. Memory Reclaimation (if at all)

Questions:
* How does Allocate work? Can I provide parameters in it.
* Can I setup a MemoryPool with self as a peer
* Read makes a copy? Can I do pointer arithmetic?
* Why is Launch.py not working?