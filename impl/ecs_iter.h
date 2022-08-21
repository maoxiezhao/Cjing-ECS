#pragma once

#include "ecs_def.h"

namespace ECS
{
	void InitIterator(Iterator& it, U8 fields);
	void SetIteratorVar(Iterator& it, I32 varID, const TableRange& range);
	bool IsIteratorVarConstrained(Iterator& it, I32 varID);
	void ValidateInteratorCache(Iterator& it);
	void IteratorPopulateData(WorldImpl* world, Iterator& iter, EntityTable* table, I32 offset, size_t* sizes, void** ptrs);
	void FiniIterator(Iterator& it);
	bool NextIterator(Iterator* it);
	Iterator GetSplitWorkerInterator(Iterator& it, I32 index, I32 count);
}