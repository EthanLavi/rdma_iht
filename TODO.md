# Self TODO List
Goal: IHT Operations without rehashing

[x] done

Goal: IHT Operations with rehashing

[x] done

Goal: Refining my IHT repo

1. Add export statistics
2. Change name of client to `worker` and server to `manager`
3. Reallocation should be 2n-1 and not 2n?
4. Improve speed of pointer refreshing. The error I've discovered is if a node is waiting for a EList to unlock. But while this is happening another node changes the EList to a PList, the original node will interpret its stale copy of the Base pointer as a PList, when in reality it is pointing to the old EList.
5. In addition READ must become ExtendedRead to deal with varying size of PList. Ask Professor about a cleaner solution than using depth.
6. Fix deallocation issue with EList (leaks memory)
7. Double check mempool safety after fixing data structure (don't remeber what I was thinking when I wrote this. Maybe think about thread safety? and improving speed?)
8. Conciser functions

Issues:

How do I deallocate the EList (upon rehashing) if the owner is not me? 

* Maybe I need to implement a lazy solution? 
* Better idea would be each node maintains a linkedlist or some data structure of elists to be freed. Each node can periodically iterate on it to make a free.
