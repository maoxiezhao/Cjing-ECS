#pragma once

#include "common.h"
#include "ecs_def.h"
#include "ecs_util.h"

namespace ECS
{
	struct WorldImpl;

	const EntityID HiComponentID = 256;

	////////////////////////////////////////////////////////////////////////////////
	//// Entity info
	////////////////////////////////////////////////////////////////////////////////

	struct EntityInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Table graph
	////////////////////////////////////////////////////////////////////////////////

	struct EntityTableDiff
	{
		EntityIDs added;			// Components added between tablePool
		EntityIDs removed;			// Components removed between tablePool
	};
	static EntityTableDiff EMPTY_TABLE_DIFF;

	struct TableGraphEdge : Util::ListNode<TableGraphEdge>
	{
		EntityTable* from = nullptr;
		EntityTable* to = nullptr;
		EntityID compID = INVALID_ENTITY;
		EntityTableDiff* diff = nullptr; // mapping to TableGraphNode diffBuffer
	};

	struct TableGraphEdges
	{
		TableGraphEdge loEdges[HiComponentID]; // id < HiComponentID
		Hashmap<TableGraphEdge*> hiEdges;
	};

	struct TableGraphNode
	{
		TableGraphEdges add;
		TableGraphEdges remove;
		TableGraphEdge incomingEdges;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Table cache
	////////////////////////////////////////////////////////////////////////////////

	struct EntityTableCacheBase
	{
		Hashmap<EntityTableCacheItem*> tableRecordMap; // <TableID, CacheItem>
		Util::List<EntityTableCacheItem> tables;
		Util::List<EntityTableCacheItem> emptyTables;

		void InsertTableIntoCache(const EntityTable* table, EntityTableCacheItem* cacheNode);
		EntityTableCacheItem* RemoveTableFromCache(EntityTable* table);
		EntityTableCacheItem* GetTableCache(EntityTable* table);
		bool SetTableCacheState(EntityTable* table, bool isEmpty);

		I32 GetTableCount()const {
			return tables.count;
		}

		I32 GetEmptyTableCount()const {
			return emptyTables.count;
		}

	private:
		void ListInsertNode(EntityTableCacheItem* node, bool isEmpty)
		{
			Util::List<EntityTableCacheItem>& list = isEmpty ? emptyTables : tables;
			Util::ListNode<EntityTableCacheItem>* last = list.last;
			list.last = node;
			list.count++;
			if (list.count == 1)
				list.first = node;

			node->next = nullptr;
			node->prev = last;

			if (last != nullptr)
				last->next = node;
		}

		void ListRemoveNode(EntityTableCacheItem* node, bool isEmpty)
		{
			if (node == nullptr)
				return;

			Util::List<EntityTableCacheItem>& list = isEmpty ? emptyTables : tables;
			if (node->prev != nullptr)
				node->prev->next = node->next;
			if (node->next != nullptr)
				node->next->prev = node->prev;

			list.count--;

			if (node == list.first)
				list.first = node->next;
			if (node == list.last)
				list.last = node->prev;
		}
	};

	template<typename T, typename = int>
	struct EntityTableCache {};

	template<typename T>
	struct EntityTableCache<T, std::enable_if_t<std::is_base_of_v<EntityTableCacheItem, T>, int>> : EntityTableCacheBase {};

	////////////////////////////////////////////////////////////////////////////////
	//// EntityTable
	////////////////////////////////////////////////////////////////////////////////

	enum TableFlag
	{
		TableFlagIsPrefab = 1 << 0,
		TableFlagHasRelation = 1 << 1,
		TableFlagHasIsA = 1 << 2,
		TableFlagIsChild = 1 << 3,
		TableFlagHasCtors = 1 << 4,
		TableFlagHasDtors = 1 << 5,
		TableFlagHasCopy = 1 << 6,
		TableFlagHasMove = 1 << 7,
	};

	struct TableComponentRecordData
	{
		U64 compID = 0;
		I32 column = 0;					// The column of comp in target table
		I32 count = 0;
	};
	using TableComponentRecord = EntityTableCacheItemInst<TableComponentRecordData>;

	using ComponentColumnData = Util::StorageVector;

	struct EntityTable
	{
	public:
		WorldImpl* world = nullptr;
		EntityType type;
		U64 tableID = 0;
		TableGraphNode graphNode;
		bool isInitialized = false;
		U32 flags = 0;
		I32 refCount = 0;

		// Storage
		I32 storageCount = 0;
		EntityID* storageIDs;
		Vector<I32> typeToStorageMap;
		Vector<I32> storageToTypeMap;
		EntityTable* storageTable = nullptr;		// Without tags
		Vector<EntityID> entities;
		Vector<EntityInfo*> entityInfos;
		Vector<ComponentColumnData> storageColumns; // Comp1,         Comp2,         Comp3
		ComponentTypeInfo* compTypeInfos;			// CompTypeInfo1, CompTypeInfo2, CompTypeInfo3
		Vector<TableComponentRecord> tableRecords;  // CompTable1,    CompTable2,    CompTable3

