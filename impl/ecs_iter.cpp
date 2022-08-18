#include "ecs_impl_types.h"
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
		if (typeID == INVALID_ENTITY)
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

	void IteratorPopulateData(WorldImpl* world, Iterator& iter, EntityTable* table, I32 offset, size_t* sizes, void** ptrs)
	{
		iter.table = table;
		iter.count = 0;

		if (table != nullptr)
		{
			iter.count = GetTableCount(table);
			if (iter.count > 0)
				iter.entities = table->entities.data();
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
}