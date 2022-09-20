#pragma once

#include "ecs_def.h"

namespace ECS
{
	EntityTable* GetTable(WorldImpl* world, EntityID entity);
	I32 GetTableCount(EntityTable* table);
	void FlushPendingTables(WorldImpl* world);
	EntityTable* FindOrCreateTableWithID(WorldImpl* world, EntityTable* parent, EntityID compID, TableGraphEdge* edge);
	EntityTable* FindOrCreateTableWithIDs(WorldImpl* world, const Vector<EntityID>& compIDs);
	EntityTable* FindOrCreateTableWithPrefab(EntityTable* table, EntityID prefab);
	EntityTable* FindOrCreateTableWithoutID(WorldImpl* world, EntityTable* parent, EntityID compID, TableGraphEdge* edge);
	TableComponentRecord* GetTableRecord(WorldImpl* world, EntityTable* table, EntityID compID);
	I32 TableSearchType(EntityTable* table, EntityID compID);
	I32 TableSearchType(EntityTable* table, ComponentRecord* compRecord);
	I32 TableSearchRelationLast(EntityTable* table, EntityID compID, EntityID relation, I32 minDepth, I32 maxDepth, I32* depthOut);

	// Search target component id from a table
	// Return column of component if compID exists, otherwise return -1
	I32 TableSearchType(EntityTable* table, EntityID compID);

	TableComponentRecord* GetTableRecordFromCache(EntityTableCacheBase* cache, const EntityTable* table);

	EntityTable* TableAppend(WorldImpl* world, EntityTable* table, EntityID compID, EntityTableDiff& diff);
	EntityTable* TableTraverseAdd(WorldImpl* world, EntityTable* table, EntityID compID, EntityTableDiff& diff);
	EntityTable* TableTraverseRemove(WorldImpl* world, EntityTable* table, EntityID compID, EntityTableDiff& diff);
	void CommitTables(WorldImpl* world, EntityID entity, EntityInfo* info, EntityTable* dstTable, EntityTableDiff& diff, bool construct);

	EntityTableCacheItem* GetTableCacheListIterNext(EntityTableCacheIterator& iter);
	EntityTableCacheIterator GetTableCacheListIter(EntityTableCacheBase* cache, bool emptyTable);

	void TableNotifyOnSet(WorldImpl* world, EntityTable* table, I32 row, I32 count, EntityID compID);
}