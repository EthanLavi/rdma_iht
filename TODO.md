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
6. Improve speed of pointer refreshing. The error I've discovered is if a node is waiting for a EList to unlock. But while this is happening another node changes the EList to a PList, the original node will interpret its stale copy of the Base pointer as a PList, when in reality it is pointing to the old EList.
7. In addition READ must become ExtendedRead to deal with varying size of PList. Ask Professor about a cleaner solution than using depth.
8. Fix deallocation issue with EList (leaks memory)
9. Double check mempool safety after fixing data structure


Things to look into:

* Rehashing the same key leads to a similar value. As a result, we are "stuck" using certain paths, causing much more collisions. Fixed by hashing by count-1. Avoid "Given mod A, find mod 2A issue". Update this to a better way method later.

Issues:

How do I deallocate the EList (upon rehashing) if the owner is not me? 

* Maybe I need to implement a lazy solution? 
* Better idea would be each node maintains a linkedlist or some data structure of elists to be freed. Each node can periodically iterate on it to make a free.
