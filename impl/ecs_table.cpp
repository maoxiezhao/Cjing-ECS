#include "ecs_priv_types.h"
#include "ecs_table.h"
#include "ecs_world.h"
#include "ecs_query.h"
#include "ecs_iter.h"
#include "ecs_observer.h"
#include "ecs_stage.h"
#include "ecs_entity.h"

namespace ECS
{
	I32 TableQuickSortPartition(EntityTable* table, I32 lo, I32 hi, QueryOrderByAction compare)
	{
		I32 mid = (lo + hi) / 2;
		auto& entities = table->entities;
		EntityID pivot = entities[mid];
		I32 i = lo - 1;
		I32 j = hi + 1;
		while (true)
		{
			do i++; while (compare(entities[i], nullptr, pivot, nullptr) < 0);
			do j--; while (compare(entities[j], nullptr, pivot, nullptr) > 0);
			if (i >= j)
				return j;

			table->SwapRows(i, j);

			if (mid == i)
			{
				mid = j;
				pivot = entities[j];
			}
			else if (mid == j)
			{
				mid = i;
				pivot = entities[i];
			}
		}
	}

	void TableQuickSort(EntityTable* table, I32 lo, I32 hi, QueryOrderByAction compare)
	{
		if (hi - lo < 1)
			return;

		I32 p = TableQuickSortPartition(table, lo, hi, compare);
		TableQuickSort(table, lo, p, compare);
		TableQuickSort(table, p + 1, hi, compare);
	}

