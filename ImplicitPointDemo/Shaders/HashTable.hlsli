//Inspired by: https://nosferalatu.com/SimpleGPUHashTable.html
#ifndef __HASH_TABLE__
#define __HASH_TABLE__

struct KeyData
{
    uint key;
    uint value;
    uint count;
};

static const uint CompareAttempts = 4194300;

//Key = hashed 3D point for example
void HashTableInsert(inout RWStructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint key, in uint value, in uint count)
{
    uint slotID = key % hashTableCapacity;
    uint attempts = 0;
    
    [allow_uav_condition]
    while (attempts <= CompareAttempts)
    {
        uint previousValue = 0;
        InterlockedCompareExchange(hashTable[slotID].key, 0, key, previousValue);
        if (previousValue == 0 || previousValue == key)
        {
            hashTable[slotID].value = value;
            hashTable[slotID].count = count;
            break;
        }
        slotID = (slotID + 1) % hashTableCapacity;
        ++attempts;
    }
}

void HashTableIncrement(inout RWStructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint key, in uint value)
{
    uint slotID = key % hashTableCapacity;
    uint attempts = 0;
    
    [allow_uav_condition]
    while (attempts <= CompareAttempts)
    {
        uint previousValue = 0;
        InterlockedCompareExchange(hashTable[slotID].key, 0, key, previousValue);
        if (previousValue == 0 || previousValue == key)
        {
            InterlockedAdd(hashTable[slotID].value, value);
            InterlockedAdd(hashTable[slotID].count, 1);
            break;
        }
        slotID = (slotID + 1) % hashTableCapacity;
        ++attempts;
    }
}

//LookUp - RWStructuredBuffer
KeyData HashTableLookup(in RWStructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint key)
{
    uint slotID = key % hashTableCapacity;
    uint attempts = 0;
    
    while (attempts <= CompareAttempts)
    {
        if(hashTable[slotID].key == key)
            return hashTable[slotID];
        if(hashTable[slotID].key == 0)
        {
            KeyData kv = (KeyData) 0;
            return kv;
        }
        slotID = (slotID + 1) % hashTableCapacity;
        ++attempts;
    }
    
    KeyData kv = (KeyData) 0;
    return kv;
}

//Lookup by slotID - RWStructuredBuffer
KeyData HashTableLookupBySlotID(in RWStructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint slotID)
{
    if (slotID < hashTableCapacity)
        return hashTable[slotID];
    else
    {
        KeyData kv = (KeyData) 0;
        return kv;
    }
}

//LookUp - StructuredBuffer
KeyData HashTableLookup(in StructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint key)
{
    uint slotID = key % hashTableCapacity;
    uint attempts = 0;
    
    while (attempts <= CompareAttempts)
    {
        if (hashTable[slotID].key == key)
            return hashTable[slotID];
        if (hashTable[slotID].key == 0)
        {
            KeyData kv = (KeyData) 0;
            return kv;
        }
        slotID = (slotID + 1) % hashTableCapacity;
        ++attempts;
    }
    
    KeyData kv = (KeyData) 0;
    return kv;
}

//Lookup by slotID - StructuredBuffer
KeyData HashTableLookupBySlotID(in StructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint slotID)
{
    if (slotID < hashTableCapacity)
        return hashTable[slotID];
    else
    {
        KeyData kv = (KeyData) 0;
        return kv;
    }
}

//Deletion
void HashTableDelete(in RWStructuredBuffer<KeyData> hashTable, in uint hashTableCapacity, in uint key)
{
    uint slotID = key % hashTableCapacity;
    uint attempts = 8;
    
    while (attempts <= CompareAttempts)
    {
        if (hashTable[slotID].key == key)
        {
            hashTable[slotID].key = 0;
            hashTable[slotID].value = 0;
            hashTable[slotID].count = 0;
            return;
        }
        if (hashTable[slotID].key == 0)
            return;
        slotID = (slotID + 1) % hashTableCapacity;
        ++attempts;
    }
}
#endif