﻿#pragma once

#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <type_traits>
#include <stdexcept>
#include <format>

class HTException : public std::exception {
public:
	HTException(const std::string& message)
		: std::exception(message.c_str()) {
	}
};

class HTInvalidKeyType : public HTException {
public:
	HTInvalidKeyType()
		: HTException("Default hash function accepts only contiguously allocated keys.") {
	}
};

class HTRehashFailed : public HTException {
public:
	HTRehashFailed()
		: HTException("Failed to rehash hash table.") {
	}
};

class HTInsertionFailed : public HTException {
public:
	HTInsertionFailed()
		: HTException("Failed to insert an item in hash table.") {
	}
};

template<typename K, typename V>
class HashTable {
public:
	// Custom hash function interface
	using HashFunction = std::function<size_t(const K&)>;

	HashTable(HashFunction customHash = nullptr, size_t expectedSize = HashTable::INITIAL_CPTY,
		      double maxLoadFactor = HashTable::MAX_LOAD_FACTOR)
		: size(0), customHash(customHash), cpty(expectedSize), maxLoadFactor(maxLoadFactor) {
		if (expectedSize < HashTable::INITIAL_CPTY)
			cpty = HashTable::INITIAL_CPTY;

		if (!isValidMaxLoadFactor(maxLoadFactor)) {
			std::cout << std::format("Invalid maximum load factor: {}.\n", maxLoadFactor)
				      << "The maximum load factor will be settled to its default value.\n";
			this->maxLoadFactor = HashTable::MAX_LOAD_FACTOR;
		}

		this->minLoadFactor = this->maxLoadFactor / 2;
		
		table.resize(cpty);

		if (!isValidKeyForDefaultHash())
			throw HTInvalidKeyType();
	}

	// Copy constructor.
	HashTable(const HashTable<K, V>& obj)
		: size(obj.size), customHash(obj.customHash), cpty(obj.cpty), 
		  minLoadFactor(obj.minLoadFactor), maxLoadFactor(obj.maxLoadFactor), 
		  table(obj.table.begin(), obj.table.end()) {
	}

	// Get table's current size.
	size_t getSize() const {
		return size;
	}

	// Set a maximum limit for the hash table's load factor.
	void setMaxLoadFactor(double maxLoadFactor) {
		if (!isValidMaxLoadFactor(maxLoadFactor)) {
			std::cout << std::format("Invalid maximum load factor: {}.\n", maxLoadFactor)
				      << "No changes were made.\n";
			return;
		}
		
		this->maxLoadFactor = maxLoadFactor;
		this->minLoadFactor = maxLoadFactor / 2;

		try {
			if (maxLoadFactorExceeded()) {
				resize(2 * cpty);
			}
			else if (minLoadFactorExceeded() && cpty > INITIAL_CPTY) {
				resize((cpty + 1) / 2);
			}
		}
		catch (HTRehashFailed& e) {
			throw;
		}
	}

	// Set a custom hash function and rehash.
	void setCustomHash(HashFunction customHash) {
		HashFunction oldHashFunction = this->customHash;
		this->customHash = customHash;
		try {
			rehash(cpty);
		}
		catch (HTRehashFailed& e) {
			this->customHash = oldHashFunction;
			std::cout << e.what() << "\n";
			throw;
		}
	}

	// Check if the hash table contains an item with the given key.
	bool contains(const K& key) const {
		size_t h = hash(key, cpty), i = 0;

		do {
			size_t idx = (h + i) % cpty;
			if (table[idx] != nullptr && table[idx]->first == key)
				return true;
			i++;
		} while (i < HashTable::NBHD_SIZE);

		return false;
	}

	// Get the value associated with the given key.
	// Returns a nullopt if the item is not in the hash table.
	std::optional<V> getValue(const K& key) const {
		size_t h = hash(key, cpty), i = 0;

		do {
			size_t idx = (h + i) % cpty;
			if (table[idx] != nullptr && table[idx]->first == key)
				return std::make_optional<V>(table[idx]->second);
			i++;
		} while (i < HashTable::NBHD_SIZE);

		return std::nullopt;
	}

	// Get the item (key-value pair) associated with the given key.
	// Returns a nullopt if the item is not in the hash table.
	std::optional<std::pair<K, V>> getItem(const K& key) const {
		size_t h = hash(key, cpty), i = 0;

		do {
			size_t idx = (h + i) % cpty;
			if (table[idx] != nullptr && table[idx]->first == key)
				return std::make_optional<std::pair<K, V>>(*table[idx]);
			i++;
		} while (i < HashTable::NBHD_SIZE);

		return std::nullopt;
	}

