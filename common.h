#pragma once

#define CONTAINS 0
#define INSERT 1
#define REMOVE 2
#define CNF_ELIST_SIZE 7 // 7
#define CNF_PLIST_SIZE 128 // 128

/// @brief IHT_Op is used by the Client Adaptor to pass in operations to Apply, by forming a stream of IHT_Ops.
template <typename K, typename V>
struct IHT_Op {
    int op_type;
    K key;
    V value;
    IHT_Op(int op_type_, K key_, V value_) : op_type(op_type_), key(key_), value(value_) {};
};

/// @brief Output for IHT that includes a status and a value
struct IHT_Res {
    bool status;
    int result;

    IHT_Res(bool status, int result){
        this->status = status;
        this->result = result;
    }
};