		bool InitTable(WorldImpl* world_);
		void Claim();
		bool Release();
		void Free();
		void FiniData(bool updateEntity, bool deleted);
		void DeleteEntity(U32 index, bool destruct);
		void RemoveColumnLast();
		void RemoveColumns(U32 columns, U32 index);
		void GrowColumn(Vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo* compTypeInfo, size_t addCount, size_t newCapacity, bool construct);
		U32  AppendNewEntity(EntityID entity, EntityInfo* info, bool construct);
		void RegisterTableComponentRecords();
		void UnregisterTableRecords();
		void SetEmpty();
		size_t Count()const;
		I32 GetStorageIndexByType(I32 index);

	private:
		void InitTableFlags();
		void InitStorageTable();
		void InitTypeInfos();
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Component
	////////////////////////////////////////////////////////////////////////////////

	struct ComponentRecord
	{
		EntityTableCache<TableComponentRecord> cache;
		bool typeInfoInited = false;
		ComponentTypeInfo* typeInfo = nullptr;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Query
	////////////////////////////////////////////////////////////////////////////////

	const size_t QUERY_ITEM_SMALL_CACHE_SIZE = 4;

	struct QueryTableMatch : Util::ListNode<QueryTableMatch>
	{
		EntityTable* table = nullptr;
		I32 termCount = 0;
		U64* ids = nullptr;
		I32* columns = nullptr;
		size_t* sizes = nullptr;
		U64 groupID = 0;
		QueryTableMatch* nextMatch = nullptr;
	};
	using QueryTableMatchList = Util::List<QueryTableMatch>;

	struct QueryTableCacheData
	{
		QueryTableMatch* first = nullptr;
		QueryTableMatch* last = nullptr;
	};
	using QueryTableCache = EntityTableCacheItemInst<QueryTableCacheData>;

	struct QueryImpl
	{
		WorldImpl* world = nullptr;
		U64 queryID;
		I32 sortByItemIndex = 0;
		I32 matchingCount = 0;
		I32 prevMatchingCount = 0;
		Filter filter;
		Iterable iterable;

		// Tables
		EntityTableCache<QueryTableCache> cache; // All matched tables <QueryTableCache>
		QueryTableMatchList tableList;	         // Non-empty ordered tables

		// Group
		EntityID groupByID = INVALID_ENTITY;
		Term* groupByItem = nullptr;
		Map<QueryTableMatchList> groups;

		// Observer
		EntityID observer = INVALID_ENTITY;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Events
	////////////////////////////////////////////////////////////////////////////////

	struct Trigger
	{
		Term term;
		EntityID events[ECS_TRIGGER_MAX_EVENT_COUNT];
		I32 eventCount = 0;
		Observable* observable = nullptr;
		IterCallbackAction callback;
		void* ctx = nullptr;

		// Used if this trigger is part of Observer
		I32* eventID = nullptr;
		I32 id = 0;
		EntityID entity;
		WorldImpl* world;
	};

	struct EventRecord
	{
		Map<Trigger*> triggers;
		I32 triggerCount = 0;
	};

	struct EventRecords
	{
		Map<EventRecord> eventIds; // Map<CompID, EventRecord>
	};

	struct Observable
	{
		Util::SparseArray<EventRecords> events;	// Sparse<EventID, EventRecords>
	};

	////////////////////////////////////////////////////////////////////////////////
	//// WorldImpl
	////////////////////////////////////////////////////////////////////////////////

	struct WorldImpl
	{
		// ID infos
		EntityID lastComponentID = 0;
		EntityID lastID = 0;

		// Entity
		Util::SparseArray<EntityInfo> entityPool;
		Hashmap<EntityID> entityNameMap;

		// Tables
		EntityTable root;
		Util::SparseArray<EntityTable> tablePool;
		Hashmap<EntityTable*> tableTypeHashMap;

		// Table edge cache
		TableGraphEdge* freeEdge = nullptr;

		// Pending tables
		Util::SparseArray<EntityTable*>* pendingTables;
		Util::SparseArray<EntityTable*>* pendingBuffer;

		// Component
		Hashmap<ComponentRecord*> compRecordMap;
		Util::SparseArray<ComponentTypeInfo> compTypePool;	// Component reflect type info

		// Query
		Util::SparseArray<QueryImpl> queryPool;

		// Events
		Observable observable;
		Util::SparseArray<Observer> observers;
		Util::SparseArray<Trigger> triggers;
		int32_t eventID = 0;	// Unique id is used to distinguish events

		// Status
		bool isReadonly = false;
		bool isFini = false;
		U32 defer = 0;
	};
}