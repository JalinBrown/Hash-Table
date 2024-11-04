/*!****************************************************************************
*\file     OAHashTable.cpp
*\author   Jalin A. Brown
*\brief An open-addressing-based hash table class that uses both linear probing
        (with ascending indexes) and double hashing to resolve collisions

******************************************************************************/

//-----------------------------------------------------------------------------
#include "OAHashTable.h"
//-----------------------------------------------------------------------------

/*!****************************************************************************
//! Hash table definition (open-addressing)
******************************************************************************/

//-----------------------------------------------------------------------------
//<! Public Functions:
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Constructor 
//-----------------------------------------------------------------------------
/**
 * @brief Initialize the table with the given configuration
 *
 * @tparam T     - Type of data to store in the table
 * @param config - Configuration for the hash table
 */
template <typename T>
OAHashTable<T>::OAHashTable(const OAHTConfig& config) {

  // Set the table size to the closest prime number to the initial table size
  tableSize = GetClosestPrime(config.InitialTableSize_);

  // Initialize the table
  table = new OAHTSlot[tableSize]();
  count = 0;
  probes = 0;
  expansions = 0;
  primary_hash_func = config.PrimaryHashFunc_;
  secondary_hash_func = config.SecondaryHashFunc_;
  max_load_factor = config.MaxLoadFactor_;
  growth_factor = config.GrowthFactor_;
  deletion_policy = config.DeletionPolicy_;
  freeProc = config.FreeProc_;
}

//-----------------------------------------------------------------------------
// Destructor 
//-----------------------------------------------------------------------------
/**
 * @brief Clear and delete the table
 *
 */
template <typename T>
OAHashTable<T>::~OAHashTable() {
  clear();
  delete[] table;
}

//-----------------------------------------------------------------------------
// Insert
//-----------------------------------------------------------------------------
/**
 * @brief Insert a key/data pair into the table.
 *        Throws an exception if the insertion is unsuccessful.
 *
 * @param Key   - Key to insert
 * @param Data  - Data to insert
 */
template <typename T>
void OAHashTable<T>::insert(const char* Key, const T& Data)
{
  // Check if the key is null before inserting  
  if (Key == nullptr)
  {
    throw OAHashTableException(OAHashTableException::E_NO_MEMORY, "Key cannot be null.");
  }

  // Check if the table needs to be resized
  if (CheckResizeRequired())
  {
    resizeTable(static_cast<double>(std::ceil(GetStats().TableSize_ * growth_factor)));
  }

  // Insert the key/data pair using linear probing
  linearProbeInsert(Key, Data);
}

//-----------------------------------------------------------------------------
// Remove
//-----------------------------------------------------------------------------
/**
 * @brief Delete an item by key. Throws an exception if the key doesn't exist.
 *
 * @param Key - Key to remove
 */
template<typename T>
void OAHashTable<T>::remove(const char* Key) {
  // Check if the key is null before removing
  if (Key == nullptr) {
    throw OAHashTableException(OAHashTableException::E_ITEM_NOT_FOUND, "Key not in table.");
  }
  
  unsigned index = primary_hash_func(Key, tableSize);
  unsigned originalIndex = index;
  unsigned stride = (secondary_hash_func) ? secondary_hash_func(Key, tableSize - 1) + 1 : 1;
  unsigned probesCount = 0;
  bool keyFound = false; // Flag to indicate if the key is found

  // Start linear probing to find the key to remove
  do {
    probesCount++;
    //<! Check if the current slot is occupied and its key matches the target key
    if (table[index].State == OAHTSlot::OCCUPIED && strcmp(table[index].Key, Key) == 0) 
    {
      keyFound = true;
      
      // If the slot contains data and a freeProc function is provided, delete the data
      if (table[index].data && freeProc != nullptr) {
        freeProc(table[index].data); // Delete the slot using FreeProc function
        table[index].data = nullptr;
      }
      
      // Depending on the deletion policy, handle slot deletion
      if (deletion_policy == OAHTDeletionPolicy::PACK) {
        table[index].State = OAHTSlot::UNOCCUPIED; // Mark slot as unoccupied
        packTable(index); // Compact the table
        count--;
      }
      else if (deletion_policy == OAHTDeletionPolicy::MARK) {
        table[index].State = OAHTSlot::DELETED; // Mark slot as deleted
        count--;
      }
      break; // Exit the loop after removing the key
    }
    //<! If the current slot is unoccupied, exit the loop
    else if (table[index].State == OAHTSlot::UNOCCUPIED) {
      break; 
    }

    index = (index + stride) % tableSize;
  } while (index != originalIndex); 

  probes += probesCount;

  // If the key was not found during linear probing, throw an exception
  if (!keyFound) {
    throw OAHashTableException(OAHashTableException::E_ITEM_NOT_FOUND, "Key not in table.");
  }
}

//-----------------------------------------------------------------------------
// Find 
//-----------------------------------------------------------------------------
/**
 * @brief Find and return data by key. Throws an exception if not found.
 *
 * @param Key        - Key to find
 * @return const T&  - Reference to the constant data
 */