	// Insert a new key-value pair into the hash table.
	// Returns true if the item was inserted and false otherwise.
	bool insert(const K& key, const V& val) {
		size_t h = hash(key, cpty), i = 0;

		// Check if there is any empty bucket within table[h]'s neighborhood
		do {
			size_t idx = (h + i) % cpty;

			if (table[idx] == nullptr) {
				// Insert new key-value pair in empty bucket
				table[idx] = std::make_shared<std::pair<K, V>>(key, val);
				size++;

				if (maxLoadFactorExceeded()) {
					try {
						resize(2 * cpty);
					}
					catch (HTRehashFailed& e) {
						throw;
					}
				}
				return true;
			}

			if (table[idx]->first == key) 
				// Don't insert duplicates
				return false;
	
			i++;
		} while (i < NBHD_SIZE);

		// There is no empty bucket within table[h]'s neighborhood
		// Try to free space by moving buckets in table[h]'s neighborhood
		// to another location
		size_t idx = (h + i) % cpty;
		while (idx != h) {
			if (table[idx] == nullptr) {
				size_t emptyBucketIdx = idx;

				i -= HashTable::NBHD_SIZE - 1;
				idx = (h + i) % cpty;

				table[emptyBucketIdx] = table[idx];
				table[idx].reset();

				if (i < HashTable::NBHD_SIZE) {
					table[idx] = std::make_shared<std::pair<K, V>>(key, val);
					size++;

					if (maxLoadFactorExceeded()) {
						try {
							resize(2 * cpty);
						}
						catch (HTRehashFailed& e) {
							throw;
						}
					}
					return true;
				}
				continue;
			}
			i++;
			idx = (h + i) % cpty;
		}

		// Failed to insert new key-value pair
		throw HTInsertionFailed();
	}

	// Remove the item with the given key from the hash table and returns its value.
	// Returns a nullopt if the item is not in the hash table.
	std::optional<V> remove(const K& key) {
		size_t h = hash(key, cpty);
		size_t i = 0;

		do {
			size_t idx = (h + i) % cpty;
			if (table[idx] != nullptr && table[idx]->first == key) {
				// Assign return value with bucket's value
				std::optional<V> r = table[idx]->second;

				table[idx].reset(); // Empty bucket
				size--;

				// Resize if needed
				if (minLoadFactorExceeded() && cpty > INITIAL_CPTY) 
					resize((cpty + 1) / 2);
				
				return r;
			}
			i++;
		} while (i < HashTable::NBHD_SIZE);

		return std::nullopt;
	}

	// Get a vector with all the values stored in the hash table.
	std::vector<V> getAll() const {
		std::vector<V> v;

		for (Bucket bucket : table) 
			if (bucket)
				v.emplace_back(bucket->second);
		
		return v;
	}

	bool isEmpty() const { 
		return size == 0; 
	}

	// Overload the [] operator for key-based lookup and insertion.
	template <typename T = V>
	typename std::enable_if<std::is_default_constructible<T>::value, T&>::type operator[](const K& key) {
		Bucket bucket = getBucket(key);

		if (bucket != nullptr)
			return bucket->second;

		insert(key, T()); // Default-construct a value of type T

		return getBucket(key)->second;
	}

	// Copy assignment operator for the hash table.
	void operator=(const HashTable& obj) {
		clear();

		cpty = obj.cpty;
		size = obj.size;
		maxLoadFactor = obj.maxLoadFactor;
		minLoadFactor = obj.minLoadFactor;
		customHash = obj.customHash;

		table.resize(cpty);

		for (size_t i = 0; i < cpty; i++)
			if (obj.table[i] != nullptr)
				table[i] = std::make_shared<std::pair<K, V>>(*obj.table[i]);
	}

private:
	static constexpr double MAX_LOAD_FACTOR = 0.8; // Default maximum load factor

	// Size of bucket neighborhoods
	// (referred to as "H" in the original paper on Hopscotch Hashing)
	static constexpr int NBHD_SIZE = 32; 

	static constexpr int INITIAL_CPTY = NBHD_SIZE; // Initial hash table capacity

	using Bucket = std::shared_ptr<std::pair<K, V>>;

	std::vector<Bucket> table; // The underlying table storing the key-value pairs
	size_t cpty; // Total number of buckets in the hash table
	size_t size; // Number of occupied buckets in the hash table
	double maxLoadFactor, minLoadFactor;
	HashFunction customHash;

