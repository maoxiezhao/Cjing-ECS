#include "ecs_priv_types.h"
#include "ecs_world.h"
#include "ecs_iter.h"
#include "ecs_table.h"

namespace ECS
{
	template<typename T>
	void InitIteratorCache(T*& ptr, T* smallPtr, I32 count, U8 fields, U8 mask, IteratorCache& cache)
	{
		if (ptr == nullptr && (fields & mask) && count > 0)
		{
			if (count <= ECS_TERM_CACHE_SIZE)
			{
				ptr = smallPtr;
				cache.used |= mask;
			}
			else
			{
				ptr = static_cast<T*>(ECS_MALLOC(sizeof(T) * count));
				cache.allocated |= mask;
			}
		}
	}

	template<typename T>
	void ValidateCache(T*& ptr, T* smallPtr, U8 mask, IteratorCache& cache)
	{
		if (ptr != nullptr)
		{
			if (cache.used & mask)
				ptr = smallPtr;
		}
	}

	void ValidateInteratorCache(Iterator& it)
	{
		IteratorCache& cache = it.priv.cache;
		ValidateCache(it.ids, cache.ids, ITERATOR_CACHE_MASK_IDS, cache);
		ValidateCache(it.columns, cache.columns, ITERATOR_CACHE_MASK_COLUMNS, cache);
		ValidateCache(it.sizes, cache.sizes, ITERATOR_CACHE_MASK_SIZES, cache);
		ValidateCache(it.ptrs, cache.ptrs, ITERATOR_CACHE_MASK_PTRS, cache);
	}

	void FiniIteratorCache(void* ptr, U8 mask, IteratorCache& cache)
	{
		if (ptr && (cache.allocated & mask))
			ECS_FREE(ptr);
	}

	void InitIterator(Iterator& it, U8 fields)
	{
		IteratorCache& cache = it.priv.cache;
		cache.used = 0;
		cache.allocated = 0;

		InitIteratorCache(it.ids, cache.ids, it.termCount, fields, ITERATOR_CACHE_MASK_IDS, cache);
		InitIteratorCache(it.columns, cache.columns, it.termCount, fields, ITERATOR_CACHE_MASK_COLUMNS, cache);
		InitIteratorCache(it.sizes, cache.sizes, it.termCount, fields, ITERATOR_CACHE_MASK_SIZES, cache);
		InitIteratorCache(it.ptrs, cache.ptrs, it.termCount, fields, ITERATOR_CACHE_MASK_PTRS, cache);
	}

	void FiniIterator(Iterator& it)
	{
		IteratorCache& cache = it.priv.cache;
		FiniIteratorCache(it.ids, ITERATOR_CACHE_MASK_IDS, cache);
		FiniIteratorCache(it.columns, ITERATOR_CACHE_MASK_COLUMNS, cache);
		FiniIteratorCache(it.sizes, ITERATOR_CACHE_MASK_SIZES, cache);
		FiniIteratorCache(it.ptrs, ITERATOR_CACHE_MASK_PTRS, cache);
	}

