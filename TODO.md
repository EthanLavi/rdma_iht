# Self TODO List
Goal: IHT Operations without rehashing

[x] done

Goal: IHT Operations with rehashing

[x] done

Goal: Refining my IHT repo

1. Add more threads/nodes (test to be sure this works)
2. Add export statistics
3. Change name of client to `worker` and server to `manager`
4. Conciser functions
5. Reallocation should be 2n-1 and not 2n
6. Fix deallocation issue with EList (leaks memory)


Things to look into:

* Rehashing the same key leads to a similar value. As a result, we are "stuck" using certain paths, causing much more collisions. Fixed by hashing by count-1. Avoid "Given mod A, find mod 2A issue". Update this to a better way method later.

Issues:

How do I deallocate the EList (upon rehashing) if the owner is not me? 

* Maybe I need to implement a lazy solution? 
* Better idea would be each node maintains a linkedlist or some data structure of elists to be freed. Each node can periodically iterate on it to make a free.