	bool isValidMaxLoadFactor(double maxLoadFactor) const {
		return !(maxLoadFactor <= 0.0 || maxLoadFactor > 1.0);
	}

	// Check if the default hash function supports the class's key type.
	bool isValidKeyForDefaultHash() {
		return customHash != nullptr || std::is_fundamental_v<K> ||
			   std::is_pointer_v<K> || std::is_array_v<K>;
	}

	bool isPrime(uint64_t n) const {
		if (n <= 1)
			return false;

		for (uint64_t i = 2; i * i <= n; i++)
			if (n % i == 0)
				return false;

		return true;
	}

	size_t hash(const K& key, size_t range) const {
		// Use the custom hash function if provided; 
		// otherwise, use the default hash function
		auto hashFunction = customHash ? customHash : [this, range](const K& k) -> size_t {
			// Pointer that points to the memory representation of k
			const char* bytes = reinterpret_cast<const char*>(&k);

			if (!bytes) 
				throw HTInvalidKeyType();
			
			const size_t blockSize = sizeof(k);

			// Smallest prime number greater than the number of distinct values
			// that a byte can represent
			const size_t p = 257;

			size_t pPow = p;
			size_t hashCode = 0;

			// Treat k as a byte string and apply polynomial rolling hash
			for (size_t i = 0; i < blockSize; i++) {
				hashCode = (hashCode + (static_cast<size_t>(*(bytes + i)) + 1) * pPow) % range;
				pPow = (pPow * p) % range;
			}

			return hashCode;
		};

		// Calculate hash code using the selected hash function
		return hashFunction(key) % range;
	}

	// Resize hash table to the given capacity and rehash.
	void resize(size_t newCpty) {
		size_t oldCpty = cpty;
		cpty = newCpty;

		try {
			rehash(oldCpty);
		}
		catch (HTRehashFailed& e) {
			cpty = oldCpty;
			std::cout << e.what() << "\n";
			throw;
		}
	}

	// Reconfigure buckets indexes by applying a new hash function.
	void rehash(size_t range) {
		std::vector<Bucket> tempTable;
		tempTable.resize(cpty);

		// Transfer non-empty buckets from the current table to temp
		for (size_t i = 0; i < range; i++) {
			if (table[i] == nullptr)
				continue;

			size_t h = hash(table[i]->first, cpty);

			// Check if there is any empty bucket within tempTable[h]'s neighborhood
			size_t j = 0;
			do {
				size_t idx = (h + j) % cpty;
				if (tempTable[idx] == nullptr) {
					tempTable[idx] = table[i];
					break;
				}
				j++;
			} while (j < HashTable::NBHD_SIZE);

			if (j < HashTable::NBHD_SIZE)
				continue;

			// There is no empty bucket in tempTable[h]'s neighborhood
			// Try to free space by moving buckets in tempTable[h]'s neighborhood 
			// to another location
			size_t idx = (h + j) % cpty;
			while (idx != h) {
				if (tempTable[idx] == nullptr) {
					size_t emptyBucketIdx = idx;

					j -= HashTable::NBHD_SIZE - 1;
					idx = (h + j) % cpty;

					tempTable[emptyBucketIdx] = tempTable[idx];
					tempTable[idx].reset();

					if (j < HashTable::NBHD_SIZE) {
						tempTable[idx] = table[i];
						break;
					}
					continue;
				}
				j++;
				idx = (h + j) % cpty;
			}

			// Failed to rehash hash table
			if (idx == h)
				throw HTRehashFailed();
		}

		// Swap the contents of the current table with the temporary table
		table.swap(tempTable);
	}

	// Get bucket that points to the item associated with the given key.
	// Returns a nullptr if the item is not in the hash table.
	Bucket getBucket(const K& key) const {
		size_t h = hash(key, cpty);
		size_t i = 0;

		do {
			size_t idx = (h + i) % cpty;
			if (table[idx] != nullptr && table[idx]->first == key)
				return table[(h + i) % cpty];
			i++;
		} while (i < HashTable::NBHD_SIZE);

		return nullptr;
	}

	bool maxLoadFactorExceeded() const {
		return static_cast<double>(size) / cpty > maxLoadFactor;
	}

	bool minLoadFactorExceeded() const {
		return static_cast<double>(size) / cpty < minLoadFactor;
	}

	// Reset the hash table to its default initial state.
	void clear() {
		for (Bucket bucket : table)
			if (bucket != nullptr)
				bucket.reset();

		table.clear();

		size = 0;
		cpty = HashTable::INITIAL_CPTY;
	}
};