	bool NextIterator(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->next != nullptr);
		return it->next(it);
	}

	void SetIteratorVar(Iterator& it, I32 varID, const TableRange& range)
	{
		ECS_ASSERT(varID >= 0 && varID < ECS_TERM_CACHE_SIZE);
		ECS_ASSERT(varID < it.variableCount);
		ECS_ASSERT(range.table != nullptr);
		it.variables[varID].range = range;
		it.variableMask |= 1 << varID;
	}

	bool IsIteratorVarConstrained(Iterator& it, I32 varID)
	{
		return (it.variableMask & (1u << varID)) != 0;
	}

	// Get size for target comp id
	size_t IteratorGetSizeForID(WorldImpl* world, EntityID id)
	{
		EntityID typeID = GetRealTypeID(world, id);
		if (typeID == INVALID_ENTITYID)
			return 0;

		auto info = GetComponentTypeInfo(world, typeID);
		ECS_ASSERT(info != nullptr);
		return info->size;
	}

	bool IteratorPopulateTermData(WorldImpl* world, Iterator& iter, I32 termIndex, I32 column, void** ptrOut, size_t* sizeOut)
	{
		EntityTable* table;
		I32 storageColumn;
		ComponentTypeInfo* typeInfo;
		ComponentColumnData* columnData;

		if (iter.terms == nullptr)
			goto NoData;

		table = iter.table;
		if (table == nullptr || table->Count() <= 0)
			goto NoData;

		storageColumn = table->GetStorageIndexByType(column);
		if (storageColumn == -1)
			goto NoData;

		typeInfo = &table->compTypeInfos[storageColumn];
		columnData = &table->storageColumns[storageColumn];

		if (ptrOut)
			ptrOut[0] = columnData->Get(typeInfo->size, typeInfo->alignment, iter.offset);
		if (sizeOut)
			sizeOut[0] = typeInfo->size;
		return true;

	NoData:
		if (ptrOut) ptrOut[0] = NULL;
		if (sizeOut) sizeOut[0] = 0;
		return false;
	}

	void IteratorPopulateData(WorldImpl* world, Iterator& iter, EntityTable* table, I32 offset, I32 count, size_t* sizes, void** ptrs)
	{
		iter.table = table;
		iter.offset = offset;
		iter.count = count;

		if (table != nullptr)
		{
			if (iter.count == 0)
				iter.count = GetTableCount(table);

			if (iter.count > 0)
				iter.entities = table->entities.data() + offset;
			else
				iter.entities = nullptr;
		}

		// If is iterator filter, return metadata only
		if (ECS_BIT_IS_SET(iter.flags, IteratorFlagIsFilter))
		{
			if (sizes != nullptr)
			{
				for (int i = 0; i < iter.termCount; i++)
					sizes[i] = IteratorGetSizeForID(world, iter.ids[i]);
			}
			return;
		}

		// Populate term datas
		for (int i = 0; i < iter.termCount; i++)
		{
			ECS_ASSERT(iter.columns != nullptr);

			I32 column = iter.columns[i];
			void** ptr = nullptr;
			if (ptrs != nullptr)
				ptr = &ptrs[i];

			size_t* size = nullptr;
			if (sizes != nullptr)
				size = &sizes[i];

			IteratorPopulateTermData(world, iter, i, column, ptr, size);
		}
	}

	void OffsetIterator(Iterator* it, I32 offset)
	{
		it->entities = &it->entities[offset];
		for (int t = 0; t < it->termCount; t++)
		{
			void* ptr = it->ptrs[t];
			if (ptr == nullptr)
				continue;

			it->ptrs[t] = (U8*)ptr + offset * it->sizes[t];
		}
	}

	bool WorkerNextInstanced(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->chainIter != nullptr);

		Iterator* chainIter = it->chainIter;
		I32 numWorker = it->priv.iter.worker.count;
		I32 workerIndex = it->priv.iter.worker.index;
		I32 perWorker = 0, first = 0;

		// Traverse all chain iterator to get 
		do
		{
			// Do next of source chain iterator
			if (!chainIter->next(chainIter))
				return false;

			memcpy(it, chainIter, offsetof(Iterator, priv));

			I32 count = (I32)it->count;
			perWorker = count / numWorker;
			first = perWorker * workerIndex;

			// If there is still left, let the workers whose index is less than count execute one more entity
			count -= perWorker * numWorker;
			if (count > 0)
			{
				if (workerIndex < count)
				{
					perWorker++;
					first += workerIndex;
				}
				else
				{
					first += count;
				}
			}

			if (perWorker == 0 && it->table == nullptr)
				return workerIndex == 0;

		} while (perWorker == 0);

		// Offset iterator data for each worker
		OffsetIterator(it, it->offset + first);

		it->count = perWorker;
		it->offset += first;

		return true;
	}

	bool NextSplitWorkerIter(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->chainIter != nullptr);
		ECS_ASSERT(it->next == NextSplitWorkerIter);

		return WorkerNextInstanced(it);
	}

	Iterator GetSplitWorkerInterator(Iterator& it, I32 index, I32 count)
	{
		ECS_ASSERT(it.next != nullptr);
		ECS_ASSERT(index >= 0 && index < count);
		ECS_ASSERT(count > 0);

		Iterator iter = {};
		iter.world = it.world;
		iter.chainIter = &it;
		iter.next = NextSplitWorkerIter;
		iter.priv.iter.worker.index = index;
		iter.priv.iter.worker.count = count;
		return iter;
	}
}