template<typename T>
const T& OAHashTable<T>::find(const char* Key) const {
  if (Key == nullptr) {
    throw OAHashTableException(OAHashTableException::E_ITEM_NOT_FOUND, "Key cannot be null.");
  }

  unsigned originalIndex = primary_hash_func(Key, tableSize);
  unsigned index = originalIndex;
  unsigned stride = (secondary_hash_func) ? secondary_hash_func(Key, tableSize - 1) + 1 : 1;
  bool keyFound = false; // Flag to indicate if the key is found

  // Start linear probing to find the key to return
  do {
    ++probes; 
    if (table[index].State == OAHTSlot::OCCUPIED && strcmp(table[index].Key, Key) == 0)
    {
      keyFound = true;
      break; // Exit the loop if the key is found
    }
    else if (table[index].State == OAHTSlot::UNOCCUPIED)
    {
      break; // If the slot is unoccupied, the key doesn't exist in the table
    }
    else
    {
      index = (index + stride) % tableSize; // Move to the next slot
    }
  } while (index != originalIndex);

  if (keyFound)
  {
    return (table[index].data); // Return a reference to the constant data
  }
  else
  {
    throw OAHashTableException(OAHashTableException::E_ITEM_NOT_FOUND, "Item not found in table.");
  }
}

//-----------------------------------------------------------------------------
// Clear 
//-----------------------------------------------------------------------------
/**
 * @brief Removes all items from the table (Doesn't deallocate table)
 *
 */
template<typename T>
void OAHashTable<T>::clear() {
  for (unsigned i = 0; i < tableSize; i++) {
    if (table[i].State == OAHTSlot::OCCUPIED) {
      if (table[i].data) {
        if (freeProc != nullptr) {
          freeProc(table[i].data); // Delete the slot using user-provided FreeProc function
        }
        table[i].data = nullptr;
      }
    }
    table[i].State = OAHTSlot::UNOCCUPIED;
  }
  count = 0;  // Reset the count 
}

//-----------------------------------------------------------------------------
// GetStats 
//-----------------------------------------------------------------------------
/**
 * @brief Get the statistics of the table
 *
 * @return stats - The statistics of the table as an OAHTStats object
 */
template <typename T>
OAHTStats OAHashTable<T>::GetStats() const
{
  // Create a new OAHTStats object and set its values to the current table statistics
  OAHTStats stats;
  stats.TableSize_ = tableSize;
  stats.Probes_ = probes;
  stats.Count_ = count;
  stats.Expansions_ = expansions;
  stats.LoadFactor_ = static_cast<double>(count) / static_cast<double>(tableSize);
  stats.PrimaryHashFunc_ = primary_hash_func;
  stats.SecondaryHashFunc_ = secondary_hash_func;

  return stats; // Return the new OAHTStats object
}

//-----------------------------------------------------------------------------
// GetTable 
//-----------------------------------------------------------------------------
/**
 * @brief Return a pointer to the table
 *
 * @return table - A pointer to the table
 */
template <typename T>
const typename OAHashTable<T>::OAHTSlot* OAHashTable<T>::GetTable()
{
  return table;
}

//-----------------------------------------------------------------------------
//<! Private Functions:
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Insert helper - Linear Probe Insert
//-----------------------------------------------------------------------------
/**
 * @brief Insert a key/data pair into the table using linear probing.
 *
 * @param Key   - Key to insert
 * @param Data  - Data to insert
 */
template<typename T>
void OAHashTable<T>::linearProbeInsert(const char* Key, const T& Data) {
  // Get the original hash index
  unsigned originalIndex = primary_hash_func(Key, tableSize);
  unsigned index = originalIndex;

  // Get the stride from the secondary hash function 
  unsigned stride = (secondary_hash_func) ? // (if it exists) 
                    secondary_hash_func(Key, tableSize - 1) + 1 : 
                    1;  // or use 1 if it doesn't exist
   
  unsigned probesCount = 0; // Initialize the number of probes
  int deletedIndex = -1;    // and the index of the first deleted slot found

  // Start linear probing 
  do {
    ++probesCount; // Increment the probe count for each iteration

    //<! Check if the current slot is marked as deleted
    if (table[index].State == OAHTSlot::DELETED) 
    {
      // When the first deleted slot found, record its index index
      if (deletedIndex == -1) {
        deletedIndex = index;
      }
    }

    //<! Check if the current slot is unoccupied
    else if (table[index].State == OAHTSlot::UNOCCUPIED) 
    {
      // Check if a deleted slot has also already been found, 
      // if so, insert the new item at that deleted slot instead of this slot
      if (deletedIndex != -1) {
        // using the index of the first deleted slot found
        index = deletedIndex; 
        table[index].data = Data;
        strncpy(table[index].Key, Key, MAX_KEYLEN - 1);
        table[index].State = OAHTSlot::OCCUPIED;
        ++count;                // Increment the table item count
        probes += probesCount;  // Increment the total number of probes
        return;                 // Exit the function after insertion
      }

      // Otherwise, if the slot is unoocupied, 
      // and a deleted index has not been found,
      // insert the new item in this current unnocupied slot
      table[index].data = Data;
      strncpy(table[index].Key, Key, MAX_KEYLEN - 1);
      table[index].State = OAHTSlot::OCCUPIED;
      ++count;                  
      probes += probesCount;    
      return;                   // Exit the function after insertion
    }

    //<! Continue probing if the slot table[index].State == OAHTSlot::OCCUPIED
    index = (index + stride) % tableSize; // Move to the next slot
  } while (index != originalIndex);       
  // End linear probing when we loop back to the original index

  // Add the number of probes used in the loop to the total number of probes 
  // Works for both cases where the key is found or not found
  probes += probesCount;

  // If a deleted slot was found, insert the new item there
  if (deletedIndex != -1) {
    index = deletedIndex; // Use the index of the first deleted slot found
    table[index].data = Data;
    strncpy(table[index].Key, Key, MAX_KEYLEN - 1);
    table[index].State = OAHTSlot::OCCUPIED;
    ++count;                    
    return;                     // Exit the function after insertion
  }

  // In case of no insertion cases met, throw an exception
  throw OAHashTableException(OAHashTableException::E_NO_MEMORY, "Failed to insert item.");
}