	void OnComponentCallback(WorldImpl* world, EntityTable* table, ComponentTypeInfo* typeInfo, IterCallbackAction callback, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
	{
		Iterator it = {};
		it.termCount = 1;
		it.entities = entities;

		// term count < ECS_TERM_CACHE_SIZE
		InitIterator(it, ITERATOR_CACHE_MASK_ALL);
		it.world = world;
		it.table = table;
		it.ptrs[0] = columnData->Get(typeInfo->size, typeInfo->alignment, row);
		it.sizes[0] = typeInfo->size;
		it.ids[0] = compID;
		it.count = count;
		it.invoker = typeInfo->hooks.invoker;
		ValidateInteratorCache(it);
		callback(&it);
	}

	void CtorComponent(WorldImpl* world, ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
	{
		ECS_ASSERT(columnData != nullptr);

		if (typeInfo != nullptr && typeInfo->hooks.ctor != nullptr)
		{
			void* mem = columnData->Get(typeInfo->size, typeInfo->alignment, row);
			typeInfo->hooks.ctor(mem, count, typeInfo);
		}
	}

	void DtorComponent(WorldImpl* world, ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
	{
		ECS_ASSERT(columnData != nullptr);

		if (typeInfo != nullptr && typeInfo->hooks.dtor != nullptr)
		{
			void* mem = columnData->Get(typeInfo->size, typeInfo->alignment, row);
			typeInfo->hooks.dtor(mem, count, typeInfo);
		}
	}

	void AddNewComponent(WorldImpl* world, EntityTable* table, ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
	{
		ECS_ASSERT(typeInfo != nullptr);

		CtorComponent(world, typeInfo, columnData, entities, compID, row, count);

		auto onAdd = typeInfo->hooks.onAdd;
		if (onAdd != nullptr)
			OnComponentCallback(world, table, typeInfo, onAdd, columnData, entities, compID, row, count);
	}

	void RemoveComponent(WorldImpl* world, EntityTable* table, ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
	{
		ECS_ASSERT(typeInfo != nullptr);

		auto onRemove = typeInfo->hooks.onRemove;
		if (onRemove != nullptr)
			OnComponentCallback(world, table, typeInfo, onRemove, columnData, entities, compID, row, count);

		DtorComponent(world, typeInfo, columnData, entities, compID, row, count);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Table graph
	////////////////////////////////////////////////////////////////////////////////

	TableGraphEdge* RequestTableGraphEdge(WorldImpl* world)
	{
		TableGraphEdge* ret = world->freeEdge;
		if (ret != nullptr)
			world->freeEdge = (TableGraphEdge*)ret->next;
		else
			ret = ECS_MALLOC_T(TableGraphEdge);

		ECS_ASSERT(ret != nullptr);
		memset(ret, 0, sizeof(TableGraphEdge));
		return ret;
	}

	void FreeTableGraphEdge(WorldImpl* world, TableGraphEdge* edge)
	{
		if (world->isFini)
		{
			ECS_FREE(edge);
		}
		else
		{
			edge->next = (Util::ListNode<TableGraphEdge>*)world->freeEdge;
			world->freeEdge = edge;
		}
	}

	TableGraphEdge* EnsureHiTableGraphEdge(WorldImpl* world, TableGraphEdges& edges, EntityID compID)
	{
		auto it = edges.hiEdges.find(compID);
		if (it != edges.hiEdges.end())
			return it->second;

		TableGraphEdge* edge = nullptr;
		if (compID < HiComponentID)
			edge = &edges.loEdges[compID];
		else
			edge = RequestTableGraphEdge(world);

		edges.hiEdges[compID] = edge;
		return edge;
	}

	TableGraphEdge* EnsureTableGraphEdge(WorldImpl* world, TableGraphEdges& edges, EntityID compID)
	{
		TableGraphEdge* edge = nullptr;
		if (compID < HiComponentID)
		{
			edge = &edges.loEdges[compID];
		}
		else
		{
			auto it = edges.hiEdges.find(compID);
			if (it != edges.hiEdges.end())
				edge = it->second;
			else
				edge = EnsureHiTableGraphEdge(world, edges, compID);
		}
		return edge;
	}

	TableGraphEdge* FindTableGraphEdge(TableGraphEdges& edges, EntityID compID)
	{
		TableGraphEdge* edge = nullptr;
		if (compID < HiComponentID)
		{
			edge = &edges.loEdges[compID];
		}
		else
		{
			auto it = edges.hiEdges.find(compID);
			if (it != edges.hiEdges.end())
				edge = it->second;
		}
		return edge;
	}

	void DisconnectEdge(WorldImpl* world, TableGraphEdge* edge, EntityID compID)
	{
		ECS_ASSERT(edge != nullptr);
		ECS_ASSERT(edge->compID == compID);

		// Remove node from list of Edges
		Util::ListNode<TableGraphEdge>* prev = edge->prev;
		Util::ListNode<TableGraphEdge>* next = edge->next;
		if (next)
			next->prev = prev;
		if (prev)
			prev->next = next;

		// Free table diff
		EntityTableDiff* diff = edge->diff;
		if (diff != nullptr && diff != &EMPTY_TABLE_DIFF)
			ECS_DELETE_OBJECT(diff);

		// Component use small cache array when compID < HiComponentID
		if (compID >= HiComponentID)
			FreeTableGraphEdge(world, edge);
		else
			memset(edge, 0, sizeof(TableGraphEdge));
	}

	void ClearTableGraphEdges(WorldImpl* world, EntityTable* table)
	{
		TableGraphNode& graphNode = table->graphNode;

		// Remove outgoing edges
		for (auto& kvp : graphNode.add.hiEdges)
			DisconnectEdge(world, kvp.second, kvp.first);
		for (auto& kvp : graphNode.remove.hiEdges)
			DisconnectEdge(world, kvp.second, kvp.first);

		// Remove incoming edges
		// 1. Add edges are appended to incomingEdges->Next
		Util::ListNode<TableGraphEdge>* cur = graphNode.incomingEdges.next;
		Util::ListNode<TableGraphEdge>* next = nullptr;
		if (cur != nullptr)
		{
			do
			{
				TableGraphEdge* edget = cur->Cast();
				ECS_ASSERT(edget->to == table);
				ECS_ASSERT(edget->from != nullptr);
				next = cur->next;

				auto& edges = edget->from->graphNode.add.hiEdges;
				EntityID compId = edget->compID;
				DisconnectEdge(world, edget, compId);		
				edges.erase(compId);
			} 
			while ((cur = next));
		}
		// 2. Remove edges are appended to incomingEdges->prev
		cur = graphNode.incomingEdges.prev;
		if (cur != nullptr)
		{
			do
			{
				TableGraphEdge* edget = cur->Cast();
				ECS_ASSERT(edget->to == table);
				ECS_ASSERT(edget->from != nullptr);
				next = cur->prev;

				auto& edges = edget->from->graphNode.remove.hiEdges;
				EntityID compId = edget->compID;
				DisconnectEdge(world, edget, compId);
				edges.erase(compId);
			} 
			while ((cur = next));
		}

		graphNode.add.hiEdges.clear();
		graphNode.remove.hiEdges.clear();
	}

	void EntityTableCacheBase::InsertTableIntoCache(const EntityTable* table, EntityTableCacheItem* cacheNode)
	{
		ECS_ASSERT(table != nullptr);
		ECS_ASSERT(cacheNode != nullptr);

		bool empty = table->entities.empty();
		cacheNode->tableCache = this;
		cacheNode->table = (EntityTable*)(table);
		cacheNode->empty = empty;

		tableRecordMap[table->tableID] = cacheNode;
		ListInsertNode(cacheNode, empty);
	}

	EntityTableCacheItem* EntityTableCacheBase::RemoveTableFromCache(EntityTable* table)
	{
		auto it = tableRecordMap.find(table->tableID);
		if (it == tableRecordMap.end())
			return nullptr;

		EntityTableCacheItem* node = it->second;
		if (node == nullptr)
			return nullptr;

		ListRemoveNode(node, node->empty);
		tableRecordMap.erase(table->tableID);
		return node;
	}

	EntityTableCacheItem* EntityTableCacheBase::GetTableCache(EntityTable* table)
	{
		auto it = tableRecordMap.find(table->tableID);
		if (it == tableRecordMap.end())
			return nullptr;
		return it->second;
	}

	bool EntityTableCacheBase::SetTableCacheState(EntityTable* table, bool isEmpty)
	{
		auto it = tableRecordMap.find(table->tableID);
		if (it == tableRecordMap.end())
			return false;

		EntityTableCacheItem* node = it->second;
		if (node == nullptr)
			return false;

		if (node->empty == isEmpty)
			return false;

		node->empty = isEmpty;

		if (isEmpty)
		{
			ListRemoveNode(node, false);
			ListInsertNode(node, true);
		}
		else
		{
			ListRemoveNode(node, true);
			ListInsertNode(node, false);
		}
		return true;
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Table cache
	////////////////////////////////////////////////////////////////////////////////

	EntityTableCacheItem* GetTableCacheListIterNext(EntityTableCacheIterator& iter)
	{
		Util::ListNode<EntityTableCacheItem>* next = iter.next;
		if (!next)
			return nullptr;

		iter.cur = next;
		iter.next = next->next;
		return static_cast<EntityTableCacheItem*>(next);
	}

	EntityTableCacheIterator GetTableCacheListIter(EntityTableCacheBase* cache, bool emptyTable)
	{
		EntityTableCacheIterator iter = {};
		iter.cur = nullptr;
		iter.next = emptyTable ? cache->emptyTables.first : cache->tables.first;
		return iter;
	}

	TableComponentRecord* GetTableRecordFromCache(EntityTableCacheBase* cache, const EntityTable* table)
	{
		auto it = cache->tableRecordMap.find(table->tableID);
		if (it == cache->tableRecordMap.end())
			return nullptr;

		return reinterpret_cast<TableComponentRecord*>(it->second);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Table
	////////////////////////////////////////////////////////////////////////////////

	EntityTable* CreateNewTable(WorldImpl* world, EntityType entityType)
	{
		EntityTable* ret = world->tablePool.Requset();
		ECS_ASSERT(ret != nullptr);
		ret->tableID = world->tablePool.GetLastID();
		ret->type = entityType;
		if (!ret->InitTable(world))
		{
			ECS_ASSERT(0);
			return nullptr;
		}

		world->tableTypeHashMap[EntityTypeHash(entityType)] = ret;

		// Queries need to rematch tables
		QueryEvent ent = {};
		ent.type = QueryEventType::MatchTable;
		ent.table = ret;
		NotifyQueriss(world, ent);

		return ret;
	}

	EntityTable* GetTable(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(entity != INVALID_ENTITYID);

		world = GetWorld(world);
		
		EntityInfo* info = world->entityPool.Get(entity);
		if (info == nullptr)
			return nullptr;

		return info->table;
	}

	I32 GetTableCount(EntityTable* table)
	{
		ECS_ASSERT(table != nullptr);
		return (I32)table->entities.size();
	}

	TableComponentRecord* GetTableRecord(WorldImpl* world, EntityTable* table, EntityID compID)
	{
		ComponentRecord* compRecord = GetComponentRecord(world, compID);
		if (compRecord == nullptr)
			return nullptr;

		return GetTableRecordFromCache(&compRecord->cache, table);
	}

	// Search target component id from a table
	// Return column of component if compID exists, otherwise return -1
	I32 TableSearchType(EntityTable* table, EntityID compID)
	{
		if (table == nullptr)
			return -1;

		TableComponentRecord* record = GetTableRecord(table->world, table, compID);
		if (record == nullptr)
			return -1;

		return record->data.column;
	}

	I32 TableSearchType(EntityTable* table, ComponentRecord* compRecord)
	{
		if (table == nullptr || compRecord == nullptr)
			return -1;

		TableComponentRecord* record = GetTableRecordFromCache(&compRecord->cache, table);
		if (record == nullptr)
			return -1;

		return record->data.column;
	}

	I32 TypeSearchRelation(EntityTable* table, EntityID compID, EntityID relation, ComponentRecord* compRecord, I32 minDepth, I32 maxDepth, EntityID* objOut, I32* depthOut)
	{
		WorldImpl* world = table->world;
		ECS_ASSERT(world != nullptr);

		if (minDepth <= 0)
		{
			I32 ret = TableSearchType(table, compRecord);
			if (ret != -1)
				return ret;
		}

		// Check table flags
		if (!(table->flags & TableFlagHasRelation) || relation == INVALID_ENTITYID)
			return -1;

		// Relation must is a pair (relation, None)
		ComponentRecord* relationRecord = GetComponentRecord(world, relation);
		if (relationRecord == nullptr)
			return -1;

		I32 column = TableSearchType(table, relationRecord);
		if (column != -1)
		{
			EntityID obj = ECS_GET_PAIR_SECOND(table->type[column]);
			ECS_ASSERT(obj != INVALID_ENTITYID);
			EntityInfo* objInfo = world->entityPool.Get(obj);
			ECS_ASSERT(objInfo != nullptr);

			I32 objColumn = TypeSearchRelation(objInfo->table, compID, relation, compRecord, minDepth - 1, maxDepth - 1, objOut, depthOut);
			if (objColumn != -1)
			{
				if (objOut != nullptr)
					*objOut = GetAliveEntity(world, obj);
				if (depthOut != nullptr)
					(*depthOut)++;

				return objColumn;
			}

			// Obj dont have same component
			// TODO
		}

		return -1;
	}

	// Search target component id with relation from a table
	I32 TableSearchRelation(EntityTable* table, EntityID compID, EntityID relation, I32 minDepth, I32 maxDepth, EntityID* objOut, I32* depthOut)
	{
		if (table == nullptr)
			return -1;

		WorldImpl* world = table->world;
		ECS_ASSERT(world != nullptr);

		ComponentRecord* record = GetComponentRecord(world, compID);
		if (record == nullptr)
			return -1;

		maxDepth = (maxDepth == 0) ? INT32_MAX : maxDepth;

		return TypeSearchRelation(
			table, compID, ECS_MAKE_PAIR(relation, EcsPropertyNone),
			record, minDepth, maxDepth, objOut, depthOut);
	}

	// Search target component with relation from a table
	I32 TableSearchRelationLast(EntityTable* table, EntityID compID, EntityID relation, I32 minDepth, I32 maxDepth, I32* depthOut)
	{
		if (table == nullptr)
			return -1;

		WorldImpl* world = table->world;
		ECS_ASSERT(world != nullptr);

		// Find the column of target compID
		I32 depth = 0;
		EntityID obj = INVALID_ENTITYID;
		I32 column = TableSearchRelation(table, compID, relation, 0, 0, &obj, &depth);
		if (column == -1)
			return -1;

		// Get obj of relation
		if (obj == INVALID_ENTITYID)
		{
			if (TableSearchRelation(table, compID, relation, 1, 0, &obj, &depth) == -1)
				return column;
		}

		while (true)
		{
			EntityTable* curTable = GetTable(world, obj);
			ECS_ASSERT(curTable != nullptr);
			I32 curDepth = 0;
			EntityID curObj = INVALID_ENTITYID;
			if (TableSearchRelation(curTable, compID, relation, 1, 0, &curObj, &curDepth) == -1)
				break;

			depth += curDepth;
			obj = curObj;
		}

		if (depthOut != nullptr)
			*depthOut = depth;

		return column;
	}

	void AppendTableDiff(EntityTableDiff& dst, EntityTableDiff& src)
	{
		dst.added.insert(dst.added.end(), src.added.begin(), src.added.end());
		dst.removed.insert(dst.removed.end(), src.removed.begin(), src.removed.end());
	}

	void PopulateTableDiff(TableGraphEdge* edge, EntityID addID, EntityID removeID, EntityTableDiff& outDiff)
	{
		ECS_ASSERT(edge != nullptr);
		EntityTableDiff* diff = edge->diff;
		if (diff && diff != (&EMPTY_TABLE_DIFF))
		{
			outDiff = *diff;
		}
		else
		{
			if (addID != INVALID_ENTITYID)
				outDiff.added.push_back(addID);

			if (removeID != INVALID_ENTITYID)
				outDiff.removed.push_back(removeID);
		}
	}

	EntityTable* TableTraverseAdd(WorldImpl* world, EntityTable* table, EntityID compID, EntityTableDiff& diff)
	{
		EntityTable* node = table != nullptr ? table : &world->root;
		TableGraphEdge* edge = EnsureTableGraphEdge(world, node->graphNode.add, compID);
		EntityTable* ret = edge->to;
		if (ret == nullptr)
		{
			ret = FindOrCreateTableWithID(world, node, compID, edge);
			ECS_ASSERT(ret != nullptr);
		}

		PopulateTableDiff(edge, compID, INVALID_ENTITYID, diff);
		return ret;
	}

	EntityTable* TableTraverseRemove(WorldImpl* world, EntityTable* table, EntityID compID, EntityTableDiff& diff)
	{
		EntityTable* node = table != nullptr ? table : &world->root;
		TableGraphEdge* edge = EnsureTableGraphEdge(world, node->graphNode.remove, compID);
		EntityTable* ret = edge->to;
		if (ret == nullptr)
		{
			ret = FindOrCreateTableWithoutID(world, node, compID, edge);
			ECS_ASSERT(ret != nullptr);
		}

		PopulateTableDiff(edge, compID, INVALID_ENTITYID, diff);
		return ret;
	}

	void ComputeTableDiff(EntityTable* t1, EntityTable* t2, TableGraphEdge* edge, EntityID compID)
	{
		if (t1 == t2)
			return;

		// Calculate addedCount and removedCount
		U32 addedCount = 0;
		U32 removedCount = 0;
		bool trivialEdge = true;
		U32 srcNumColumns = (U32)t1->storageCount;
		U32 dstNumColumns = (U32)t2->storageCount;
		U32 srcColumnIndex, dstColumnIndex;
		for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
		{
			EntityID srcComponentID = t1->storageIDs[srcColumnIndex];
			EntityID dstComponentID = t2->storageIDs[dstColumnIndex];
			if (srcComponentID < dstComponentID)
			{
				removedCount++;
				trivialEdge = false;
			}
			else if (srcComponentID > dstComponentID)
			{
				addedCount++;
				trivialEdge = false;
			}

			srcColumnIndex += srcComponentID <= dstComponentID;
			dstColumnIndex += dstComponentID <= srcComponentID;
		}

		addedCount += dstNumColumns - dstColumnIndex;
		removedCount += srcNumColumns - srcColumnIndex;

		trivialEdge &= (addedCount + removedCount) <= 1 &&
			(!ECS_HAS_RELATION(compID, EcsRelationIsA)
				&& !(t1->flags & TableFlagHasIsA)
				&& !(t2->flags & TableFlagHasIsA)) &&
			CheckIDHasPropertyNone(compID);

		if (trivialEdge)
		{
			if (t1->storageTable != t2->storageTable)
				edge->diff = &EMPTY_TABLE_DIFF;
			return;
		}

		// Create a new TableDiff
		EntityTableDiff* diff = ECS_NEW_OBJECT<EntityTableDiff>();
		edge->diff = diff;
		if (addedCount > 0)
			diff->added.reserve(addedCount);
		if (removedCount > 0)
			diff->removed.reserve(removedCount);

		for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
		{
			EntityID srcComponentID = t1->storageIDs[srcColumnIndex];
			EntityID dstComponentID = t2->storageIDs[dstColumnIndex];
			if (srcComponentID < dstComponentID)
				diff->removed.push_back(srcComponentID);
			else if (srcComponentID > dstComponentID)
				diff->added.push_back(dstComponentID);

			srcColumnIndex += srcComponentID <= dstComponentID;
			dstColumnIndex += dstComponentID <= srcComponentID;
		}

		for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
			diff->removed.push_back(t1->storageIDs[srcColumnIndex]);
		for (; dstColumnIndex < dstNumColumns; dstColumnIndex++)
			diff->added.push_back(t2->storageIDs[dstColumnIndex]);

		ECS_ASSERT(diff->added.size() == addedCount);
		ECS_ASSERT(diff->removed.size() == removedCount);
	}

	void InitAddTableGraphEdge(WorldImpl* world, TableGraphEdge* edge, EntityID compID, EntityTable* from, EntityTable* to)
	{
		ECS_ASSERT(edge->prev == nullptr);
		ECS_ASSERT(edge->next == nullptr);

		edge->from = from;
		edge->to = to;
		edge->compID = compID;

		EnsureHiTableGraphEdge(world, from->graphNode.add, compID);

		if (from != to)
		{
			Util::ListNode<TableGraphEdge>* toNode = &to->graphNode.incomingEdges;
			Util::ListNode<TableGraphEdge>* next = toNode->next;
			toNode->next = edge;

			edge->prev = toNode;
			edge->next = next;

			if (next != nullptr)
				next->prev = edge;

			// Compute table diff (Call PopulateTableDiff to get all diffs)
			ComputeTableDiff(from, to, edge, compID);
		}
	}

	void InitRemoveTableGraphEdge(WorldImpl* world, TableGraphEdge* edge, EntityID compID, EntityTable* from, EntityTable* to)
	{
		ECS_ASSERT(edge->prev == nullptr);
		ECS_ASSERT(edge->next == nullptr);

		edge->from = from;
		edge->to = to;
		edge->compID = compID;

		EnsureHiTableGraphEdge(world, from->graphNode.remove, compID);

		if (from != to)
		{
			// Remove edges are appended to incomingEdges->prev
			Util::ListNode<TableGraphEdge>* toNode = &to->graphNode.incomingEdges;
			Util::ListNode<TableGraphEdge>* prev = toNode->prev;
			toNode->prev = edge;

			edge->next = toNode;
			edge->prev = prev;

			if (prev != nullptr)
				prev->next = edge;

			// Compute table diff (Call PopulateTableDiff to get all diffs)
			ComputeTableDiff(from, to, edge, compID);
		}
	}

	EntityTable* FindOrCreateTableWithIDs(WorldImpl* world, const Vector<EntityID>& compIDs)
	{
		auto it = world->tableTypeHashMap.find(EntityTypeHash(compIDs));
		if (it != world->tableTypeHashMap.end())
			return it->second;

		return CreateNewTable(world, compIDs);
	}

	EntityTable* FindOrCreateTableWithPrefab(EntityTable* table, EntityID prefab)
	{
		if (table->flags & TableFlagIsPrefab)
			return table;

		WorldImpl* world = table->world;
		ECS_ASSERT(world != nullptr);

		EntityTable* prefabTable = GetTable(world, prefab);
		if (prefabTable == nullptr)
			return table;

		I32 prefabTypeCount = (I32)(prefabTable->type.size() - 1);
		for (int i = prefabTypeCount; i >= 0; i--)
		{
			EntityID compID = prefabTable->type[i];
			if (ECS_HAS_ROLE(compID, EcsRoleShared))
			{
				// TODO: support SharedComponents
				ECS_ASSERT(0);
				continue;
			}

			if (compID == EcsTagPrefab)
				continue;

			if (ECS_HAS_ROLE(compID, EcsRolePair) && (ECS_GET_PAIR_FIRST(compID) == EcsRelationIsA))
			{
				EntityID baseOfPrefab = ECS_GET_PAIR_SECOND(compID);
				table = FindOrCreateTableWithPrefab(table, baseOfPrefab);
			}

			// In default case, we just add datas of prefab to current table
			EntityTableDiff diff = {};
			table = TableTraverseAdd(world, table, compID & ECS_COMPONENT_MASK, diff);
		}

		return table;
	}

	EntityTable* FindOrCreateTableWithID(WorldImpl* world, EntityTable* parent, EntityID compID, TableGraphEdge* edge)
	{
		EntityType entityType = parent->type;
		if (!MergeEntityType(entityType, compID))
			return parent;

		if (entityType.empty())
			return &world->root;

		// Find exsiting tables
		auto it = world->tableTypeHashMap.find(EntityTypeHash(entityType));
		if (it != world->tableTypeHashMap.end())
			return it->second;

		EntityTable* newTable = CreateNewTable(world, entityType);
		ECS_ASSERT(newTable);

		// Create from prefab if comp has relation of isA
		if (ECS_HAS_ROLE(compID, EcsRolePair) && ECS_GET_PAIR_FIRST(compID) == EcsRelationIsA)
		{
			EntityID prefab = ECS_GET_PAIR_SECOND(compID);
			newTable = FindOrCreateTableWithPrefab(newTable, prefab);
		}

		// Connect parent with new table 
		InitAddTableGraphEdge(world, edge, compID, parent, newTable);

		return newTable;
	}

	EntityTable* TableAppend(WorldImpl* world, EntityTable* table, EntityID compID, EntityTableDiff& diff)
	{
		EntityTableDiff tempDiff = {};
		EntityTable* ret = TableTraverseAdd(world, table, compID, diff);
		ECS_ASSERT(ret != nullptr);
		AppendTableDiff(diff, tempDiff);
		return ret;
	}

	EntityTable* FindOrCreateTableWithoutID(WorldImpl* world, EntityTable* parent, EntityID compID, TableGraphEdge* edge)
	{
		EntityType entityType = parent->type;
		RemoveFromEntityType(entityType, compID);
		EntityTable* ret = FindOrCreateTableWithIDs(world, entityType);
		InitRemoveTableGraphEdge(world, edge, compID, parent, ret);
		return ret;
	}

	void MoveTableEntityImpl(WorldImpl* world, EntityID srcEntity, EntityTable* srcTable, I32 srcRow, EntityID dstEntity, EntityTable* dstTable, I32 dstRow, bool construct)
	{
		// Move entites from srcTable to dstTable. Always keep the order of ComponentIDs()
		bool sameEntity = srcEntity == dstEntity;
		U32 srcNumColumns = (U32)srcTable->storageCount;
		U32 dstNumColumns = (U32)dstTable->storageCount;
		U32 srcColumnIndex, dstColumnIndex;
		for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
		{
			EntityID srcComponentID = srcTable->storageIDs[srcColumnIndex];
			EntityID dstComponentID = dstTable->storageIDs[dstColumnIndex];
			if (srcComponentID == dstComponentID)
			{
				ComponentColumnData* srcColumnData = &srcTable->storageColumns[srcColumnIndex];
				ComponentColumnData* dstColumnData = &dstTable->storageColumns[dstColumnIndex];
				ComponentTypeInfo& typeInfo = srcTable->compTypeInfos[srcColumnIndex];

				void* srcMem = srcColumnData->Get(typeInfo.size, typeInfo.alignment, srcRow);
				void* dstMem = dstColumnData->Get(typeInfo.size, typeInfo.alignment, dstRow);
				ECS_ASSERT(srcMem != nullptr);
				ECS_ASSERT(dstMem != nullptr);

				if (sameEntity)
				{
					// Do move-ctor-dtor if same entity
					auto moveCtor = typeInfo.hooks.moveCtor;
					auto dtor = typeInfo.hooks.dtor;
					if (moveCtor != nullptr && dtor != nullptr)
					{
						moveCtor(srcMem, dstMem, 1, &typeInfo);
						dtor(srcMem, 1, &typeInfo);
					}
					else
					{
						memcpy(dstMem, srcMem, typeInfo.size);
					}
				}
				else
				{
					// Do copy-ctor
					if (typeInfo.hooks.copyCtor != nullptr)
						typeInfo.hooks.copyCtor(srcMem, dstMem, 1, &typeInfo);
					else
						memcpy(dstMem, srcMem, typeInfo.size);
				}
			}
			else
			{
				if (dstComponentID < srcComponentID)
				{
					if (construct)
					{
						AddNewComponent(
							world,
							dstTable,
							&dstTable->compTypeInfos[dstColumnIndex],
							&dstTable->storageColumns[dstColumnIndex],
							&dstEntity,
							dstComponentID,
							dstRow,
							1);
					}
				}
				else
				{
					RemoveComponent(
						world,
						srcTable,
						&srcTable->compTypeInfos[srcColumnIndex],
						&srcTable->storageColumns[srcColumnIndex],
						&srcEntity,
						srcComponentID,
						srcRow,
						1);
				}
			}

			srcColumnIndex += (dstComponentID >= srcComponentID);
			dstColumnIndex += (dstComponentID <= srcComponentID);
		}

		// Construct remainning columns
		if (construct)
		{
			for (; dstColumnIndex < dstNumColumns; dstColumnIndex++)
				AddNewComponent(
					world,
					dstTable,
					&dstTable->compTypeInfos[dstColumnIndex],
					&dstTable->storageColumns[dstColumnIndex],
					&dstEntity,
					dstTable->storageIDs[dstColumnIndex],
					dstRow,
					1);
		}

		// Destruct remainning columns
		for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
			RemoveComponent(
				world,
				srcTable,
				&srcTable->compTypeInfos[srcColumnIndex],
				&srcTable->storageColumns[srcColumnIndex],
				&srcEntity,
				srcTable->storageIDs[srcColumnIndex],
				srcRow,
				1);
	}


	I32 MoveTableEntity(WorldImpl* world, EntityID entity, EntityInfo* entityInfo, EntityTable* srcTable, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
	{
		ECS_ASSERT(entityInfo != nullptr);
		ECS_ASSERT(entityInfo == world->entityPool.Get(entity));
		ECS_ASSERT(IsEntityAlive(world, entity));

		U32 srcRow = entityInfo->row;
		ECS_ASSERT(srcRow >= 0);

		// Add a new entity for dstTable (Just reserve storage)
		U32 newRow = dstTable->AppendNewEntity(entity, entityInfo, false);
		ECS_ASSERT(srcTable->entities.size() > entityInfo->row);

		// Move comp datas from src table to new table of entity
		if (!srcTable->type.empty())
			MoveTableEntityImpl(world, entity, srcTable, entityInfo->row, entity, dstTable, newRow, construct);

		entityInfo->row = newRow;
		entityInfo->table = dstTable;

		// Remove old entity from src table
		srcTable->DeleteEntity(srcRow, false);

		return newRow;
	}


	EntityInfo* TableNewEntityImpl(WorldImpl* world, EntityID entity, EntityInfo* entityInfo, EntityTable* table, bool construct)
	{
		if (entityInfo == nullptr)
			entityInfo = world->entityPool.Ensure(entity);

		U32 newRow = table->AppendNewEntity(entity, entityInfo, construct);
		entityInfo->row = newRow;
		entityInfo->table = table;
		return entityInfo;
	}

	void CommitTables(WorldImpl* world, EntityID entity, EntityInfo* info, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
	{
		EntityTable* srcTable = info != nullptr ? info->table : nullptr;
		ECS_ASSERT(dstTable != nullptr);
		if (srcTable != nullptr)
		{
			if (!dstTable->type.empty()) {
				MoveTableEntity(world, entity, info, srcTable, dstTable, diff, construct);
			}
			else {
				srcTable->DeleteEntity(info->row, true);
				info->table = nullptr;
			}
		}
		else
		{
			if (!dstTable->type.empty())
				info = TableNewEntityImpl(world, entity, info, dstTable, construct);
		}
	}

	void FlushPendingTables(WorldImpl* world)
	{
		ECS_ASSERT(ECS_CHECK_OBJECT(&world->base, WorldImpl));

		if (world->isReadonly)
		{
			ECS_ASSERT(world->pendingTables->Count() == 0);
			return;
		}

		// Pending table is iterating when pending buffer is null.
		if (world->pendingBuffer == nullptr)
			return;

		size_t pendingCount = world->pendingTables->Count();
		if (pendingCount == 0)
			return;

		// Check whether the status (Emtpy/NonEmtpy) of the table cache has changed
		auto NeedUpdateTable = [&](EntityTable* table)->bool {
			bool ret = false;
			bool isEmpty = table->Count() == 0;
			for (int i = 0; i < table->tableRecords.size(); i++)
			{
				TableComponentRecord& record = table->tableRecords[i];
				ret |= record.tableCache->SetTableCacheState(table, isEmpty);
			}
			return ret;
		};
		do
		{
			Util::SparseArray<EntityTable*>* tables = world->pendingTables;
			world->pendingTables = world->pendingBuffer;
			world->pendingBuffer = nullptr;

			// Defer all operations, when we emit event to change states of table
			BeginDefer(world);

			for (size_t i = 0; i < pendingCount; i++)
			{
				EntityTable* table = *tables->GetByDense(i);
				if (table == nullptr || table->tableID == 0)
					continue;

				if (NeedUpdateTable(table))
				{
					EventDesc desc = {};
					desc.event = table->Count() > 0 ? EcsEventTableFill : EcsEventTableEmpty;
					desc.ids = table->type;
					desc.observable = &world->observable;
					desc.table = table;
					EmitEvent(world, desc);
				}
			}

			tables->Clear();

			EndDefer(world);

			world->pendingBuffer = tables;

		} while (pendingCount = world->pendingTables->Count());
	}

	////////////////////////////////////////////////////////////////////////////////
	//// EntityTableImpl
	////////////////////////////////////////////////////////////////////////////////

	bool EntityTable::InitTable(WorldImpl* world_)
	{
		ECS_ASSERT(world_ != nullptr);
		world = world_;
		refCount = 1;

		// Ensure all ids used exist */
		for (auto& id : type)
			EnsureEntity(world_, id);

		// Init table flags
		InitTableFlags();

		//  Register table records
		RegisterTableComponentRecords();

		// Init storage table
		InitStorageTable();

		// Init type infos
		InitTypeInfos();

		// Set dirty info
		tableDirty = 1;
		columnDirty.resize(storageCount);
		for (int i = 0; i < columnDirty.size(); i++)
			columnDirty[i] = 1;

		return true;
	}

	void EntityTable::Claim()
	{
		ECS_ASSERT(refCount > 0);
		refCount++;
	}

	bool EntityTable::Release()
	{
		ECS_ASSERT(refCount > 0);
		if (--refCount == 0)
		{
			Free();
			return true;
		}
		return false;
	}

	void EntityTable::Free()
	{
		bool isRoot = this == &world->root;
		ECS_ASSERT(isRoot || this->tableID != 0);
		ECS_ASSERT(refCount == 0);

		// Queries need to rematch tables
		if (!isRoot && !world->isFini)
		{
			QueryEvent ent = {};
			ent.type = QueryEventType::UnmatchTable;
			ent.table = this;
			NotifyQueriss(world, ent);
		}

		// Fini data
		FiniData(true, true);

		// Clear all graph edges
		ClearTableGraphEdges(world, this);

		if (!isRoot)
		{
			// Only non-root table insert into HashMap 
			world->tableTypeHashMap.erase(EntityTypeHash(type));
		}

		//  Unregister table
		UnregisterTableRecords();

		// Decrease the ref coung of the storage table
		if (storageTable != nullptr && storageTable != this)
			storageTable->Release();

		// Free component type infos
		if (storageTable == this)
		{
			if (compTypeInfos != nullptr)
				ECS_FREE(compTypeInfos);
		}

		if (!world->isFini)
			world->tablePool.Remove(tableID);
	}

	void EntityTable::FiniData(bool updateEntity, bool deleted)
	{
		// Dtor all components
		size_t count = entities.size();
		if (count > 0)
		{
			if (ECS_HAS_FLAG(flags, TableFlagHasDtors))
			{
				for (size_t row = 0; row < count; row++)
				{
					// Dtor components
					for (size_t col = 0; col < storageCount; col++)
					{
						DtorComponent(
							world,
							&compTypeInfos[col],
							&storageColumns[col],
							entities.data(),
							storageIDs[col],
							(I32)row,
							1);
					}

					// Remove entity
					if (updateEntity)
					{
						EntityID entity = entities[row];
						ECS_ASSERT(entity != INVALID_ENTITYID);
						if (deleted)
						{
							world->entityPool.Remove(entity);
						}
						else
						{
							entityInfos[row]->table = nullptr;
							entityInfos[row]->row = 0;
						}
					}
				}
			}
			else if (updateEntity)
			{
				for (size_t row = 0; row < count; row++)
				{
					EntityID entity = entities[row];
					ECS_ASSERT(entity != INVALID_ENTITYID);
					if (deleted)
					{
						world->entityPool.Remove(entity);
					}
					else
					{
						entityInfos[row]->table = nullptr;
						entityInfos[row]->row = 0;
					}
				}
			}
		}

		ECS_ASSERT(entityInfos.size() == entities.size());

		// Clear all storage column datas
		for (int i = 0; i < storageColumns.size(); i++)
		{
			ComponentColumnData& columnData = storageColumns[i];
			ECS_ASSERT(columnData.GetCount() == count);
			columnData.Clear();
		}
		storageColumns.clear();

		entities.clear();
		entityInfos.clear();
	}

	void EntityTable::DeleteEntity(U32 index, bool destruct)
	{
		U32 count = (U32)entities.size() - 1;
		ECS_ASSERT(count >= 0);

		// Remove target entity
		EntityID entityToMove = entities[count];
		EntityID entityToDelete = entities[index];
		entities[index] = entityToMove;
		entities.pop_back();

		// Remove target entity info ptr
		EntityInfo* entityInfoToMove = entityInfos[count];
		entityInfos[index] = entityInfoToMove;
		entityInfos.pop_back();

		// Retarget row of entity info
		if (index != count && entityInfoToMove != nullptr)
			entityInfoToMove->row = index;

		// Set table dirty
		SetTableDirty();

		// Pending empty table
		if (count == 0)
			SetEmpty();

		if (index == count)
		{
			// Destruct the last data of column
			if (destruct && ECS_HAS_FLAG(flags, TableFlagHasDtors))
			{
				for (int i = 0; i < storageCount; i++)
				{
					RemoveComponent(
						world,
						this,
						&compTypeInfos[i],
						&storageColumns[i],
						&entityToDelete,
						storageIDs[i],
						index,
						1);
				}
			}
			RemoveColumnLast();
		}
		else
		{
			// Swap target element and last element, then remove last element
			if (destruct && ECS_HAS_FLAG(flags, TableFlagHasDtors | TableFlagHasMove))
			{
				for (int i = 0; i < storageCount; i++)
				{
					ComponentTypeInfo& typeInfo = compTypeInfos[i];
					auto& columnData = storageColumns[i];
					void* srcMem = columnData.Get(typeInfo.size, typeInfo.alignment, count);
					void* dstMem = columnData.Get(typeInfo.size, typeInfo.alignment, index);

					auto onRemove = typeInfo.hooks.onRemove;
					if (onRemove != nullptr)
						OnComponentCallback(world, this, &typeInfo, onRemove, &columnData, &entityToDelete, storageIDs[i], index, 1);

					if (typeInfo.hooks.moveDtor != nullptr)
						typeInfo.hooks.moveDtor(srcMem, dstMem, 1, &typeInfo);
					else
						memcpy(dstMem, srcMem, typeInfo.size);

					columnData.RemoveLast();
				}
			}
			else
			{
				RemoveColumns(storageCount, index);
			}
		}
	}

	void EntityTable::RemoveColumnLast()
	{
		for (int i = 0; i < storageCount; i++)
		{
			auto& columnData = storageColumns[i];
			columnData.RemoveLast();
		}
	}

	void EntityTable::RemoveColumns(U32 columns, U32 index)
	{
		for (U32 i = 0; i < columns; i++)
		{
			auto& columnData = storageColumns[i];
			auto& typeInfo = compTypeInfos[i];
			columnData.Remove(typeInfo.size, typeInfo.alignment, index);
		}
	}

	void EntityTable::GrowColumn(Vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo* compTypeInfo, size_t addCount, size_t newCapacity, bool construct)
	{
		U32 oldCount = (U32)columnData.GetCount();
		U32 oldCapacity = (U32)columnData.GetCapacity();

		// Realloc column data
		if (oldCapacity != newCapacity)
			columnData.Reserve(compTypeInfo->size, compTypeInfo->alignment, newCapacity);

		// Push new column datas and do placement new if we have ctor
		void* mem = columnData.PushBackN(compTypeInfo->size, compTypeInfo->alignment, addCount);
		if (construct && compTypeInfo && compTypeInfo->hooks.ctor != nullptr)
			compTypeInfo->hooks.ctor(mem, addCount, compTypeInfo);
	}

	U32 EntityTable::AppendNewEntity(EntityID entity, EntityInfo* info, bool construct)
	{
		U32 count = (U32)entities.size();

		// Add a new entity for table
		entities.push_back(entity);
		entityInfos.push_back(info);

		// Set table dirty
		SetTableDirty();

		// ensure that the columns have the same size as the entities and records.
		U32 newCapacity = (U32)entities.capacity();
		for (int i = 0; i < storageCount; i++)
		{
			ComponentColumnData& columnData = storageColumns[i];
			ComponentTypeInfo* compTypeInfo = &compTypeInfos[i];
			GrowColumn(entities, columnData, compTypeInfo, 1, newCapacity, construct);
		}

		// Pending empty table
		if (count == 0)
			SetEmpty();

		return count;
	}

	bool RegisterComponentRecord(WorldImpl* world, EntityTable* table, EntityID compID, I32 column, I32 count, TableComponentRecord& tableRecord)
	{
		// Register component and init component type info
		ComponentRecord* compRecord = EnsureComponentRecord(world, compID);
		ECS_ASSERT(compRecord != nullptr);
		compRecord->cache.InsertTableIntoCache(table, &tableRecord);

		// Init component type info
		if (!compRecord->typeInfoInited)
		{
			EntityID type = GetRealTypeID(world, compID);
			if (type != INVALID_ENTITYID)
			{
				compRecord->typeInfo = GetComponentTypeInfo(world, type);
				ECS_ASSERT(compRecord->typeInfo != nullptr);
			}
			compRecord->typeInfoInited = true;
		}

		// Init component table record
		tableRecord.data.compID = compID;
		tableRecord.data.column = column;	// Index for component from entity type
		tableRecord.data.count = count;

		return true;
	}

	struct TableTypeItem
	{
		U32 pos = 0;
		U32 count = 0;
	};
	void EntityTable::RegisterTableComponentRecords()
	{
		if (type.empty())
			return;

		bool hasChildOf = false;

		// Find all used compIDs
		std::unordered_map<EntityID, TableTypeItem> relations;
		std::unordered_map<EntityID, TableTypeItem> objects;
		for (U32 i = 0; i < type.size(); i++)
		{
			EntityID compId = type[i];
			if (ECS_HAS_ROLE(compId, EcsRolePair))
			{
				EntityID relation = ECS_GET_PAIR_FIRST(compId);
				if (relation != INVALID_ENTITYID)
				{
					if (relations.count(relation) == 0)
					{
						relations[relation] = {};
						relations[relation].pos = i;
					}
					relations[relation].count++;
				}

				EntityID obj = ECS_GET_PAIR_SECOND(compId);
				if (obj != INVALID_ENTITYID)
				{
					if (objects.count(relation) == 0)
					{
						objects[obj] = {};
						objects[obj].pos = i;
					}
					objects[obj].count++;
				}

				if (relation == EcsRelationChildOf)
					hasChildOf = true;
			}
		}

		size_t totalCount = type.size() + relations.size() + objects.size();
		if (!hasChildOf)
			totalCount++;

		tableRecords.resize(totalCount);

		// Register component record for base table type
		U32 index = 0;
		for (U32 i = 0; i < type.size(); i++)
		{
			RegisterComponentRecord(world, this, type[index], index, 1, tableRecords[index]);
			index++;
		}

		// Relations (Record all relations which table used)
		for (auto kvp : relations)
		{
			EntityID type = ECS_MAKE_PAIR(kvp.first, EcsPropertyNone);
			RegisterComponentRecord(world, this, type, kvp.second.pos, kvp.second.count, tableRecords[index]);
			index++;
		}

		// Objects (Record all objects which table used)
		for (auto kvp : objects)
		{
			EntityID type = ECS_MAKE_PAIR(EcsPropertyNone, kvp.first);
			RegisterComponentRecord(world, this, type, kvp.second.pos, kvp.second.count, tableRecords[index]);
			index++;
		}

		// Add default child record if withou childof 
		if (!hasChildOf && type.size() > 0)
			RegisterComponentRecord(world, this, ECS_MAKE_PAIR(EcsRelationChildOf, 0), index, index, tableRecords[index]);
	}

	void EntityTable::UnregisterTableRecords()
	{
		for (size_t i = 0; i < tableRecords.size(); i++)
		{
			TableComponentRecord* tableRecord = &tableRecords[i];
			EntityTableCacheBase* cache = tableRecord->tableCache;
			if (cache == nullptr)
				continue;

			ECS_ASSERT(tableRecord->table == this);

			cache->RemoveTableFromCache(this);

			if (cache->tableRecordMap.empty())
			{
				ComponentRecord* compRecord = reinterpret_cast<ComponentRecord*>(cache);
				RemoveComponentRecord(world, tableRecord->data.compID, compRecord);
			}
		}
		tableRecords.clear();
	}

	void EntityTable::SetEmpty()
	{
		EntityTable** tablePtr = world->pendingTables->Ensure(tableID);
		ECS_ASSERT(tablePtr != nullptr);
		(*tablePtr) = this;
	}

	size_t EntityTable::Count()const
	{
		return entities.size();
	}

	I32 EntityTable::GetStorageIndexByType(I32 index)
	{
		ECS_ASSERT(index >= 0);
		ECS_ASSERT(index < typeToStorageMap.size());
		return typeToStorageMap[index];
	}

	void* EntityTable::GetColumnData(I32 columnIndex)
	{
		ECS_ASSERT(columnIndex >= 0 && columnIndex < storageCount);
		auto typeinfo = compTypeInfos[columnIndex];
		return storageColumns[columnIndex].Get(typeinfo.size, typeinfo.alignment, 0);
	}

	void EntityTable::SortByEntity(QueryOrderByAction compare)
	{
		ECS_ASSERT(world != nullptr);
		if (storageCount < 0)
			return;

		if (Count() < 2)
			return;

		TableQuickSort(this, 0, (I32)Count() - 1, compare);
	}

	void EntityTable::SwapRows(I32 src, I32 dst)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(src >= 0 && src < entities.size());
		ECS_ASSERT(dst >= 0 && dst < entities.size());

		if (src == dst)
			return;

		// Set table dirty
		SetTableDirty();

		// Swap entities
		EntityID entitySrc = entities[src];
		EntityID entityDst = entities[dst];
		entities[dst] = entitySrc;
		entities[src] = entityDst;

		// Swap entity infos
		EntityInfo* entityInfoSrc = entityInfos[src];
		EntityInfo* entityInfoDst = entityInfos[dst];
		entityInfos[dst] = entityInfoSrc;
		entityInfos[src] = entityInfoDst;

		if (storageColumns.empty())
			return;

		// Find a maximum size of storage column for temporary buffer to swap
		size_t tempSize = 0;
		for (int i = 0; i < storageCount; i++)
			tempSize = std::max(tempSize, compTypeInfos[i].size);
		
		void* tmp = ECS_MALLOC(tempSize);
		for (int i = 0; i < storageCount; i++)
		{
			ComponentTypeInfo& typeInfo = compTypeInfos[i];
			auto& columnData = storageColumns[i];
			void* srcMem = columnData.Get(typeInfo.size, typeInfo.alignment, src);
			void* dstMem = columnData.Get(typeInfo.size, typeInfo.alignment, dst);

			memcpy(tmp, srcMem, typeInfo.size);
			memcpy(srcMem, dstMem, typeInfo.size);
			memcpy(dstMem, tmp, typeInfo.size);
		}
		ECS_FREE(tmp);
	}

	void EntityTable::SetTableDirty()
	{
		tableDirty++;
	}

	void EntityTable::SetColumnDirty(EntityID compID)
	{
		I32 index = TableSearchType(this, compID);
		ECS_ASSERT(index >= 0 && index < storageCount);
		columnDirty[index]++;
	}

	void EntityTable::InitTableFlags()
	{
		for (U32 i = 0; i < type.size(); i++)
		{
			EntityID compID = type[i];
			if (compID == EcsTagPrefab)
				flags |= TableFlagIsPrefab;
			else if (compID == EcsTagDisabled)
				flags |= TableFlagDisabled;


			if (ECS_HAS_ROLE(compID, EcsRolePair))
			{
				U32 relation = ECS_GET_PAIR_FIRST(compID);
				if (relation != INVALID_ENTITYID)
					flags |= TableFlagHasRelation;

				if (relation == EcsRelationIsA)
					flags |= TableFlagHasIsA;
				else if (relation == EcsRelationChildOf)
					flags |= TableFlagIsChild;
			}
		}
	}

	void EntityTable::InitStorageTable()
	{
		if (storageTable != nullptr)
			return;

		Vector<EntityID> usedCompIDs;
		for (U32 i = 0; i < type.size(); i++)
		{
			TableComponentRecord& tableRecord = tableRecords[i];
			ComponentRecord* compRecord = reinterpret_cast<ComponentRecord*>(tableRecord.tableCache);
			// We sure all component types is initialized in RegisterTableComponentRecords
			ECS_ASSERT(compRecord->typeInfoInited);

			if (compRecord->typeInfo == nullptr)
				continue;

			usedCompIDs.push_back(type[i]);
		}

		// Get storage ids
		if (usedCompIDs.size() > 0)
		{
			if (usedCompIDs.size() != type.size())
			{
				storageTable = FindOrCreateTableWithIDs(world, usedCompIDs);
				storageTable->refCount++;
				storageCount = (I32)storageTable->type.size();
				storageIDs = storageTable->type.data();
			}
			else
			{
				storageTable = this;
				storageCount = (I32)type.size();
				storageIDs = type.data();
			}
		}
		// Init storage map
		if (typeToStorageMap.empty() || storageToTypeMap.empty())
		{
			U32 numType = (U32)type.size();
			U32 numStorageType = storageCount;

			typeToStorageMap.resize(numType);
			storageToTypeMap.resize(numStorageType);

			U32 t, s;
			for (s = 0, t = 0; (t < numType) && (s < numStorageType); )
			{
				EntityID id = type[t];
				EntityID storageID = storageIDs[s];
				if (id == storageID)
				{
					typeToStorageMap[t] = s;
					storageToTypeMap[s] = t;
				}
				else
				{
					typeToStorageMap[t] = -1;
				}

				t += (id <= storageID);
				s += (id == storageID);
			}

			//  Process remainning of type
			for (; (t < numType); t++) {
				typeToStorageMap[t] = -1;
			}
		}

		// Init storage column datas
		if (storageCount > 0)
			storageColumns.resize(storageCount);
	}

	void EntityTable::InitTypeInfos()
	{
		if (!storageTable)
			return;

		// Same stroage components with storage table,
		// we can share the same type info
		if (storageTable != this)
		{
			compTypeInfos = storageTable->compTypeInfos;
			flags |= storageTable->flags;
			return;
		}

		// Get component type info from ComponentRecord
		compTypeInfos = ECS_CALLOC_T_N(ComponentTypeInfo, type.size());
		ECS_ASSERT(compTypeInfos != nullptr);
		for (int i = 0; i < type.size(); i++)
		{
			TableComponentRecord* tableRecord = &tableRecords[i];
			ComponentRecord* compRecord = reinterpret_cast<ComponentRecord*>(tableRecord->tableCache);
			ECS_ASSERT(compRecord != nullptr && compRecord->typeInfoInited);
			ECS_ASSERT(compRecord->typeInfo);
			compTypeInfos[i] = *compRecord->typeInfo;

			ComponentTypeHooks& hooks = compRecord->typeInfo->hooks;
			if (hooks.ctor)
				flags |= TableFlagHasCtors;
			if (hooks.dtor)
				flags |= TableFlagHasDtors;
			if (hooks.copy)
				flags |= TableFlagHasCopy;
			if (hooks.move)
				flags |= TableFlagHasMove;
		}
	}

	void TableNotifyOnSet(WorldImpl* world, EntityTable* table, I32 row, I32 count, EntityID compID)
	{
		ECS_ASSERT(world != NULL);
		ECS_ASSERT(row + count <= table->entities.size());
		EntityID* entities = &table->entities[row];
		TableComponentRecord* record = GetTableRecord(world, table, compID);
		if (record == nullptr)
			return;

		ComponentTypeInfo* typeInfo = &table->compTypeInfos[record->data.column];
		auto onSetCallback = typeInfo->hooks.onSet;
		if (onSetCallback)
		{
			OnComponentCallback(
				world,
				table,
				typeInfo,
				onSetCallback,
				&table->storageColumns[record->data.column],
				entities,
				compID,
				row,
				count);
		}
	}
}