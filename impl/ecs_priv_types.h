#pragma once

#include "ecs_def.h"
#include "ecs_util.h"

namespace ECS
{
	struct WorldImpl;

	const EntityID HiComponentID = 256;

	struct InfoComponent
	{
		size_t size = 0;
		size_t algnment = 0;
	};

	struct NameComponent
	{
		const char* name = nullptr;
		U64 hash = 0;
	};

	extern EntityID ECS_ENTITY_ID(InfoComponent);
	extern EntityID ECS_ENTITY_ID(NameComponent);
	extern EntityID ECS_ENTITY_ID(SystemComponent);
	extern EntityID ECS_ENTITY_ID(PipelineComponent);
	extern EntityID ECS_ENTITY_ID(TriggerComponent);
	extern EntityID ECS_ENTITY_ID(ObserverComponent);

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
		EntityID compID = INVALID_ENTITYID;
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
		TableFlagDisabled = 1 << 8,
		TableFlagHasOnSet = 1 << 9
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

		// Dirty infos	
		I32 tableDirty;
		Vector<I32> columnDirty;	// Comp1Dirty,    Comp2Dirty,    Comp3Dirty

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
		void* GetColumnData(I32 columnIndex);
		void SortByEntity(QueryOrderByAction compare);
		void SwapRows(I32 src, I32 dst);
		void SetTableDirty();
		void SetColumnDirty(EntityID id);

		I32 GetTableDirty()const {
			return tableDirty;
		}
		const I32* GetColumnDirty()const {
			return columnDirty.data();
		}

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
		Hashmap<EntityID> entityNameMap;
		ComponentTypeInfo* typeInfo = nullptr;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Query
	////////////////////////////////////////////////////////////////////////////////

	const size_t QUERY_ITEM_SMALL_CACHE_SIZE = 4;

	struct QueryTableNode : Util::ListNode<QueryTableNode>
	{
		struct QueryTableMatch* match;
		I32 offset = 0;					// Starting point in table
		I32 count = 0;					// Number of entities
	};

	struct QueryTableMatch
	{
		QueryTableNode node;
		EntityTable* table = nullptr;
		I32 termCount = 0;
		U64* ids = nullptr;
		I32* columns = nullptr;
		size_t* sizes = nullptr;
		U64 groupID = 0;
		QueryTableMatch* nextMatch = nullptr;
		I32* monitor = nullptr;
	};
	using QueryTableList = Util::List<QueryTableNode>;

	struct QueryTableCacheData
	{
		QueryTableMatch* first = nullptr;
		QueryTableMatch* last = nullptr;
	};
	using QueryTableCache = EntityTableCacheItemInst<QueryTableCacheData>;

	struct QueryImpl
	{
		U64 queryID;							 // Query uniqure ptr
		I32 sortByItemIndex = 0;
		I32 matchingCount = 0;
		I32 prevMatchingCount = 0;

		Filter filter;
		Iterable iterable;

		// Tables
		EntityTableCache<QueryTableCache> cache; // All matched tables <QueryTableCache>
		QueryTableList tableList;	         // Non-empty ordered tables
		
		QueryOrderByAction orderBy;				
		Vector<QueryTableNode> tableSlices;     // Table sorted by orderby

		// Group
		EntityID groupByID = INVALID_ENTITYID;
		Term* groupByItem = nullptr;
		Map<QueryTableList> groups;

		// Observer
		EntityID observer = INVALID_ENTITYID;

		// Monitor
		Vector<I32> monitor;
		bool hasMonitor = false;

		WorldImpl* world = nullptr;
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

	struct ObjectBase
	{
		I32 type;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Object
	////////////////////////////////////////////////////////////////////////////////

	inline void InitEcsObject(ObjectBase* object, I32 type)
	{
		object->type = type;
	}

	inline bool IsEcsObject(ObjectBase* object, I32 type)
	{
		return object->type == type;
	}

#define ECS_DEFINE_OBEJCT(type) type##_magic
#define ECS_INIT_OBJECT(object, type)\
    InitEcsObject(&object->base, type##_magic)
#define ECS_CHECK_OBJECT(object, type) \
	IsEcsObject(object, type##_magic)

	extern const U32 ECS_DEFINE_OBEJCT(WorldImpl);
	extern const U32 ECS_DEFINE_OBEJCT(Stage);

	////////////////////////////////////////////////////////////////////////////////
	//// WorldImpl
	////////////////////////////////////////////////////////////////////////////////

	enum DeferOperationKind 
	{
		EcsOpNew,
		EcsOpAdd,
		EcsOpRemove,
		EcsOpSet,
		EcsOpMut,
		EcsOpModified,
		EcsOpDelete,
		EcsOpClear,
		EcsOpOnDeleteAction,
		EcsOpEnable,
		EcsOpDisable
	};

	struct DeferOperation
	{
		EntityID id;
		DeferOperationKind kind;
		EntityID entity;
		size_t size;
		void* value;
	};

	struct SuspendReadonlyState
	{
		bool isReadonly = false;
		bool isDeferred = false;
		EntityID scope = INVALID_ENTITYID;
		I32 defer = 0;
		Vector<DeferOperation> deferQueue;
		Util::Stack deferStack;
	};

	struct Stage
	{
		// Base object info
		ObjectBase base;

		// Base info
		I32 id = 0;
		EntityID scope;

		// Thread
		void* thread = 0;	
		ObjectBase* threadCtx = nullptr;
		WorldImpl* world = nullptr;

		// Deferred
		I32 defer = 0;
		bool deferSuspend = false;
		Vector<DeferOperation> deferQueue;
		Util::Stack deferStack;
	};

	struct WorldImpl
	{
		// Base object info
		ObjectBase base;

		// ID infos
		EntityID lastComponentID = 0;
		EntityID lastID = 0;

		// Entity
		Util::SparseArray<EntityInfo> entityPool;

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

		// Pipeline
		EntityID pipeline = 0;
		ThreadContext threadCtx;

		// Stages
		Stage* stages = nullptr;
		I32 stageCount = 0;

		// Status
		bool isReadonly = false;
		bool isMultiThreaded = false;
		bool isFini = false;
	};
}