//-----------------------------------------------------------------------------
// Insert helper - CheckResizeRequired
//-----------------------------------------------------------------------------
/**
 * @brief Check if the table needs to be resized.
 *
 * @return true  - If the table needs to be resized
 * @return false - If the table does not need to be resized
 */
template<typename T>
bool OAHashTable<T>::CheckResizeRequired() {
  // Check if max load factor == 1
  if (max_load_factor == 1) {
    // if so check if count == tableSize, and return the boolean result
    return (GetStats().Count_ == GetStats().TableSize_);
  }

  // Check if the load factor is greater than the max load factor, and return the boolean result
  return ((static_cast<double>(GetStats().Count_ + 1) / GetStats().TableSize_) > max_load_factor);
}

//-----------------------------------------------------------------------------
// Insert helper - resizeTable
//-----------------------------------------------------------------------------
/**
 * @brief Resize the table to the new size.
 *
 * @param newSize - New size of the table
 */
template<typename T>
void OAHashTable<T>::resizeTable(double newSize) {
  // Create a copy of the old table and set the HashTable pointer to the new table
  unsigned oldSize = tableSize;
  OAHTSlot* oldTable = table;
  tableSize = GetClosestPrime(static_cast<unsigned>(newSize));
  table = new OAHTSlot[tableSize]();
  ++expansions;
  count = 0;

  // Initialize the new table
  InitTable();
  
  // Rehash the old table into the new table
  for (unsigned i = 0; i < oldSize; i++) {
    OAHTSlot& slot = oldTable[i];
    if (slot.State == OAHTSlot::OCCUPIED) {
      insert(slot.Key, slot.data);
    }
  }

  delete[] oldTable; // Delete the old table
}

//-----------------------------------------------------------------------------
// Remove helper - packTable 
//-----------------------------------------------------------------------------
/**
 * @brief Compact the table cluster starting from the given index. 
 *
 * @param index - Index to start compacting the table from
 */
template<typename T>
void OAHashTable<T>::packTable(unsigned index) {
// If the deletion policy is not PACK, exit the function 
  if (deletion_policy != OAHTDeletionPolicy::PACK) {
    return; 
  }

  unsigned stride = (secondary_hash_func) ? secondary_hash_func(table[index].Key, tableSize - 1) + 1 : 1;
  unsigned currentIndex = index;
  unsigned stoppingIndex = 0; // Initialize stoppingIndex until an empty slot

  // First probe: Find the stopping index (first empty slot)
  while (true) {
    currentIndex = (currentIndex + stride) % tableSize;
    if (table[currentIndex].State == OAHTSlot::UNOCCUPIED) {
      stoppingIndex = (currentIndex - stride) % tableSize; // Store the stopping index
      break;
    }
    else if (currentIndex == index) {
      // Break the loop if we complete a full cycle without finding an empty slot
      stoppingIndex = currentIndex;
      break;
    }
  }

  // Second probe: Reinsert each index that we passed by during the first probe
  currentIndex = index; // Reset the currentIndex to the starting index
  while (currentIndex != stoppingIndex) {
    currentIndex = (currentIndex + stride) % tableSize; // Move to the next slot

    // Create a local copy of the key
    char localKey[MAX_KEYLEN - 1];
    strcpy(localKey, table[currentIndex].Key);

    // Create a local copy of the data
    T localData = table[currentIndex].data;

    // Mark the slot as unoccupied
    table[currentIndex].State = OAHTSlot::UNOCCUPIED;

    --count; // Decrement the count

    // Insert the local copy into the table
    insert(localKey, localData);
  }
}

//-----------------------------------------------------------------------------
// ResizeTable helper - InitTable 
//-----------------------------------------------------------------------------
/**
 * @brief Initialize/Reset the new table
 *
 */
template<typename T>
void OAHashTable<T>::InitTable() {
  for (unsigned i = 0; i < tableSize; i++) {
    table[i].State = OAHTSlot::UNOCCUPIED;
    table[i].data = nullptr;
    table[i].probes = 0;
  }
}