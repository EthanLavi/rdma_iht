#define CONTAINS 0
#define INSERT 1
#define REMOVE 2

/// @brief IHT_Op is used by the Client Adaptor to pass in operations to Apply, by forming a stream of IHT_Ops.
template <typename K, typename V>
struct IHT_Op {
    int op_type;
    K key;
    V value;
};