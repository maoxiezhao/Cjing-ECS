#include "ecs.h"

// Finished
// 1. Query refactor
// 2. Event/Trigger/Observer

// TODO
// 1. Work pipeline
// 2. Mult threads
// 3. Shared component (GetBaseComponent)
// 4. Serialization

namespace ECS
{
	struct WorldImpl;
	struct EntityTable;


	// -----------------------------------------
	//            EntityID: 64                 |
	//__________________________________________
	// 	 32            |          32           |
	// ----------------------------------------|
	//   FFffFFff      |   FFFFffff            |
	// ----------------------------------------|
	//   generation    |    entity             |
	// ----------------------------------------|
	// 
	// -----------------------------------------
	//            ID: 64                       |
	//__________________________________________
	// 	 8     |     24          |    32       |
	// ----------------------------------------|
	//   ff    |     ffFFff      |   FFFFffff  |
	// ----------------------------------------|
	//  role   |   generation    |    entity   |
	// ----------------------------------------|
	//  role   |          component            |
	//------------------------------------------

	// Roles is EcsRolePair
	// -----------------------------------------
	//            EntityID: 64                 |
	//__________________________________________
	// 	 8     |     24          |    32       |
	// ----------------------------------------|
	//   ff    |     ffFFff      |   FFFFffff  |
	// ------------------------------------------
	//  Pair   |    Relation     |   Object    |
	// -----------------------------------------
	// Usage:
	// ECS_GET_PAIR_FIRST to get relation
	// ECS_GET_PAIR_SECOND to get object
	// ECS_MAKE_PAIR to make pair of relation and object

	const EntityID EcsRolePair = ((0x01ull) << 56);
	const EntityID EcsRoleShared = ((0x02ull) << 56);

	const size_t ENTITY_PAIR_FLAG = EcsRolePair;

	////////////////////////////////////////////////////////////////////////////////
	//// Builtin ids
	////////////////////////////////////////////////////////////////////////////////
	const EntityID HiComponentID = 256;
	const U32 FirstUserComponentID = 32;               // [32 - 256] user components	
	const U32 FirstUserEntityID = HiComponentID + 128; // [256 - 384] builtin tags

	EntityID BuiltinEntityID = HiComponentID;
	#define BUILTIN_ENTITY_ID (BuiltinEntityID++)

	// properties
	const EntityID EcsPropertyTag = BUILTIN_ENTITY_ID;
	const EntityID EcsPropertyNone = BUILTIN_ENTITY_ID;
	// Tags
	const EntityID EcsTagPrefab = BUILTIN_ENTITY_ID;
	// Events
	const EntityID EcsEventTableEmpty = BUILTIN_ENTITY_ID;
	const EntityID EcsEventTableFill = BUILTIN_ENTITY_ID;
	const EntityID EcsEventOnAdd = BUILTIN_ENTITY_ID;
	const EntityID EcsEventOnRemove = BUILTIN_ENTITY_ID;
	// Relations
	const EntityID EcsRelationIsA = BUILTIN_ENTITY_ID;
	const EntityID EcsRelationChildOf = BUILTIN_ENTITY_ID;

	////////////////////////////////////////////////////////////////////////////////
	//// Definition
	////////////////////////////////////////////////////////////////////////////////

	#define ECS_ENTITY_ID(e) (ECS_ID_##e)
	#define ECS_ENTITY_MASK               (0xFFFFffffull)	// 32
	#define ECS_ROLE_MASK                 (0xFFull << 56)
	#define ECS_COMPONENT_MASK            (~ECS_ROLE_MASK)	// 56
	#define ECS_GENERATION_MASK           (0xFFFFull << 32)
	#define ECS_GENERATION(e)             ((e & ECS_GENERATION_MASK) >> 32)

	#define ECS_HAS_ROLE(e, p) ((e & ECS_ROLE_MASK) == p)
	#define ECS_GET_PAIR_FIRST(e) (ECS_ENTITY_HI(e & ECS_COMPONENT_MASK))
	#define ECS_GET_PAIR_SECOND(e) (ECS_ENTITY_LOW(e))
	#define ECS_HAS_RELATION(e, rela) (ECS_HAS_ROLE(e, EcsRolePair) && ECS_GET_PAIR_FIRST(e) == rela)

	inline U64 EntityTypeHash(const EntityType& entityType)
	{
		return Util::HashFunc(entityType.data(), entityType.size() * sizeof(EntityID));
	}

	inline EntityID StripGeneration(EntityID id)
	{
		if (id & ECS_ROLE_MASK)
			return id;

		return id & (~ECS_GENERATION_MASK);
	}

	inline void DefaultCtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		memset(ptr, 0, size * count);
	}

	void InitFilterIter(World* world, const void* iterable, Iterator* it, Term* filter);
	bool NextFilterIter(Iterator* it);
	void InitQueryIter(World* world, const void* iterable, Iterator* it, Term* filter);
	bool NextQueryIter(Iterator* it);

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
		TableFlagIsPrefab    = 1 << 0,
		TableFlagHasRelation = 1 << 1,
		TableFlagHasIsA		 = 1 << 2,
		TableFlagIsChild	 = 1 << 3,
		TableFlagHasCtors	 = 1 << 4,
		TableFlagHasDtors	 = 1 << 5,
		TableFlagHasCopy	 = 1 << 6,
		TableFlagHasMove	 = 1 << 7,
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

	enum class QueryEventType
	{
		Invalid,
		MatchTable,
		UnmatchTable
	};

	struct QueryEvent
	{
		QueryEventType type = QueryEventType::Invalid;
		EntityTable* table;
	};

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
	//// Builtin components
	////////////////////////////////////////////////////////////////////////////////

#define BuiltinCompDtor(type) type##_dtor

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

	struct SystemComponent
	{
		EntityID entity;
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
		QueryImpl* query;
	};

	struct TriggerComponent
	{
		Trigger* trigger = nullptr;
	};
	static void BuiltinCompDtor(TriggerComponent)(World* world, EntityID* entities, size_t size, size_t count, void* ptr);

	struct ObserverComponent
	{
		Observer* observer = nullptr;
	};	
	static void BuiltinCompDtor(ObserverComponent)(World* world, EntityID* entities, size_t size, size_t count, void* ptr);

	EntityID BuiltinComponentID = 1;
	#define BUILTIN_COMPONENT_ID (BuiltinComponentID++)

	EntityID ECS_ENTITY_ID(InfoComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(NameComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(SystemComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(TriggerComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(ObserverComponent) = BUILTIN_COMPONENT_ID;

	////////////////////////////////////////////////////////////////////////////////
	//// WorldImpl
	////////////////////////////////////////////////////////////////////////////////

	struct WorldImpl : public World
	{
		EntityBuilder entityBuilder = EntityBuilder(this);

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

		WorldImpl()
		{
			pendingTables = ECS_NEW_OBJECT<Util::SparseArray<EntityTable*>>();
			pendingBuffer = ECS_NEW_OBJECT<Util::SparseArray<EntityTable*>>();

			compRecordMap.reserve(HiComponentID);
			entityPool.SetSourceID(&lastID);
			if (!root.InitTable(this))
				ECS_ASSERT(0);

			// Skip id 0
			U64 id = tablePool.NewIndex();
			ECS_ASSERT(id == 0);
			id = queryPool.NewIndex();
			ECS_ASSERT(id == 0);

			SetupComponentTypes();
			InitBuiltinComponents();
			InitBuiltinEntites();
			InitSystemComponent();
		}

		~WorldImpl()
		{	
			isFini = true;

			// Begin defer to discard all operations (Add/Delete/Dtor)
			BeginDefer();

			// Free all tables, neet to skip id 0
			size_t tabelCount = tablePool.Count();
			for (size_t i = 1; i < tabelCount; i++)
			{
				EntityTable* table = tablePool.GetByDense(i);
				if (table != nullptr)
					table->Release();
			}
			tablePool.Clear();
			pendingTables->Clear();
			pendingBuffer->Clear();

			// Free root table
			root.Release();

			// Free graph edges
			Util::ListNode<TableGraphEdge>* cur, *next = freeEdge;
			while ((cur = next))
			{
				next = cur->next;
				ECS_FREE(cur);
			}

			// Fini all queries
			FiniQueries();

			// Fini component records
			FiniComponentRecords();

			// Fini component type infos
			FiniComponentTypeInfos();

			// Clear entity pool
			entityPool.Clear();

			ECS_DELETE_OBJECT(pendingBuffer);
			ECS_DELETE_OBJECT(pendingTables);
		}

		void BeginDefer()
		{
			// TODO
			defer++;
		}

		void EndDefer()
		{
			// TODO
			FlushDefer();
		}

		bool FlushDefer()
		{
			if (--defer > 0)
				return false;

			// TODO
			// Flush defer queue
			return true;
		}

		const EntityBuilder& CreateEntity(const char* name) override
		{
			entityBuilder.entity = CreateEntityID(name);
			return entityBuilder;
		}

		const EntityBuilder& CreatePrefab(const char* name) override
		{
			EntityID entity = CreateEntityID(name);
			AddComponent(entity, EcsTagPrefab);
			entityBuilder.entity = entity;
			return entityBuilder;
		}

		EntityID CreateEntityID(const char* name) override
		{
			EntityCreateDesc desc = {};
			desc.name = name;
			desc.useComponentID = false;
			return CreateEntityID(desc);
		}

		EntityID FindEntityIDByName(const char* name) override
		{
			// First find from entityNameMap
			auto it = entityNameMap.find(Util::HashFunc(name, strlen(name)));
			if (it != entityNameMap.end())
				return it->second;

			// Init a filter to get all entity which has NameComponent
			// TODO...

			return INVALID_ENTITY;
		}

		bool EntityExists(EntityID entity)const override
		{
			ECS_ASSERT(entity != INVALID_ENTITY);
			return entityPool.CheckExsist(entity);
		}

		bool IsEntityValid(EntityID entity)const
		{
			if (entity == INVALID_ENTITY)
				return false;

			// Entity identifiers should not contain flag bits
			if (entity & ECS_ROLE_MASK)
				return false;

			if (!EntityExists(entity))
				return ECS_GENERATION(entity) == 0;

			return IsEntityAlive(entity);
		}
			
		bool IsEntityAlive(EntityID entity)const
		{
			return entityPool.Get(entity) != nullptr;
		}
		
		bool DeferDeleteEntity(EntityID entity)
		{
			// TODO
			if (defer > 0)
			{
				// TODO: Add a job into the defer queue
				return true;
			}

			return false;
		}

		void DeleteEntity(EntityID entity) override
		{ 
			ECS_ASSERT(entity != INVALID_ENTITY);

			if (DeferDeleteEntity(entity))
				return;

			EntityInfo* entityInfo = entityPool.Get(entity);
			if (entityInfo == nullptr)
				return;
			
			U64 tableID = 0;
			if (entityInfo->table)
				tableID = entityInfo->table->tableID;

			if (tableID > 0 && tablePool.CheckExsist(tableID))
				entityInfo->table->DeleteEntity(entityInfo->row, true);

			entityInfo->row = 0;
			entityInfo->table = nullptr;
			entityPool.Remove(entity);
		}

		void SetEntityName(EntityID entity, const char* name) override
		{
			NameComponent nameComp = {};
			nameComp.name = _strdup(name);
			nameComp.hash = Util::HashFunc(name, strlen(name));
			SetComponent(entity, ECS_ENTITY_ID(NameComponent), sizeof(NameComponent), &nameComp, false);
		}

		const char* GetEntityName(EntityID entity)override
		{
			ECS_ASSERT(IsEntityValid(entity));
			const NameComponent* ptr = static_cast<const NameComponent*>(GetComponent(entity, ECS_ENTITY_ID(NameComponent)));
			return ptr ? ptr->name : nullptr;
		}

		void EnsureEntity(EntityID entity) override
		{
			if (ECS_HAS_ROLE(entity, EcsRolePair))
			{
				EntityID re = ECS_GET_PAIR_FIRST(entity);
				EntityID comp = ECS_GET_PAIR_SECOND(entity);

				if (GetAliveEntity(re) != re)
					entityPool.Ensure(re);

				if (GetAliveEntity(comp) != comp)
					entityPool.Ensure(comp);
			}
			else
			{
				if (GetAliveEntity(StripGeneration(entity)) == entity)
					return;

				entityPool.Ensure(entity);
			}
		}

		void Instantiate(EntityID entity, EntityID prefab) override
		{
			AddComponent(entity, ECS_MAKE_PAIR(EcsRelationIsA, prefab));
		}

		void ChildOf(EntityID entity, EntityID parent)override
		{
			AddComponent(entity, ECS_MAKE_PAIR(EcsRelationChildOf, parent));
		}

		EntityID GetParent(EntityID entity)override
		{
			return GetRelationObject(entity, EcsRelationChildOf, 0);
		}

		EntityID GetRelationObject(EntityID entity, EntityID relation, U32 index = 0)override
		{
			EntityTable* table = GetTable(entity);
			if (table == nullptr)
				return INVALID_ENTITY;

			TableComponentRecord* record = GetTableRecord(table, ECS_MAKE_PAIR(relation, EcsPropertyNone));
			if (record == nullptr)
				return INVALID_ENTITY;
	
			if (index >= (U32)record->data.count)
				return INVALID_ENTITY;

			return ECS_GET_PAIR_SECOND(table->type[record->data.column + index]);
		}

		void* GetComponent(EntityID entity, EntityID compID) override
		{
			EntityInfo* info = entityPool.Get(entity); 
			if (info == nullptr || info->table == nullptr)
				return nullptr;

			EntityTable* table = info->table;
			if (table->storageTable == nullptr)
				return nullptr;

			TableComponentRecord* tableRecord = GetTableRecord(table->storageTable, compID);
			if (tableRecord == nullptr)
				return nullptr;

			return GetComponentPtrFromTable(*info->table, info->row, tableRecord->data.column);
		}

		bool HasComponent(EntityID entity, EntityID compID) override
		{
			ECS_ASSERT(compID != INVALID_ENTITY);
			EntityTable* table = GetTable(entity);
			if (table == nullptr)
				return false;

			return TableSearchType(table, compID) != -1;
		}

		void AddRelation(EntityID entity, EntityID relation, EntityID compID)override
		{
			AddComponent(entity, ECS_MAKE_PAIR(relation, compID));
		}

		bool HasComponentTypeInfo(EntityID compID)const override
		{
			return GetComponentTypeInfo(compID) != nullptr;
		}

		ComponentTypeInfo* EnsureComponentTypInfo(EntityID compID)
		{
			return compTypePool.Ensure(compID);
		}

		ComponentTypeInfo* GetComponentTypeInfo(EntityID compID) override
		{
			return compTypePool.Get(compID);
		}

		const ComponentTypeInfo* GetComponentTypeInfo(EntityID compID)const override
		{
			return compTypePool.Get(compID);
		}

		const ComponentTypeHooks* GetComponentTypeHooks(EntityID compID)const override
		{
			auto typeInfo = GetComponentTypeInfo(compID);
			if (typeInfo != nullptr)
				return &typeInfo->hooks;
			return nullptr;
		}

		// Set component type info for component id
		void SetComponentTypeInfo(EntityID compID, const ComponentTypeHooks& info) override
		{
			ComponentTypeInfo* compTypeInfo = compTypePool.Ensure(compID);
			size_t size = compTypeInfo->size;
			size_t alignment = compTypeInfo->alignment;
			if (size == 0)
			{
				InfoComponent* infoComponent = GetComponentInfo(compID);
				if (infoComponent != nullptr)
				{
					size = infoComponent->size;
					alignment = infoComponent->algnment;
				}
			}

			compTypeInfo->compID = compID;
			compTypeInfo->size = size;
			compTypeInfo->alignment = alignment;
			compTypeInfo->hooks = info;

			// Set default constructor
			if (!info.ctor && (info.dtor || info.copy || info.move))
				compTypeInfo->hooks.ctor = DefaultCtor;
		}

		EntityID InitNewComponent(const ComponentCreateDesc& desc) override
		{
			EntityID entityID = CreateEntityID(desc.entity);
			if (entityID == INVALID_ENTITY)
				return INVALID_ENTITY;

			bool added = false;
			InfoComponent* info = static_cast<InfoComponent*>(GetOrCreateMutableByID(entityID, ECS_ENTITY_ID(InfoComponent), &added));
			if (info == nullptr)
				return INVALID_ENTITY;

			if (added)
			{
				info->size = desc.size;
				info->algnment = desc.alignment;
			}
			else
			{
				ECS_ASSERT(info->size == desc.size);
				ECS_ASSERT(info->algnment == desc.alignment);
			}

			if (entityID >= lastComponentID && entityID < HiComponentID)
				lastComponentID = (U32)(entityID + 1);

			return entityID;
		}

		void* GetOrCreateComponent(EntityID entity, EntityID compID) override
		{
			bool isAdded = false;
			void* comp = GetOrCreateMutableByID(entity, compID, &isAdded);
			ECS_ASSERT(comp != nullptr);
			return comp;
		}

		void AddComponent(EntityID entity, EntityID compID) override
		{
			ECS_ASSERT(IsEntityValid(entity));
			ECS_ASSERT(IsCompIDValid(compID));
			AddComponentImpl(entity, compID);
		}

		void RemoveComponent(EntityID entity, EntityID compID)override
		{
			ECS_ASSERT(IsEntityValid(entity));
			ECS_ASSERT(IsCompIDValid(compID));

			EntityInfo* info = entityPool.Get(entity);
			if (info == nullptr || info->table == nullptr)
				return;

			EntityTableDiff diff = {};
			EntityTable* newTable = TableTraverseRemove(info->table, compID, diff);
			CommitTables(entity, info, newTable, diff, true);
		}

		void AddComponentImpl(EntityID entity, EntityID compID)
		{
			EntityInfo* info = entityPool.Ensure(entity);
			EntityTableDiff diff = {};
			EntityTable* srcTable = info->table;
			EntityTable* newTable = TableTraverseAdd(srcTable, compID, diff);
			CommitTables(entity, info, newTable, diff, true);
		}

		////////////////////////////////////////////////////////////////////////////////
		//// System
		////////////////////////////////////////////////////////////////////////////////

		EntityID InitNewSystem(const SystemCreateDesc& desc) override
		{
			EntityID entity = CreateEntityID(desc.entity);
			if (entity == INVALID_ENTITY)
				return INVALID_ENTITY;

			bool newAdded = false;
			SystemComponent* sysComponent = static_cast<SystemComponent*>(GetOrCreateMutableByID(entity, ECS_ENTITY_ID(SystemComponent), &newAdded));
			if (newAdded)
			{
				memset(sysComponent, 0, sizeof(SystemComponent));
				sysComponent->entity = entity;
				sysComponent->action = desc.action;
				sysComponent->invoker = desc.invoker;
				sysComponent->invokerDeleter = desc.invokerDeleter;

				QueryImpl* queryInfo = CreateQuery(desc.query);
				if (queryInfo == nullptr)
					return INVALID_ENTITY;

				sysComponent->query = queryInfo;
			}
			return entity;
		}

		void RunSystem(EntityID entity) override
		{
			ECS_ASSERT(entity != INVALID_ENTITY);
			SystemComponent* sysComponent =static_cast<SystemComponent*>(GetComponent(entity, ECS_ENTITY_ID(SystemComponent)));
			if (sysComponent == nullptr)
				return;

			SystemAction action = sysComponent->action;
			ECS_ASSERT(action != nullptr);
			ECS_ASSERT(sysComponent->query != nullptr);
			ECS_ASSERT(sysComponent->invoker != nullptr);

			Iterator iter = GetQueryIterator(sysComponent->query);
			iter.invoker = sysComponent->invoker;
			while (NextQueryIter(&iter))
				action(&iter);
		}

	public:

		////////////////////////////////////////////////////////////////////////////////
		//// Iterator
		////////////////////////////////////////////////////////////////////////////////

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

		////////////////////////////////////////////////////////////////////////////////
		//// Term
		////////////////////////////////////////////////////////////////////////////////

		bool IsTermInited(const Term& term)const
		{
			return term.compID != 0 || term.pred != 0;
		}

		bool FinalizeTermID(Term& term)const
		{
			// Calculate the final compID 
			EntityID pred = term.pred;
			EntityID obj = term.obj;
			EntityID role = term.role;
			if (ECS_HAS_ROLE(pred, EcsRolePair))
			{
				ECS_ASSERT(term.obj != INVALID_ENTITY);
				pred = ECS_GET_PAIR_FIRST(pred);
				obj = ECS_GET_PAIR_SECOND(pred);

				term.pred = pred;
				term.obj = obj;
			}

			if (obj == INVALID_ENTITY && role != EcsRolePair)
			{
				term.compID = pred | role;
			}
			else
			{
				if (obj != INVALID_ENTITY)
				{
					term.compID = (EcsRolePair | ECS_ENTITY_COMBO(obj, pred));
					term.role = EcsRolePair;
				}
				else
				{
					term.compID = pred;
					term.role = 0;
				}
			}

			return true;
		}

		bool PopulateFromTermID(Term& term)const
		{
			EntityID pred = 0;
			EntityID obj = 0;
			EntityID role = term.compID & ECS_ROLE_MASK;

			if (!role && term.role)
			{
				role = term.role;
				term.compID |= role;
			}

			if (term.role && term.role != role)
			{
				ECS_ERROR("Missing role between term.id and term.role");
				return false;
			}

			term.role = role;
		
			if (ECS_HAS_ROLE(term.compID, EcsRolePair))
			{
				pred = ECS_GET_PAIR_FIRST(term.compID);
				obj = ECS_GET_PAIR_SECOND(term.compID);

				if (!pred)
				{
					ECS_ERROR("Missing pred of component id");
					return false;
				}

				if (!obj)
				{
					ECS_ERROR("Missing obj of component id");
					return false;
				}
			}
			else
			{
				pred = term.compID & ECS_COMPONENT_MASK;
				if (!pred)
				{
					ECS_ERROR("Missing pred of component id");
					return false;
				}
			}

			term.pred = pred;
			term.obj = obj;
			return true;
		}

		bool FinalizeTerm(Term& term)const
		{
			if (term.compID == INVALID_ENTITY)
			{
				if (!FinalizeTermID(term))
					return false;
			}
			else
			{
				if (!PopulateFromTermID(term))
					return false;
			}
			return true;
		}

		void InitTermIterNoData(TermIterator& iter)
		{
			iter.term = {};
			iter.term.index = -1;
			iter.current = nullptr;
		}

		void InitTermIter(Term& term, TermIterator& iter, bool emptyTables)
		{
			iter.term = term;
			iter.index = 0;
			iter.current = GetComponentRecord(term.compID);

			if (iter.current)
			{
				// Empty tables
				if (emptyTables)
				{
					iter.tableCacheIter.cur = nullptr;
					iter.tableCacheIter.next = iter.current->cache.emptyTables.first;
					emptyTables = iter.tableCacheIter.next != nullptr;
					if (emptyTables)
						iter.emptyTables = true;
				}

				// Non-empty tables
				if (!emptyTables)
				{
					iter.tableCacheIter.cur = nullptr;
					iter.tableCacheIter.next = iter.current->cache.tables.first;
				}
			}
			else
			{
				InitTermIterNoData(iter);
			}
		}

		bool SetTermIterator(TermIterator* iter, EntityTable* table)
		{
			TableComponentRecord* tr = nullptr;
			ComponentRecord* cr = iter->current;
			if (cr != nullptr)
			{
				tr = GetTableRecordFromCache(&cr->cache, table);
				if (tr != nullptr)
				{
					iter->matchCount = tr->data.count;
					iter->column = tr->data.column;
					iter->id = table->type[tr->data.column];
				}
			}

			if (tr == nullptr)
				return false;

			iter->table = table;
			iter->curMatch = 0;
			return true;
		}

		bool TermIteratorNext(TermIterator* termIter)
		{
			auto GetNextTable = [&](TermIterator* termIter)->TableComponentRecord*
			{
				if (termIter->current == nullptr)
					return nullptr;

				EntityTableCacheItem* term = nullptr;
				term = GetTableCacheListIterNext(termIter->tableCacheIter);
				if (term == nullptr)
				{
					if (termIter->emptyTables)
					{
						termIter->emptyTables = false;
						termIter->tableCacheIter.cur = nullptr;
						termIter->tableCacheIter.next = termIter->current->cache.tables.first;

						term = GetTableCacheListIterNext(termIter->tableCacheIter);
					}
				}

				return (TableComponentRecord*)term;
			};

			TableComponentRecord* tableRecord = nullptr;
			EntityTable* table = termIter->table;
			do
			{
				if (table != nullptr)
				{
					termIter->curMatch++;
					if (termIter->curMatch >= termIter->matchCount)
					{
						table = nullptr;
					}
					else
					{
						// TODO
						ECS_ASSERT(0);
					}
				}

				if (table == nullptr)
				{
					tableRecord = GetNextTable(termIter);
					if (tableRecord == nullptr)
						return false;

					EntityTable* table = tableRecord->table;
					if (table == nullptr)
						return false;

					if (table->flags & TableFlagIsPrefab)
						continue;

					termIter->table = table;
					termIter->curMatch = 0;
					termIter->matchCount = tableRecord->data.count;
					termIter->column = tableRecord->data.column;
					termIter->id = table->type[termIter->column];
					break;
				}
			} while (true);

			return true;
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Filter
		////////////////////////////////////////////////////////////////////////////////
		
		bool InitFilter(const FilterCreateDesc& desc, Filter& outFilter)const override
		{
			Filter filter;
			I32 termCount = 0;
			for (int i = 0; i < MAX_QUERY_ITEM_COUNT; i++)
			{
				if (!IsTermInited(desc.terms[i]))
					break;

				termCount++;
			}

			filter.terms = nullptr;
			filter.termCount = termCount;

			// Copy terms from the desc
			if (termCount > 0)
			{
				Term* terms = (Term*)desc.terms;
				if (termCount <= QUERY_ITEM_SMALL_CACHE_SIZE)
				{
					terms = filter.termSmallCache;
					filter.useSmallCache = true;
				}
				else
				{
					terms = ECS_MALLOC_T_N(Term, termCount);
				}

				if (terms != desc.terms)
					memcpy(terms, desc.terms, sizeof(Term) * termCount);

				filter.terms = terms;
			}

			if (!FinalizeFilter(filter))
			{
				FiniFilter(filter);
				return false;
			}

			outFilter = filter;
			if (outFilter.useSmallCache)
				outFilter.terms = outFilter.termSmallCache;
			outFilter.iterable.init = InitFilterIter;

			return true;
		}

		bool FinalizeFilter(Filter& filter)const
		{
			ECS_BIT_SET(filter.flags, FilterFlagMatchThis);
			ECS_BIT_SET(filter.flags, FilterFlagIsFilter);

			for (int i = 0; i < filter.termCount; i++)
			{
				Term& term = filter.terms[i];

				// Query term compID
				if (!FinalizeTerm(term))
					return false;

				term.index = i;

				// Query term flags
				if (term.set.flags & TermFlagParent)
					term.set.relation = EcsRelationChildOf;
			}
			return true;
		}

		void FiniFilter(Filter& filter)const
		{
			if (filter.terms != nullptr)
			{
				if (!filter.useSmallCache)
					ECS_FREE(filter.terms);
				filter.terms = nullptr;
			}
		}

		Iterator GetFilterIterator(Filter& filter)
		{
			FlushPendingTables();

			Iterator iter = {};
			iter.world = this;
			iter.terms = filter.terms;
			iter.termCount = filter.termCount;
			iter.next = NextFilterIter;

			FilterIterator& filterIter = iter.priv.iter.filter;
			filterIter.pivotTerm = -1;
			filterIter.filter = filter;

			if (filter.useSmallCache)
				filterIter.filter.terms = filterIter.filter.termSmallCache;

			bool valid = FinalizeFilter(filterIter.filter);
			ECS_ASSERT(valid == true);

			// Find the pivot term with the smallest number of table
			auto GetPivotItem = [&](Iterator& iter)->I32
			{
				I32 pivotItem = -1;
				I32 minTableCount = -1;
				for (int i = 0; i < iter.termCount; i++)
				{
					Term& term = iter.terms[i];
					EntityID compID = term.compID;

					ComponentRecord* compRecord = GetComponentRecord(compID);
					if (compRecord == nullptr)
						return -2;

					I32 tableCount = compRecord->cache.GetTableCount();
					if (minTableCount == -1 || tableCount < minTableCount)
					{
						pivotItem = i;
						minTableCount = tableCount;
					}
				}
				return pivotItem;
			};
			filterIter.pivotTerm = GetPivotItem(iter);
			if (filterIter.pivotTerm == -2)
				InitTermIterNoData(filterIter.termIter);
			else
				InitTermIter(filter.terms[filterIter.pivotTerm], filterIter.termIter, true);
		
			// Finally init iterator
			InitIterator(iter, ITERATOR_CACHE_MASK_ALL);

			if (ECS_BIT_IS_SET(filter.flags, FilterFlagIsFilter))
			{
				// Make space for one variable if the filter has terms for This var 
				iter.variableCount = 1;
			}

			return iter;
		}

		bool FilterIteratorNext(Iterator* it)const override
		{
			return NextFilterIter(it);
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Query
		////////////////////////////////////////////////////////////////////////////////

		static void QueryNotifyTrigger(Iterator* it)
		{
			WorldImpl* world = (WorldImpl*)it->world;
			Observer* observer = (Observer*)it->ctx;

			// Check if this event is already handled
			if (observer->eventID == world->eventID)
				return;
			observer->eventID = world->eventID;			

			QueryImpl* query = (QueryImpl*)observer->ctx;
			ECS_ASSERT(query != nullptr);
			ECS_ASSERT(it->table != nullptr);

			// Check the table is matching for query
			if (world->GetTableRecordFromCache(&query->cache, it->table) == nullptr)
				return;

			if (it->event == EcsEventTableFill)
				world->UpdateQueryTableMatch(query, it->table, false);
			else if (it->event == EcsEventTableEmpty)
				world->UpdateQueryTableMatch(query, it->table, true);
		}

		QueryImpl* CreateQuery(const QueryCreateDesc& desc) override
		{
			ECS_ASSERT(isFini == false);

			QueryImpl* ret = queryPool.Requset();
			ECS_ASSERT(ret != nullptr);
			ret->queryID = queryPool.GetLastID();

			// Init query filter
			if (!InitFilter(desc.filter, ret->filter))
				goto error;

			ret->iterable.init = InitQueryIter;
			ret->prevMatchingCount = -1;

			// Create event observer
			if (ret->filter.termCount > 0)
			{
				ObserverDesc observerDesc = {};
				observerDesc.callback = QueryNotifyTrigger;
				observerDesc.events[0] = EcsEventTableEmpty;
				observerDesc.events[1] = EcsEventTableFill;
				observerDesc.filterDesc = desc.filter;
				observerDesc.ctx = ret;

				ret->observer = CreateObserver(observerDesc);
				if (ret->observer == INVALID_ENTITY)
					goto error;
			}

			// Process query flags
			ProcessQueryFlags(ret);

			// Group before matching
			if (ret->sortByItemIndex > 0)
			{
				ret->groupByID = ret->filter.terms[ret->sortByItemIndex - 1].compID;
				ret->groupByItem = &ret->filter.terms[ret->sortByItemIndex - 1];
			}

			// Match exsiting tables and add into cache if query cached
			MatchTables(ret);

			return ret;

		error:
			if (ret != nullptr)
				FiniQuery(ret);
			return nullptr;
		}

		void DestroyQuery(QueryImpl* query) override
		{
			if (query != nullptr)
				FiniQuery(query);
		}

		void UpdateQueryTableMatch(QueryImpl* query, EntityTable* table, bool isEmpty)
		{
			I32 prevCount = query->cache.GetTableCount();
			query->cache.SetTableCacheState(table, isEmpty);
			I32 curCount = query->cache.GetTableCount();
			
			// The count of matching table is changed, need to update the tableList
			if (prevCount != curCount)
			{
				QueryTableCache* qt = (QueryTableCache*)query->cache.GetTableCache(table);
				ECS_ASSERT(qt != nullptr);

				QueryTableMatch* cur, *next;
				for (cur = qt->data.first; cur != nullptr; cur = next)
				{
					next = cur->nextMatch;
				
					if (isEmpty)
						QueryRemoveTableMatchNode(query, cur);
					else
						QueryInsertTableMatchNode(query, cur);
				}
			}
		}

		QueryTableMatch* QueryCreateTableMatchNode(QueryTableCache* cache)
		{
			QueryTableMatch* tableMatch = ECS_CALLOC_T(QueryTableMatch);

			ECS_ASSERT(tableMatch);
			if (cache->data.first == nullptr)
			{
				cache->data.first = tableMatch;
				cache->data.last = tableMatch;
			}
			else
			{
				cache->data.last->next = tableMatch;
				cache->data.last = tableMatch;
			}
			return tableMatch;
		}

		// Find the insertion node of the group which has the closest groupID
		QueryTableMatch* QueryFindGroupInsertionNode(QueryImpl* query, U64 groupID)
		{
			ECS_ASSERT(query->groupByID != INVALID_ENTITY);
			
			QueryTableMatchList* closedList = nullptr;
			U64 closedGroupID = 0;
			for (auto& kvp : query->groups)
			{
				U64 curGroupID = kvp.first;
				if (curGroupID >= groupID)
					continue;

				QueryTableMatchList& list = kvp.second;
				if (list.last == nullptr)
					continue;

				if (closedList == nullptr || (groupID - curGroupID) < (groupID - closedGroupID))
				{
					closedList = &list;
					closedGroupID = curGroupID;
				}
			}
			return closedList != nullptr ? closedList->last->Cast() : nullptr;
		}

		// Create group for the matched table node and inster into the query list
		void QueryCreateGroup(QueryImpl* query, QueryTableMatch* node)
		{
			// Find the insertion point for current group
			QueryTableMatch* insertPoint = QueryFindGroupInsertionNode(query, node->groupID);
			if (insertPoint == nullptr)
			{
				// No insertion point, just insert into the orderdTableList
				QueryTableMatchList& list = query->tableList;
				if (list.first)
				{
					node->next = list.first;
					list.first->prev = node;
					list.first = node;
				}
				else
				{
					list.first = node;
					list.last = node;
				}
			}
			else
			{
				Util::ListNode<QueryTableMatch>* next = insertPoint->next;
				node->prev = insertPoint;
				insertPoint->next = node;
				node->next = next;
				if (next)
					next->prev = node;
				else 
					query->tableList.last = node;
			}
		}

		// Compute group id by cascade
		// Traverse the hierarchy of an component type by parent relation (Parent-Children)
		// to get depth, return the depth as group id
		U64 ComputeGroupIDByCascade(QueryImpl* query, QueryTableMatch* node)
		{
			I32 depth = 0;
			if (TableSearchRelationLast(
				node->table,
				query->groupByID,
				query->groupByItem->set.relation,
				0, 0,
				&depth) != -1)
				return (U64)depth;
			else
				return 0;
		}

		// Compute group id
		U64 ComputeGroupID(QueryImpl* query, QueryTableMatch* node)
		{
			// TODO: add group type
			return ComputeGroupIDByCascade(query, node);
		}

		void QueryInsertTableMatchNode(QueryImpl* query, QueryTableMatch* node)
		{
			ECS_ASSERT(node->prev == nullptr && node->next == nullptr);

			// Compute group id
			bool groupByID = query->groupByID != INVALID_ENTITY;
			if (groupByID)
				node->groupID = ComputeGroupID(query, node);
			else
				node->groupID = 0;

			// Get target match list
			QueryTableMatchList* list = nullptr;
			if (groupByID)
				list = &query->groups[node->groupID];
			else
				list = &query->tableList;

			// Insert into list
			if (list->last)
			{
				// Insert at the end of list if list is init
				Util::ListNode<QueryTableMatch>* last = list->last;
				Util::ListNode<QueryTableMatch>* lastNext = last->next;
				node->prev = last;
				node->next = lastNext;
				last->next = node;
				if (lastNext != nullptr)
					lastNext->prev = node;
				list->last = node;
				
				if (groupByID)
				{
					// If group by id, is need to update the default match list
					if (query->tableList.last == last)
						query->tableList.last = node;
				}
			}
			else
			{
				list->first = node;
				list->last = node;

				if (groupByID)
				{
					// If the table needs to be grouped, manage the group list, 
					// and the nodes of group list are sorted according to the GroupID
					QueryCreateGroup(query, node);
				}
			}

			if (groupByID)
				query->tableList.count++;

			list->count++;
			query->matchingCount++;
		}

		void QueryRemoveTableMatchNode(QueryImpl* query, QueryTableMatch* node)
		{
			Util::ListNode<QueryTableMatch>* next = node->next;
			Util::ListNode<QueryTableMatch>* prev = node->prev;
			if (prev)
				prev->next = next;
			if (next)
				next->prev = prev;

			QueryTableMatchList& list = query->tableList;
			ECS_ASSERT(list.count > 0);
			list.count--;

			if (list.first == node)
				list.first = next;
			if (list.last == node)
				list.last = prev;

			node->prev = nullptr;
			node->next = nullptr;

			query->matchingCount--;
		}

		QueryTableMatch* QueryAddTableMatch(QueryImpl* query, QueryTableCache* qt, EntityTable* table)
		{
			U32 termCount = query->filter.termCount;

			QueryTableMatch* qm = QueryCreateTableMatchNode(qt);
			ECS_ASSERT(qm);
			qm->table = table;
			qm->termCount = termCount;
			qm->ids = ECS_CALLOC_T_N(EntityID, termCount);
			qm->columns = ECS_CALLOC_T_N(I32, termCount);
			qm->sizes = ECS_CALLOC_T_N(size_t, termCount);

			if (table != nullptr && GetTableCount(table) != 0)
				QueryInsertTableMatchNode(query, qm);

			return qm;
		}

		// Match target table for query
		bool MatchTable(QueryImpl* query, EntityTable* table)
		{
			Filter& filter = query->filter;
			I32 varID = -1;
			if (ECS_BIT_IS_SET(filter.flags, FilterFlagMatchThis))
				varID = 0;

			if (varID == -1)
				return false;

			Iterator it = GetFilterIterator(query->filter);
			ECS_BIT_SET(it.flags, IteratorFlagIsInstanced);
			ECS_BIT_SET(it.flags, IteratorFlagIsFilter);

			TableRange range = {};
			range.table = table;
			SetIteratorVar(it, varID, range);

			QueryTableCache* queryTable = nullptr;
			while (NextFilterIter(&it))
			{
				ECS_ASSERT(it.table == table);
				if (queryTable == nullptr)
				{
					queryTable = ECS_CALLOC_T(QueryTableCache);
					query->cache.InsertTableIntoCache(it.table, queryTable);
					table = it.table;
				}

				QueryTableMatch* qm = QueryAddTableMatch(query, queryTable, table);
				QuerySetTableMatch(query, qm, table, it);
			}
			return queryTable != nullptr;
		}

		void UnmatchTable(QueryImpl* query, EntityTable* table)
		{
			QueryTableCache* qt = (QueryTableCache*)query->cache.RemoveTableFromCache(table);
			if (qt != nullptr)
				QueryFreeTableCache(query, qt);
		}

		// Match exsiting tables for query
		void MatchTables(QueryImpl* query)
		{
			if (query->filter.termCount <= 0)
				return;

			Iterator it = GetFilterIterator(query->filter);
			ECS_BIT_SET(it.flags, IteratorFlagIsFilter); // Just match metadata only
			ECS_BIT_SET(it.flags, IteratorFlagIsInstanced);

			QueryTableCache* queryTable = nullptr;
			EntityTable* table = nullptr;
			while (NextFilterIter(&it))
			{
				if (table != it.table || (table != nullptr && queryTable == nullptr))
				{
					queryTable = ECS_CALLOC_T(QueryTableCache);
					query->cache.InsertTableIntoCache(it.table, queryTable);
					table = it.table;
				}

				QueryTableMatch* qm = QueryAddTableMatch(query, queryTable, table);
				QuerySetTableMatch(query, qm, table, it);
			}
		}

		void QuerySetTableMatch(QueryImpl* query, QueryTableMatch* qm, EntityTable* table, Iterator& it)
		{
			I32 termCount = query->filter.termCount;
			memcpy(qm->columns, it.columns, sizeof(I32) * termCount);
			memcpy(qm->ids, it.ids, sizeof(EntityID) * termCount);
			memcpy(qm->sizes, it.sizes, sizeof(size_t) * termCount);
		}

		void ProcessQueryFlags(QueryImpl* query)
		{
			for (int i = 0; i < query->filter.termCount; i++)
			{
				Term& queryItem = query->filter.terms[i];
				if (queryItem.set.flags & TermFlagCascade)
				{
					ECS_ASSERT(query->sortByItemIndex == 0);
					query->sortByItemIndex = i + 1;
				}
			}
		}

		void QueryFreeTableCache(QueryImpl*query, QueryTableCache* queryTable)
		{
			QueryTableMatch* cur, *next;
			for (cur = queryTable->data.first; cur != nullptr; cur = next)
			{
				QueryTableMatch* match = cur->Cast();
				ECS_FREE(match->ids);
				ECS_FREE(match->columns);
				ECS_FREE(match->sizes);

				// Remove non-emtpy table
				if (!queryTable->empty)
					QueryRemoveTableMatchNode(query, match);

				next = cur->nextMatch;
				ECS_FREE(cur);
			}
			ECS_FREE(queryTable);
		}

		void FiniQuery(QueryImpl* query)
		{
			if (query == nullptr)
				return;

			// Delete the observer
			if (!isFini)
			{
				if (query->observer != INVALID_ENTITY)
					DeleteEntity(query->observer);
			}

			// TODO: reinterpret_cast 
			// Free query table cache 
			QueryTableCache* cache = nullptr;
			EntityTableCacheIterator iter = GetTableCacheListIter(&query->cache, false);
			while (cache = (QueryTableCache*)GetTableCacheListIterNext(iter))
				QueryFreeTableCache(query, cache);

			iter = GetTableCacheListIter(&query->cache, true);
			while (cache = (QueryTableCache*)GetTableCacheListIterNext(iter))
				QueryFreeTableCache(query, cache);

			queryPool.Remove(query->queryID);
		}

		void FiniQueries()
		{
			size_t queryCount = queryPool.Count();
			for (size_t i = 0; i < queryCount; i++)
			{
				QueryImpl* query = queryPool.Get(i);
				FiniQuery(query);
			}
		}

		Iterator GetQueryIterator(QueryImpl* query) override
		{
			ECS_ASSERT(query != nullptr);

			FlushPendingTables();

			query->prevMatchingCount = query->matchingCount;

			QueryIterator queryIt = {};
			queryIt.query = query;
			queryIt.node = (QueryTableMatch*)query->tableList.first;

			Iterator iter = {};
			iter.world = this;
			iter.terms = query->filter.terms;
			iter.termCount = query->filter.termCount;
			iter.tableCount = query->cache.GetTableCount();
			iter.priv.iter.query = queryIt;
			iter.next = NextQueryIter;

			Filter& filter = query->filter;
			I32 termCount = filter.termCount;
			Iterator fit = GetFilterIterator(filter);
			if (!NextFilterIter(&fit))
			{
				FiniIterator(fit);
				goto noresults;
			}

			InitIterator(iter, ITERATOR_CACHE_MASK_ALL);

			// Copy data from fit
			if (termCount > 0)
			{
				memcpy(iter.columns, fit.columns, sizeof(I32) * termCount);
				memcpy(iter.ids, fit.ids, sizeof(EntityID) * termCount);
				memcpy(iter.sizes, fit.sizes, sizeof(size_t) * termCount);
				memcpy(iter.ptrs, fit.ptrs, sizeof(void*) * termCount);
			}
			FiniIterator(fit);
			return iter;

		noresults:
			Iterator ret = {};
			ret.flags = IteratorFlagNoResult;
			ret.next = NextQueryIter;
			return ret;
		}

		void NotifyQuery(QueryImpl* query, const QueryEvent& ent)
		{
			switch (ent.type)
			{
			case QueryEventType::MatchTable:
				MatchTable(query, ent.table);
				break;
			case QueryEventType::UnmatchTable:
				UnmatchTable(query, ent.table);
				break;
			}
		}

		void NotifyQueriss(const QueryEvent& ent)
		{
			for (int i = 1; i < queryPool.Count(); i++)
			{
				QueryImpl* query = queryPool.GetByDense(i);
				if (query == nullptr)
					continue;

				NotifyQuery(query, ent);
			}
		}
			
		////////////////////////////////////////////////////////////////////////////////
		//// Entity
		////////////////////////////////////////////////////////////////////////////////

		// Entity methods
		EntityID CreateEntityID(const EntityCreateDesc& desc)
		{
			const char* name = desc.name;
			bool isNewEntity = false;
			bool nameAssigned = false;
			EntityID result = desc.entity;
			if (result == INVALID_ENTITY)
			{
				if (name != nullptr)
				{
					result = FindEntityIDByName(name);
					if (result != INVALID_ENTITY)
						nameAssigned = true;
				}

				if (result == INVALID_ENTITY)
				{
					if (desc.useComponentID)
						result = CreateNewComponentID();
					else
						result = CreateNewEntityID();

					isNewEntity = true;
				}
			}

			if (!EntityTraverseAdd(result, desc, nameAssigned, isNewEntity))
				return INVALID_ENTITY;

			return result;
		}

		EntityID CreateNewEntityID()
		{
			return entityPool.NewIndex();
		}

		bool EntityTraverseAdd(EntityID entity, const EntityCreateDesc& desc, bool nameAssigned, bool isNewEntity)
		{
			EntityTable* srcTable = nullptr, *table = nullptr;
			EntityInfo* info = nullptr;

			// Get existing table
			if (!isNewEntity)
			{
				info = entityPool.Get(entity);
				if (info != nullptr)
					table = info->table;
			}

			EntityTableDiff diff = EMPTY_TABLE_DIFF;

			// Add name component
			const char* name = desc.name;
			if (name && !nameAssigned)
				table = TableAppend(table, ECS_ENTITY_ID(NameComponent), diff);

			// Commit entity table
			if (srcTable != table)
			{
				CommitTables(entity, info, table, diff, true);
			}

			if (name && !nameAssigned)
			{
				SetEntityName(entity, name);
				entityNameMap[Util::HashFunc(name, strlen(name))] = entity;
			}

			return true;
		}

		ComponentRecord* CreateComponentRecord(EntityID compID)
		{
			ComponentRecord* ret = ECS_NEW_OBJECT<ComponentRecord>();
			if (ECS_HAS_ROLE(compID, EcsRolePair))
			{
				EntityID rel = ECS_GET_PAIR_FIRST(compID);
				ECS_ASSERT(rel != 0);

				EntityID obj = ECS_GET_PAIR_SECOND(compID);
				if (obj != INVALID_ENTITY)
				{
					obj = GetAliveEntity(obj);
					ECS_ASSERT(obj != INVALID_ENTITY);
				}
			}
			return ret;
		}

		bool FreeComponentRecord(ComponentRecord* record)
		{
			// There are still tables in non-empty list
			if (record->cache.GetTableCount() > 0)
				return false;

			// No more tables in this record, free it
			if (record->cache.GetEmptyTableCount() == 0)
			{
				ECS_DELETE_OBJECT(record);
				return true;
			}
		
			// Release empty tables
			EntityTableCacheIterator cacheIter = GetTableCacheListIter(&record->cache, true);
			TableComponentRecord* tableRecord = nullptr;
			while (tableRecord = (TableComponentRecord*)(GetTableCacheListIterNext(cacheIter)))
			{
				if (!tableRecord->table->Release())
					return false;
			}

			return true;
		}
		
		ComponentRecord* EnsureComponentRecord(EntityID compID)
		{
			auto it = compRecordMap.find(StripGeneration(compID));
			if (it != compRecordMap.end())
				return it->second;

			ComponentRecord* ret = CreateComponentRecord(compID);
			compRecordMap[StripGeneration(compID)] = ret;
			return ret;
		}

		void RemoveComponentRecord(EntityID id, ComponentRecord* compRecord)
		{
			if (FreeComponentRecord(compRecord))
				compRecordMap.erase(StripGeneration(id));
		}

		void FiniComponentRecords()
		{
			for (auto kvp : compRecordMap)
				FreeComponentRecord(kvp.second);
			
			compRecordMap.clear();
		}

		void FiniComponentTypeInfo(ComponentTypeInfo* typeinfo)
		{
			ComponentTypeHooks& hooks = typeinfo->hooks;
			if (hooks.invoker != nullptr && hooks.invokerDeleter != nullptr)
				hooks.invokerDeleter(hooks.invoker);
		}

		void FiniComponentTypeInfos()
		{
			size_t count = compTypePool.Count();
			for (size_t i = 0; i < count; i++)
			{
				ComponentTypeInfo* typeinfo = compTypePool.GetByDense(i);
				if (typeinfo != nullptr)
					FiniComponentTypeInfo(typeinfo);
			}
		}

		bool CheckEntityTypeHasComponent(EntityType& entityType, EntityID compID)
		{
			for (auto& id : entityType)
			{
				if (id == compID)
					return true;
			}
			return false;
		}

		bool MergeEntityType(EntityType& entityType, EntityID compID)
		{
			for (auto it = entityType.begin(); it != entityType.end(); it++)
			{
				EntityID id = *it;
				if (id == compID)
					return false;

				if (id > compID)
				{
					entityType.insert(it, compID);
					return true;
				}
			}
			entityType.push_back(compID);
			return true;
		}

		void RemoveFromEntityType(EntityType& entityType, EntityID compID)
		{
			if (CheckIDHasPropertyNone(compID))
			{
				ECS_ASSERT(0);
				return;
			}

			auto it = std::find(entityType.begin(), entityType.end(), compID);
			if (it != entityType.end())
				entityType.erase(it);
		}

		ComponentRecord* GetComponentRecord(EntityID id)
		{
			auto it = compRecordMap.find(StripGeneration(id));
			if (it == compRecordMap.end())
				return nullptr;
			return it->second;
		}

		// Get alive entity, is essentially used for PariEntityID
		EntityID GetAliveEntity(EntityID entity)
		{
			// if entity has generation, just check is alived
			// if entity does not have generation, get a new entity with generation

			if (entity == INVALID_ENTITY)
				return INVALID_ENTITY;

			if (IsEntityAlive(entity))
				return entity;

			// Make sure id does not have generation
			ECS_ASSERT((U32)entity == entity);

			// Get current alived entity with generation
			EntityID current = entityPool.GetAliveIndex(entity);
			if (current == INVALID_ENTITY)
				return INVALID_ENTITY;

			return current;
		}

		// Check id is PropertyNone
		bool CheckIDHasPropertyNone(EntityID id)
		{
			return (id == EcsPropertyNone) || (ECS_HAS_ROLE(id, EcsRolePair)
				&& (ECS_GET_PAIR_FIRST(id) == EcsPropertyNone || 
					ECS_GET_PAIR_SECOND(id) == EcsPropertyNone));
		}

		bool IsCompIDValid(EntityID id)
		{
			if (id == INVALID_ENTITY)
				return false;
			
			if (CheckIDHasPropertyNone(id))
				return false;

			if (ECS_HAS_ROLE(id, EcsRolePair))
			{
				if (!ECS_GET_PAIR_FIRST(id))
					return false;

				if (!ECS_GET_PAIR_SECOND(id))
					return false;
			}
			return true;
		}

		bool IsCompIDTag(EntityID id)
		{
			if (CheckIDHasPropertyNone(id))
			{
				if (ECS_HAS_ROLE(id, EcsRolePair))
				{
					if (ECS_GET_PAIR_FIRST(id) != EcsPropertyNone)
					{
						EntityID rel = ECS_GET_PAIR_FIRST(id);
						if (IsEntityValid(rel))
						{
							if (HasComponent(rel, EcsPropertyTag))
								return true;
						}
						else
						{
							ComponentTypeInfo* info = GetComponentTypeInfo(id);
							if (info != nullptr)
								return info->compID == INVALID_ENTITY;
							return true;
						}
					}
				}
			}
			else
			{
				ComponentTypeInfo* info = GetComponentTypeInfo(id);
				if (info != nullptr)
					return info->compID == INVALID_ENTITY;
				return true;
			}

			return false;
		}

		// Get a single real type id
		EntityID GetRealTypeID(EntityID compID)
		{
			if (compID == ECS_ENTITY_ID(InfoComponent) ||
				compID == ECS_ENTITY_ID(NameComponent))
				return compID;
		
			if (ECS_HAS_ROLE(compID, EcsRolePair))
			{
				EntityID relation = ECS_GET_PAIR_FIRST(compID);
				if (relation == EcsRelationChildOf)
					return 0;
				
				relation = GetAliveEntity(relation);

				// Tag dose not have type info, return zero
				if (HasComponent(relation, EcsPropertyTag))
					return INVALID_ENTITY;

				InfoComponent* info = GetComponentInfo(relation);
				if (info && info->size != 0)
					return relation;

				EntityID object = ECS_GET_PAIR_SECOND(compID);
				if (object != INVALID_ENTITY)
				{
					object = GetAliveEntity(object);
					info = GetComponentInfo(object);
					if (info && info->size != 0)
						return object;
				}

				// No matching relation
				return 0;
			}
			else if (compID & ECS_ROLE_MASK)
			{
				return 0;
			}
			else
			{
				InfoComponent* info = GetComponentInfo(compID);
				if (!info || info->size == 0)
					return 0;
			}

			return compID;
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Component
		////////////////////////////////////////////////////////////////////////////////

		// Create component record for table
		bool RegisterComponentRecord(EntityTable* table, EntityID compID, I32 column, I32 count, TableComponentRecord& tableRecord)
		{
			// Register component and init component type info
			ComponentRecord* compRecord = EnsureComponentRecord(compID);
			ECS_ASSERT(compRecord != nullptr);
			compRecord->cache.InsertTableIntoCache(table, &tableRecord);

			// Init component type info
			if (!compRecord->typeInfoInited)
			{
				EntityID type = GetRealTypeID(compID);
				if (type != INVALID_ENTITY)
				{
					compRecord->typeInfo = GetComponentTypeInfo(type);
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

		template<typename C>
		void InitBuiltinComponentTypeInfo(EntityID id)
		{
			ComponentTypeInfo* typeInfo = EnsureComponentTypInfo(id);
			typeInfo->size = sizeof(C);
			typeInfo->alignment = alignof(C);
		}

		void SetupComponentTypes()
		{
			InitBuiltinComponentTypeInfo<InfoComponent>(ECS_ENTITY_ID(InfoComponent));
			InitBuiltinComponentTypeInfo<NameComponent>(ECS_ENTITY_ID(NameComponent));
			InitBuiltinComponentTypeInfo<SystemComponent>(ECS_ENTITY_ID(SystemComponent));
			InitBuiltinComponentTypeInfo<TriggerComponent>(ECS_ENTITY_ID(TriggerComponent));
			InitBuiltinComponentTypeInfo<ObserverComponent>(ECS_ENTITY_ID(ObserverComponent));

			// Info component
			ComponentTypeHooks info = {};
			info.ctor = DefaultCtor;
			SetComponentTypeInfo(ECS_ENTITY_ID(InfoComponent), info);

			// Name component
			info.ctor = Reflect::Ctor<NameComponent>();
			info.dtor = Reflect::Dtor<NameComponent>();
			info.copy = Reflect::Copy<NameComponent>();
			info.move = Reflect::Move<NameComponent>();
			SetComponentTypeInfo(ECS_ENTITY_ID(NameComponent), info);

			// Trigger component
			info = {};
			info.ctor = DefaultCtor;
			info.dtor = BuiltinCompDtor(TriggerComponent);
			SetComponentTypeInfo(ECS_ENTITY_ID(TriggerComponent), info);

			// Observer component
			info = {};
			info.ctor = DefaultCtor;
			info.dtor = BuiltinCompDtor(ObserverComponent);
			SetComponentTypeInfo(ECS_ENTITY_ID(ObserverComponent), info);
		}

		void InitBuiltinComponents()
		{
			// Create builtin table for builtin components
			EntityTable* table = nullptr;
			{
				Vector<EntityID> compIDs = {
					ECS_ENTITY_ID(InfoComponent),
					ECS_ENTITY_ID(NameComponent)
				};
				table = FindOrCreateTableWithIDs(compIDs);

				table->entities.reserve(FirstUserComponentID);
				table->storageColumns[0].Reserve<InfoComponent>(FirstUserComponentID);
				table->storageColumns[1].Reserve<NameComponent>(FirstUserComponentID);
			}

			// Initialize builtin components
			auto InitBuiltinComponent = [&](EntityID compID, U32 size, U32 alignment, const char* compName) {
				EntityInfo* entityInfo = entityPool.Ensure(compID);
				entityInfo->table = table;

				U32 index = table->AppendNewEntity(compID, entityInfo, false);
				entityInfo->row = index;

				// Component info
				InfoComponent* componentInfo = table->storageColumns[0].Get<InfoComponent>(index);
				componentInfo->size = size;
				componentInfo->algnment = alignment;

				// Name component
				NameComponent* nameComponent = table->storageColumns[1].Get<NameComponent>(index);
				nameComponent->name = _strdup(compName);
				nameComponent->hash = Util::HashFunc(compName, strlen(compName));

				entityNameMap[nameComponent->hash] = compID;
			};

			InitBuiltinComponent(ECS_ENTITY_ID(InfoComponent), sizeof(InfoComponent), alignof(InfoComponent), Util::Typename<InfoComponent>());
			InitBuiltinComponent(ECS_ENTITY_ID(NameComponent), sizeof(NameComponent), alignof(NameComponent), Util::Typename<NameComponent>());
			InitBuiltinComponent(ECS_ENTITY_ID(TriggerComponent), sizeof(TriggerComponent), alignof(TriggerComponent), Util::Typename<TriggerComponent>());
			InitBuiltinComponent(ECS_ENTITY_ID(ObserverComponent), sizeof(ObserverComponent), alignof(ObserverComponent), Util::Typename<ObserverComponent>());


			lastComponentID = FirstUserComponentID;
			lastID = FirstUserEntityID;
		}

		void InitBuiltinEntites()
		{
			InfoComponent tagInfo = {};
			tagInfo.size = 0;

			// TEMP:
			// Ensure EcsPropertyNone
			entityPool.Ensure(EcsPropertyNone);

			auto InitTag = [&](EntityID tagID, const char* name)
			{
				EntityInfo* entityInfo = entityPool.Ensure(tagID);
				ECS_ASSERT(entityInfo != nullptr);

				SetComponent(tagID, ECS_ENTITY_ID(InfoComponent), sizeof(InfoComponent), &tagInfo, false);
				SetEntityName(tagID, name);
			};
			// Property
			InitTag(EcsPropertyTag, "EcsPropertyTag");
			// Tags
			InitTag(EcsTagPrefab, "EcsTagPrefab");
			// Relation
			InitTag(EcsRelationIsA, "EcsRelationIsA");
			InitTag(EcsRelationChildOf, "EcsRelationChildOf");
			// Events
			InitTag(EcsEventTableEmpty, "EcsEventTableEmpty");
			InitTag(EcsEventTableFill, "EcsEventTableFill");
			InitTag(EcsEventOnAdd, "EcsEventOnAdd");
			InitTag(EcsEventOnRemove, "EcsEventOnRemove");
			

			// RelationIsA has peropty of tag
			AddComponent(EcsRelationIsA, EcsPropertyTag);
			AddComponent(EcsRelationChildOf, EcsPropertyTag);
		}

		void InitSystemComponent()
		{
			// System is a special builtin component, it build in a independent table.
			ComponentCreateDesc desc = {};
			desc.entity.entity = ECS_ENTITY_ID(SystemComponent);
			desc.entity.name = Util::Typename<SystemComponent>();
			desc.entity.useComponentID = true;
			desc.size = sizeof(SystemComponent);
			desc.alignment = alignof(SystemComponent);
			ECS_ENTITY_ID(SystemComponent) = InitNewComponent(desc);

			// Set system component action
			ComponentTypeHooks info = {};
			info.ctor = DefaultCtor;
			info.dtor = [](World* world, EntityID* entities, size_t size, size_t count, void* ptr) {
				WorldImpl* worldImpl = static_cast<WorldImpl*>(world);
				SystemComponent* sysArr = static_cast<SystemComponent*>(ptr);
				for (size_t i = 0; i < count; i++)
				{
					SystemComponent& sys = sysArr[i];
					if (sys.invoker != nullptr && sys.invokerDeleter != nullptr)
						sys.invokerDeleter(sys.invoker);

					if (sys.query != nullptr)
						worldImpl->FiniQuery(sys.query);
				}
			};
			SetComponentTypeInfo(ECS_ENTITY_ID(SystemComponent), info);
		}

		EntityID CreateNewComponentID()
		{
			EntityID ret = INVALID_ENTITY;
			if (lastComponentID < HiComponentID)
			{
				do {
					ret = lastComponentID++;
				} while (EntityExists(ret) != INVALID_ENTITY && ret <= HiComponentID);
			}

			if (ret == INVALID_ENTITY || ret >= HiComponentID)
				ret = CreateNewEntityID();

			return ret;
		}

		void* GetComponentFromTable(EntityTable& table, I32 row, EntityID compID)
		{
			ECS_ASSERT(compID != 0);
			ECS_ASSERT(row >= 0);

			if (table.storageTable == nullptr)
				return nullptr;

			TableComponentRecord* tableRecord = GetTableRecord(table.storageTable, compID);
			if (tableRecord == nullptr)
				return nullptr;

			return GetComponentPtrFromTable(table, row, tableRecord->data.column);
		}

		void* GetComponentPtrFromTable(EntityTable& table, I32 row, I32 column)
		{
			ECS_ASSERT(column < (I32)table.storageCount);
			ComponentColumnData& columnData = table.storageColumns[column];
			ComponentTypeInfo& typeInfo = table.compTypeInfos[column];
			ECS_ASSERT(typeInfo.size != 0);
			return columnData.Get(typeInfo.size, typeInfo.alignment, row);
		}

		void* GetOrCreateMutableByID(EntityID entity, EntityID compID, bool* added)
		{
			EntityInfo* info = entityPool.Ensure(entity);
			void* ret = GetOrCreateMutable(entity, compID, info, added);
			ECS_ASSERT(ret != nullptr);
			return ret;
		}

		void* GetOrCreateMutable(EntityID entity, EntityID compID, EntityInfo* info, bool* isAdded)
		{
			ECS_ASSERT(compID != 0);
			ECS_ASSERT(info != nullptr);
			ECS_ASSERT((compID & ECS_COMPONENT_MASK) == compID || ECS_HAS_ROLE(compID, EcsRolePair));

			void* ret = nullptr;
			if (info->table != nullptr)
				ret = GetComponentFromTable(*info->table, info->row, compID);

			if (ret == nullptr)
			{
				AddComponentForEntity(entity, info, compID);

				ECS_ASSERT(info != nullptr);
				ECS_ASSERT(info->table != nullptr);
				ret = GetComponentFromTable(*info->table, info->row, compID);

				if (isAdded != nullptr)
					*isAdded = true;
			}
			else
			{
				if (isAdded != nullptr)
					*isAdded = false;
			}

			return ret;
		}

		InfoComponent* GetComponentInfo(EntityID compID)
		{
			return static_cast<InfoComponent*>(GetComponent(compID, ECS_ENTITY_ID(InfoComponent)));
		}

		void SetComponent(EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove)
		{
			EntityInfo* info = entityPool.Ensure(entity);
			void* dst = GetOrCreateMutable(entity, compID, info, NULL);
			ECS_ASSERT(dst != NULL);
			if (ptr)
			{
				ComponentTypeInfo* compTypeInfo = GetComponentTypeInfo(compID);
				if (compTypeInfo != nullptr)
				{
					if (isMove)
					{
						if (compTypeInfo->hooks.move != nullptr)
							compTypeInfo->hooks.move(this, &entity, &entity, compTypeInfo->size, 1, (void*)ptr, dst);
						else
							memcpy(dst, ptr, size);
					}
					else
					{
						if (compTypeInfo->hooks.copy != nullptr)
							compTypeInfo->hooks.copy(this, &entity, &entity, compTypeInfo->size, 1, ptr, dst);
						else
							memcpy(dst, ptr, size);
					}
				}
				else
				{
					memcpy(dst, ptr, size);
				}
			}
			else
			{
				memset(dst, 0, size);
			}
		}

		void AddComponentForEntity(EntityID entity, EntityInfo* info, EntityID compID)
		{
			EntityTableDiff diff = {};
			EntityTable* srcTable = info->table;
			EntityTable* dstTable = TableTraverseAdd(srcTable, compID, diff);
			CommitTables(entity, info, dstTable, diff, true);
		}

		void CtorComponent(ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			ECS_ASSERT(columnData != nullptr);

			if (typeInfo != nullptr && typeInfo->hooks.ctor != nullptr)
			{
				void* mem = columnData->Get(typeInfo->size, typeInfo->alignment, row);
				typeInfo->hooks.ctor(this, entities, typeInfo->size, count, mem);
			}
		}

		void DtorComponent(ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			ECS_ASSERT(columnData != nullptr);

			if (typeInfo != nullptr && typeInfo->hooks.dtor != nullptr)
			{
				void* mem = columnData->Get(typeInfo->size, typeInfo->alignment, row);
				typeInfo->hooks.dtor(this, entities, typeInfo->size, count, mem);
			}
		}

		void AddNewComponent(EntityTable* table, ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			ECS_ASSERT(typeInfo != nullptr);

			CtorComponent(typeInfo, columnData, entities, compID, row, count);

			auto onAdd = typeInfo->hooks.onAdd;
			if (onAdd != nullptr)
				OnComponentCallback(table, typeInfo, onAdd, columnData, entities, compID, row, count);
		}

		void RemoveComponent(EntityTable* table, ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			ECS_ASSERT(typeInfo != nullptr);

			auto onRemove = typeInfo->hooks.onRemove;
			if (onRemove != nullptr)
				OnComponentCallback(table, typeInfo, onRemove, columnData, entities, compID, row, count);
		
			DtorComponent(typeInfo, columnData, entities, compID, row, count);
		}

		void OnComponentCallback(EntityTable* table, ComponentTypeInfo* typeInfo, IterCallbackAction callback, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			Iterator it = {};
			it.termCount = 1;
			it.entities = entities;

			// term count < ECS_TERM_CACHE_SIZE
			InitIterator(it, ITERATOR_CACHE_MASK_ALL);
			it.world = this;
			it.table = table;
			it.ptrs[0] = columnData->Get(typeInfo->size, typeInfo->alignment, row);
			it.sizes[0] = typeInfo->size;
			it.ids[0] = compID;
			it.count = count;
			it.invoker = typeInfo->hooks.invoker;
			ValidateInteratorCache(it);
			callback(&it);
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Table
		////////////////////////////////////////////////////////////////////////////////
		EntityTable* CreateNewTable(EntityType entityType)
		{
			EntityTable* ret = tablePool.Requset();
			ECS_ASSERT(ret != nullptr);
			ret->tableID = tablePool.GetLastID();
			ret->type = entityType;
			if (!ret->InitTable(this))
			{
				ECS_ASSERT(0);
				return nullptr;
			}

			tableTypeHashMap[EntityTypeHash(entityType)] = ret;

			// Queries need to rematch tables
			QueryEvent ent = {};
			ent.type = QueryEventType::MatchTable;
			ent.table = ret;
			NotifyQueriss(ent);

			return ret;
		}

		EntityTable* GetTable(EntityID entity)
		{
			EntityInfo* info = entityPool.Get(entity);
			if (info == nullptr)
				return nullptr;

			return info->table;
		}

		I32 GetTableCount(EntityTable* table)
		{
			ECS_ASSERT(table != nullptr);
			return (I32)table->entities.size();
		}

		// Search target component id from a table
		// Return column of component if compID exists, otherwise return -1
		I32 TableSearchType(EntityTable* table, EntityID compID)
		{
			if (table == nullptr)
				return -1;

			TableComponentRecord* record = GetTableRecord(table, compID);
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
			if (minDepth <= 0)
			{
				I32 ret = TableSearchType(table, compRecord);
				if (ret != -1)
					return ret;
			}

			// Check table flags
			if (!(table->flags & TableFlagHasRelation) || relation == INVALID_ENTITY)
				return -1;

			// Relation must is a pair (relation, None)
			ComponentRecord* relationRecord = GetComponentRecord(relation);
			if (relationRecord == nullptr)
				return -1;

			I32 column = TableSearchType(table, relationRecord);
			if (column != -1)
			{
				EntityID obj = ECS_GET_PAIR_SECOND(table->type[column]);
				ECS_ASSERT(obj != INVALID_ENTITY);
				EntityInfo* objInfo = entityPool.Get(obj);
				ECS_ASSERT(objInfo != nullptr);

				I32 objColumn = TypeSearchRelation(objInfo->table, compID, relation, compRecord, minDepth - 1, maxDepth - 1, objOut, depthOut);
				if (objColumn != -1)
				{
					if (objOut != nullptr)
						*objOut = GetAliveEntity(obj);
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

			ComponentRecord* record = GetComponentRecord(compID);
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

			// Find the column of target compID
			I32 depth = 0;
			EntityID obj = INVALID_ENTITY;
			I32 column = TableSearchRelation(table, compID, relation, 0, 0, &obj, &depth);
			if (column == -1)
				return -1;

			// Get obj of relation
			if (obj == INVALID_ENTITY)
			{
				if (TableSearchRelation(table, compID, relation, 1, 0, &obj, &depth) == -1)
					return column;
			}

			while (true)
			{
				EntityTable* curTable = GetTable(obj);
				ECS_ASSERT(curTable != nullptr);
				I32 curDepth = 0;
				EntityID curObj = INVALID_ENTITY;
				if (TableSearchRelation(curTable, compID, relation, 1, 0, &curObj, &curDepth) == -1)
					break;

				depth += curDepth;
				obj = curObj;
			}

			if (depthOut != nullptr)
				*depthOut = depth;

			return column;
		}

		EntityTable* FindOrCreateTableWithIDs(const Vector<EntityID>& compIDs)
		{
			auto it = tableTypeHashMap.find(EntityTypeHash(compIDs));
			if (it != tableTypeHashMap.end())
				return it->second;

			return CreateNewTable(compIDs);
		}

		EntityTable* FindOrCreateTableWithPrefab(EntityTable* table, EntityID prefab)
		{
			if (table->flags & TableFlagIsPrefab)
				return table;

			EntityTable* prefabTable = GetTable(prefab);
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
				table = TableTraverseAdd(table, compID & ECS_COMPONENT_MASK, diff);
			}

			return table;
		}

		EntityTable* FindOrCreateTableWithID(EntityTable* parent, EntityID compID, TableGraphEdge* edge)
		{
			EntityType entityType = parent->type;
			if (!MergeEntityType(entityType, compID))
				return parent;

			if (entityType.empty())
				return &root;

			// Find exsiting tables
			auto it = tableTypeHashMap.find(EntityTypeHash(entityType));
			if (it != tableTypeHashMap.end())
				return it->second;

			EntityTable* newTable = CreateNewTable(entityType);
			ECS_ASSERT(newTable);

			// Create from prefab if comp has relation of isA
			if (ECS_HAS_ROLE(compID, EcsRolePair) && ECS_GET_PAIR_FIRST(compID) == EcsRelationIsA)
			{
				EntityID prefab = ECS_GET_PAIR_SECOND(compID);
				newTable = FindOrCreateTableWithPrefab(newTable, prefab);
			}

			// Connect parent with new table 
			InitAddTableGraphEdge(edge, compID, parent, newTable);

			return newTable;
		}

		EntityTable* TableAppend(EntityTable* table, EntityID compID, EntityTableDiff& diff)
		{
			EntityTableDiff tempDiff = {};
			EntityTable* ret = TableTraverseAdd(table, compID, diff);
			ECS_ASSERT(ret != nullptr);
			AppendTableDiff(diff, tempDiff);
			return ret;
		}

		EntityTable* TableTraverseAdd(EntityTable* table, EntityID compID, EntityTableDiff& diff)
		{
			EntityTable* node = table != nullptr ? table : &root;
			TableGraphEdge* edge = EnsureTableGraphEdge(node->graphNode.add, compID);
			EntityTable* ret = edge->to;
			if (ret == nullptr)
			{
				ret = FindOrCreateTableWithID(node, compID, edge);
				ECS_ASSERT(ret != nullptr);
			}

			PopulateTableDiff(edge, compID, INVALID_ENTITY, diff);
			return ret;
		}

		EntityTable* TableTraverseRemove(EntityTable* table, EntityID compID, EntityTableDiff& diff)
		{
			EntityTable* node = table != nullptr ? table : &root;
			TableGraphEdge* edge = EnsureTableGraphEdge(node->graphNode.remove, compID);
			EntityTable* ret = edge->to;
			if (ret == nullptr)
			{
				ret = FindOrCreateTableWithoutID(node, compID, edge);
				ECS_ASSERT(ret != nullptr);
			}

			PopulateTableDiff(edge, compID, INVALID_ENTITY, diff);
			return ret;
		}

		EntityTable* FindOrCreateTableWithoutID(EntityTable* parent, EntityID compID, TableGraphEdge* edge)
		{
			EntityType entityType = parent->type;
			RemoveFromEntityType(entityType, compID);
			EntityTable* ret = FindOrCreateTableWithIDs(entityType);
			InitRemoveTableGraphEdge(edge, compID, parent, ret);
			return ret;
		}

		void SetTableEmpty(EntityTable* table)
		{
			EntityTable** tablePtr = pendingTables->Ensure(table->tableID);
			ECS_ASSERT(tablePtr != nullptr);
			(*tablePtr) = table;
		}

		TableComponentRecord* GetTableRecord(EntityTable* table, EntityID compID)
		{
			ComponentRecord* compRecord = GetComponentRecord(compID);
			if (compRecord == nullptr)
				return nullptr;

			return GetTableRecordFromCache(&compRecord->cache, table);
		}
		
		I32 MoveTableEntity(EntityID entity, EntityInfo* entityInfo, EntityTable* srcTable, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
		{
			ECS_ASSERT(entityInfo != nullptr);
			ECS_ASSERT(entityInfo == entityPool.Get(entity));
			ECS_ASSERT(IsEntityAlive(entity));

			U32 srcRow = entityInfo->row;
			ECS_ASSERT(srcRow >= 0);

			// Add a new entity for dstTable (Just reserve storage)
			U32 newRow = dstTable->AppendNewEntity(entity, entityInfo, false);
			ECS_ASSERT(srcTable->entities.size() > entityInfo->row);

			// Move comp datas from src table to new table of entity
			if (!srcTable->type.empty())
				MoveTableEntityImpl(entity, srcTable, entityInfo->row, entity, dstTable, newRow, construct);

			entityInfo->row = newRow;
			entityInfo->table = dstTable;

			// Remove old entity from src table
			srcTable->DeleteEntity(srcRow, false);

			return newRow;
		}

		void CommitTables(EntityID entity, EntityInfo* info, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
		{
			EntityTable* srcTable = info != nullptr ? info->table : nullptr;
			ECS_ASSERT(dstTable != nullptr);
			if (srcTable != nullptr)
			{
				if (!dstTable->type.empty()) {
					MoveTableEntity(entity, info, srcTable, dstTable, diff, construct);
				}
				else {
					srcTable->DeleteEntity(info->row, true);
					info->table = nullptr;
				}
			}
			else
			{
				if (!dstTable->type.empty())
					info = TableNewEntityImpl(entity, info, dstTable, construct);
			}
		}

		EntityInfo* TableNewEntityImpl(EntityID entity, EntityInfo* entityInfo, EntityTable* table, bool construct)
		{
			if (entityInfo == nullptr)
				entityInfo = entityPool.Ensure(entity);

			U32 newRow = table->AppendNewEntity(entity, entityInfo, construct);		
			entityInfo->row = newRow;
			entityInfo->table = table;
			return entityInfo;
		}

		void MoveTableEntityImpl(EntityID srcEntity, EntityTable* srcTable, I32 srcRow, EntityID dstEntity, EntityTable* dstTable, I32 dstRow, bool construct)
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
							moveCtor(this, &srcEntity, &srcEntity, typeInfo.size, 1, srcMem, dstMem);
							dtor(this, &srcEntity, typeInfo.size, 1, srcMem);
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
							typeInfo.hooks.copyCtor(this, &srcEntity, &dstEntity, typeInfo.size, 1, srcMem, dstMem);
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
					srcTable,
					&srcTable->compTypeInfos[srcColumnIndex],
					&srcTable->storageColumns[srcColumnIndex],
					&srcEntity,
					srcTable->storageIDs[srcColumnIndex],
					srcRow,
					1);
		}

		void FlushPendingTables()
		{
			if (isReadonly)
			{
				ECS_ASSERT(pendingTables->Count() == 0);
				return;
			}

			// Pending table is iterating when pending buffer is null.
			if (pendingBuffer == nullptr)
				return;

			size_t pendingCount = pendingTables->Count();
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
				Util::SparseArray<EntityTable*>* tables = pendingTables;	
				pendingTables = pendingBuffer;
				pendingBuffer = nullptr;

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
						desc.observable = &observable;
						desc.table = table;
						EmitEvent(desc);
					}
				}

				tables->Clear();
				pendingBuffer = tables;

			} while (pendingCount = pendingTables->Count());
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

		void InitAddTableGraphEdge(TableGraphEdge* edge, EntityID compID, EntityTable* from, EntityTable* to)
		{
			edge->from = from;
			edge->to = to;
			edge->compID = compID;

			EnsureHiTableGraphEdge(from->graphNode.add, compID);

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

		void InitRemoveTableGraphEdge(TableGraphEdge* edge, EntityID compID, EntityTable* from, EntityTable* to)
		{
			edge->from = from;
			edge->to = to;
			edge->compID = compID;

			EnsureHiTableGraphEdge(from->graphNode.remove, compID);

			if (from != to)
			{
				// Remove edges are appended to incomingEdges->prev
				Util::ListNode<TableGraphEdge>* toNode = &to->graphNode.incomingEdges;
				Util::ListNode<TableGraphEdge>* prev = toNode->next;
				toNode->prev = edge;
				edge->next = toNode;
				edge->prev = prev;

				if (prev != nullptr)
					prev->next = edge;

				// Compute table diff (Call PopulateTableDiff to get all diffs)
				ComputeTableDiff(from, to, edge, compID);
			}
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
				if (addID != INVALID_ENTITY)
					outDiff.added.push_back(addID);

				if (removeID != INVALID_ENTITY)
					outDiff.removed.push_back(removeID);
			}
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
		//// Table graph
		////////////////////////////////////////////////////////////////////////////////

		TableGraphEdge* RequestTableGraphEdge()
		{
			TableGraphEdge* ret = freeEdge;
			if (ret != nullptr)
				freeEdge = (TableGraphEdge*)ret->next;
			else
				ret = ECS_MALLOC_T(TableGraphEdge);

			ECS_ASSERT(ret != nullptr);
			memset(ret, 0, sizeof(TableGraphEdge));
			return ret;
		}

		void FreeTableGraphEdge(TableGraphEdge* edge)
		{
			edge->next =(Util::ListNode<TableGraphEdge>*)freeEdge;
			freeEdge = edge;
		}

		TableGraphEdge* EnsureHiTableGraphEdge(TableGraphEdges& edges, EntityID compID)
		{
			auto it = edges.hiEdges.find(compID);
			if (it != edges.hiEdges.end())
				return it->second;

			TableGraphEdge* edge = nullptr;
			if (compID < HiComponentID)
				edge = &edges.loEdges[compID];
			else
				edge = RequestTableGraphEdge();

			edges.hiEdges[compID] = edge;
			return edge;
		}

		TableGraphEdge* EnsureTableGraphEdge(TableGraphEdges& edges, EntityID compID)
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
					edge = EnsureHiTableGraphEdge(edges, compID);
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

		void ClearTableGraphEdges(EntityTable* table)
		{
			TableGraphNode& graphNode = table->graphNode;

			// Remove outgoing edges
			for (auto& kvp : graphNode.add.hiEdges)
				DisconnectEdge(kvp.second, kvp.first);
			for (auto& kvp : graphNode.remove.hiEdges)
				DisconnectEdge(kvp.second, kvp.first);

			// Remove incoming edges
			// 1. Add edges are appended to incomingEdges->Next
			Util::ListNode<TableGraphEdge>* cur, *next = graphNode.incomingEdges.next;
			while ((cur = next))
			{
				next = cur->next;

				TableGraphEdge* edge = (TableGraphEdge*)cur;
				DisconnectEdge(edge, edge->compID);
				if (edge->from != nullptr)
					edge->from->graphNode.add.hiEdges.erase(edge->compID);
			}

			// 2. Remove edges are appended to incomingEdges->prev
			Util::ListNode<TableGraphEdge>* prev = graphNode.incomingEdges.prev;
			while ((cur = prev))
			{
				prev = cur->prev;

				TableGraphEdge* edge = (TableGraphEdge*)cur;
				DisconnectEdge(edge, edge->compID);
				if (edge->from != nullptr)
					edge->from->graphNode.remove.hiEdges.erase(edge->compID);
			}

			graphNode.add.hiEdges.clear();
			graphNode.remove.hiEdges.clear();
		}

		void DisconnectEdge(TableGraphEdge* edge, EntityID compID)
		{
			ECS_ASSERT(edge != nullptr);
			ECS_ASSERT(edge->compID == compID);

			// TODO: is valid?
			if (edge->from == nullptr)
				return;

			// Remove node from list of Edges
			Util::ListNode<TableGraphEdge>* prev = edge->prev;
			Util::ListNode<TableGraphEdge>* next = edge->next;
			if (prev)
				prev->next = next;
			if (next)
				next->prev = prev;

			// Free table diff
			EntityTableDiff* diff = edge->diff;
			if (diff != nullptr && diff != &EMPTY_TABLE_DIFF)
				ECS_DELETE_OBJECT(diff);

			edge->to = nullptr;

			// Component use small cache array when compID < HiComponentID
			if (compID > HiComponentID)
				FreeTableGraphEdge(edge);
			else
				edge->from = nullptr;
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Trigger
		////////////////////////////////////////////////////////////////////////////////

		const Map<EventRecord>* GetTriggers(Observable* observable, EntityID event)
		{
			EventRecords* records = observable->events.Get(event);
			if (records != nullptr)
				return &records->eventIds;
			return nullptr;
		}

		void NotifyTriggers(Iterator& it, const Map<Trigger*>* triggers)
		{
			ECS_ASSERT(triggers != nullptr);

			auto IsTriggerValid = [&](const Trigger& trigger, EntityTable* table) {
				if (trigger.eventID && *trigger.eventID == eventID)
					return false;

				if (table == nullptr)
					return false;

				if (table->flags & TableFlagIsPrefab)
					return false;

				return true;
			};

			for (const auto& kvp : *triggers)
			{
				Trigger* trigger = (Trigger*)(kvp.second);
				if (!IsTriggerValid(*trigger, it.table))
					continue;

				it.terms = &trigger->term;
				it.ctx = trigger->ctx;
				trigger->callback(&it);
			}
		}

		void NotifyTriggersForID(Iterator& it, const Map<EventRecord>* eventMap, EntityID id)
		{
			auto kvp = eventMap->find(id);
			if (kvp == eventMap->end())
				return;

			const EventRecord& record = kvp->second;
			if (record.triggers.size() > 0)
				NotifyTriggers(it, &record.triggers);
		}

		void RegisterTriggerForID(Observable& observable, Trigger* trigger, EntityID id)
		{
			// EventID -> EventRecords -> CompID -> EventRecord -> TriggerID
			ECS_ASSERT(trigger != nullptr);

			for (int i = 0; i < trigger->eventCount; i++)
			{
				EntityID event = trigger->events[i];
				ECS_ASSERT(event != INVALID_ENTITY);

				EventRecords* records = observable.events.Ensure(event);
				EventRecord& record = records->eventIds[id];
				record.triggers[trigger->id] = trigger;
				record.triggerCount++;
			}
		}

		void RegisterTrigger(Observable& observable, Trigger* trigger)
		{
			Term& term = trigger->term;
			RegisterTriggerForID(observable, trigger, term.compID);
		}

		void UnregisterTriggerForID(Observable& observable, Trigger* trigger, EntityID id)
		{
			// EventID -> EventRecords -> CompID -> EventRecord -> TriggerID
			ECS_ASSERT(trigger != nullptr);

			for (int i = 0; i < trigger->eventCount; i++)
			{
				EntityID event = trigger->events[i];
				ECS_ASSERT(event != INVALID_ENTITY);

				EventRecords* records = observable.events.Get(event);
				if (records == nullptr)
					continue;

				auto it = records->eventIds.find(id);
				if (it == records->eventIds.end())
					continue;

				EventRecord& record = it->second;
				if (record.triggers.find(trigger->id) != record.triggers.end())
				{
					record.triggers.erase(trigger->id);
					record.triggerCount--;
				}
			}
		}

		void UnregisterTrigger(Observable& observable, Trigger* trigger)
		{
			Term& term = trigger->term;
			UnregisterTriggerForID(observable, trigger, term.compID);
		}

		EntityID CreateTrigger(const TriggerDesc& desc)
		{
			ECS_ASSERT(isFini == false);
			ECS_ASSERT(desc.callback != nullptr);

			Observable* observable = desc.observable;
			if (observable == nullptr)
				observable = &this->observable;

			EntityID ret = CreateEntityID(nullptr);
			bool newAdded = false;
			TriggerComponent* comp = static_cast<TriggerComponent*>(GetOrCreateMutableByID(ret, ECS_ENTITY_ID(TriggerComponent), &newAdded));
			if (newAdded)
			{				
				Term term = desc.term;
				if (!FinalizeTerm(term))
					goto error;

				Trigger* trigger = triggers.Requset();
				ECS_ASSERT(trigger != nullptr);
				trigger->id = triggers.GetLastID();
				comp->trigger = trigger;

				trigger->entity = ret;
				trigger->term = term;
				trigger->callback = desc.callback;	
				trigger->ctx = desc.ctx;
				memcpy(trigger->events, desc.events, sizeof(EntityID) * desc.eventCount);
				trigger->eventCount = desc.eventCount;
				trigger->eventID = desc.eventID;
				trigger->observable = observable;
			
				RegisterTrigger(*observable, trigger);
			}
			return ret;
		error:
			if (ret != INVALID_ENTITY)
				DeleteEntity(ret);
			return INVALID_ENTITY;
		}

		void FiniTrigger(Trigger* trigger)
		{
			UnregisterTrigger(*trigger->observable, trigger);
			triggers.Remove(trigger->id);
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Observer
		////////////////////////////////////////////////////////////////////////////////

		static void ObserverTriggerCallback(Iterator* it) 
		{
			Observer* observer = (Observer*)it->ctx;
			if (observer->callback)
				observer->callback(it);
		}

		EntityID CreateObserver(const ObserverDesc& desc)
		{
			ECS_ASSERT(isFini == false);
			ECS_ASSERT(desc.callback != nullptr);

			// ObserverComp => Observer => Trigger for each term

			EntityID ret = CreateEntityID(nullptr);
			bool newAdded = false;
			ObserverComponent* comp = static_cast<ObserverComponent*>(GetOrCreateMutableByID(ret, ECS_ENTITY_ID(ObserverComponent), &newAdded));
			if (newAdded)
			{
				Observer* observer = observers.Requset();
				ECS_ASSERT(observer != nullptr);
				observer->id = observers.GetLastID();
				comp->observer = observer;

				for (int i = 0; i < ECS_TRIGGER_MAX_EVENT_COUNT; i++)
				{
					if (desc.events[i] == INVALID_ENTITY)
						continue;

					observer->events[observer->eventCount] = desc.events[i];
					observer->eventCount++;
				}

				ECS_ASSERT(observer->eventCount > 0);

				observer->callback = desc.callback;
				observer->ctx = desc.ctx;

				// Init the filter of observer
				if (!InitFilter(desc.filterDesc, observer->filter))
				{
					FiniObserver(observer);
					return INVALID_ENTITY;
				}

				// Create a trigger for each term
				TriggerDesc triggerDesc = {};
				triggerDesc.callback = ObserverTriggerCallback;
				triggerDesc.ctx = observer;
				triggerDesc.eventID = &observer->eventID;
				memcpy(triggerDesc.events, observer->events, sizeof(EntityID) * observer->eventCount);
				triggerDesc.eventCount = observer->eventCount;

				const Filter& filter = observer->filter;
				for (int i = 0; i < filter.termCount; i++)
				{
					triggerDesc.term = filter.terms[i];
					/*if (IsCompIDTag(triggerDesc.term.compID))
						ECS_ASSERT(false);*/

					EntityID trigger = CreateTrigger(triggerDesc);
					if (trigger == INVALID_ENTITY)
						goto error;

					observer->triggers.push_back(trigger);
				}
			}
			return ret;

		error:
			if (ret != INVALID_ENTITY)
				DeleteEntity(ret);
			return INVALID_ENTITY;
		}

		void FiniObserver(Observer* observer)
		{
			for (auto trigger : observer->triggers)
			{
				if (trigger != INVALID_ENTITY)
					DeleteEntity(trigger);
			}
			observer->triggers.clear();

			FiniFilter(observer->filter);

			observers.Remove(observer->id);
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Events
		////////////////////////////////////////////////////////////////////////////////

		void NotifyEvents(Observable* observable, Iterator& it, const EntityType& ids, EntityID event)
		{
			ECS_ASSERT(event != INVALID_ENTITY);
			ECS_ASSERT(!ids.empty());

			const Map<EventRecord>* eventMap = GetTriggers(observable, event);
			if (eventMap == nullptr)
				return;

			for (int i = 0; i < ids.size(); i++)
			{
				EntityID id = ids[i];
				NotifyTriggersForID(it, eventMap, id);
			}
		}

		void EmitEvent(const EventDesc& desc)
		{
			ECS_ASSERT(desc.event != INVALID_ENTITY);
			ECS_ASSERT(!desc.ids.empty());
			ECS_ASSERT(desc.table != nullptr);

			Iterator it = {};
			it.world = this;
			it.table = desc.table;
			it.termCount = 1;
			it.count = desc.table->Count();
			it.event = desc.event;

			// Inc unique event id
			eventID++;

			Observable* observable = desc.observable;
			ECS_ASSERT(observable != nullptr);
			NotifyEvents(observable, it, desc.ids, desc.event);
		}
	};
	
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
			world->EnsureEntity(id);

		// Init table flags
		InitTableFlags();

		//  Register table records
		RegisterTableComponentRecords();

		// Init storage table
		InitStorageTable();

		// Init type infos
		InitTypeInfos();

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
			world->NotifyQueriss(ent);
		}

		// Fini data
		FiniData(true, true);
		
		// Clear all graph edges
		world->ClearTableGraphEdges(this);

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
						world->DtorComponent(
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
						ECS_ASSERT(entity != INVALID_ENTITY);
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
					ECS_ASSERT(entity != INVALID_ENTITY);
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

		// Pending empty table
		 if (count == 0)
			world->SetTableEmpty(this);

		if (index == count)
		{
			// Destruct the last data of column
			if (destruct && ECS_HAS_FLAG(flags, TableFlagHasDtors))
			{
				for (int i = 0; i < storageCount; i++)
				{
					world->RemoveComponent(
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
						world->OnComponentCallback(this, &typeInfo, onRemove, &columnData, &entityToDelete, storageIDs[i], index, 1);

					if (typeInfo.hooks.move != nullptr && typeInfo.hooks.dtor != nullptr)
					{
						typeInfo.hooks.move(world, &entityToMove, &entityToDelete, typeInfo.size, 1, srcMem, dstMem);
						typeInfo.hooks.dtor(world, &entityToDelete, typeInfo.size, 1, srcMem);
					}
					else
					{
						memcpy(dstMem, srcMem, typeInfo.size);
					}

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
			compTypeInfo->hooks.ctor(world, &entities[oldCount], compTypeInfo->size, addCount, mem);
	}

	U32 EntityTable::AppendNewEntity(EntityID entity, EntityInfo* info, bool construct)
	{
		U32 count = (U32)entities.size();

		// Add a new entity for table
		entities.push_back(entity);
		entityInfos.push_back(info);

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
			world->SetTableEmpty(this);

		return count;
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
				if (relation != INVALID_ENTITY)
				{
					if (relations.count(relation) == 0)
					{
						relations[relation] = {};
						relations[relation].pos = i;
					}
					relations[relation].count++;
				}

				EntityID obj = ECS_GET_PAIR_SECOND(compId);
				if (obj != INVALID_ENTITY)
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
			world->RegisterComponentRecord(this, type[index], index, 1, tableRecords[index]);
			index++;
		}
		
		// Relations (Record all relations which table used)
		for (auto kvp : relations)
		{
			EntityID type = ECS_MAKE_PAIR(kvp.first, EcsPropertyNone);
			world->RegisterComponentRecord(this, type, kvp.second.pos, kvp.second.count, tableRecords[index]);
			index++;
		}

		// Objects (Record all objects which table used)
		for (auto kvp : objects)
		{
			EntityID type = ECS_MAKE_PAIR(EcsPropertyNone, kvp.first);
			world->RegisterComponentRecord(this, type, kvp.second.pos, kvp.second.count, tableRecords[index]);
			index++;
		}

		// Add default child record if withou childof 
		if (!hasChildOf && type.size() > 0)
			world->RegisterComponentRecord(
				this, ECS_MAKE_PAIR(EcsRelationChildOf, 0), index, index, tableRecords[index]);
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
				world->RemoveComponentRecord(tableRecord->data.compID, compRecord);
			}
		}
		tableRecords.clear();
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

	void EntityTable::InitTableFlags()
	{
		for (U32 i = 0; i < type.size(); i++)
		{
			EntityID compID = type[i];
			if (compID == EcsTagPrefab)
				flags |= TableFlagIsPrefab;

			if (ECS_HAS_ROLE(compID, EcsRolePair))
			{
				U32 relation = ECS_GET_PAIR_FIRST(compID);
				if (relation != INVALID_ENTITY)
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
				storageTable = world->FindOrCreateTableWithIDs(usedCompIDs);
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

	// Get size for target comp id
	size_t IteratorGetSizeForID(WorldImpl* world, EntityID id)
	{
		EntityID typeID = world->GetRealTypeID(id);
		if (typeID == INVALID_ENTITY)
			return 0;

		auto info =world->GetComponentTypeInfo(typeID);
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

	void IteratorPopulateData(WorldImpl* world, Iterator& iter, EntityTable* table, I32 offset, size_t* sizes, void**ptrs)
	{
		iter.table = table;
		iter.count = 0;

		if (table != nullptr)
		{
			iter.count = world->GetTableCount(table);
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

	bool TermMatchTable(WorldImpl* world, EntityTable* table, Term& term, EntityID* outID, I32* outColumn)
	{
		I32 column = world->TableSearchType(table, term.compID);
		if (column == -1)
			return false;

		if (outID)
			*outID = term.compID;
		if (outColumn)
			*outColumn = column;

		return true;
	}

	bool FilterMatchTable(WorldImpl* world, EntityTable* table, Iterator& iter, I32 pivotItem, EntityID* ids, I32* columns)
	{
		// Check current table includes all compsIDs from terms
		for (int i = 0; i < iter.termCount; i++)
		{
			if (i == pivotItem)
				continue;

			if (!TermMatchTable(world, table, iter.terms[i], &ids[i], &columns[i]))
				return false;
		}

		return true;
	}

	bool FilterNextInstanced(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->world != nullptr);
		ECS_ASSERT(it->next == NextFilterIter);
		ECS_ASSERT(it->chainIter != it);

		WorldImpl* world = static_cast<WorldImpl*>(it->world);
		FilterIterator& iter = it->priv.iter.filter;
		Filter& filter = iter.filter;
		Iterator* chain = it->chainIter;
		EntityTable* table = nullptr;

		if (filter.termCount <= 0)
			goto done;

		world->ValidateInteratorCache(*it);

		if (filter.terms == nullptr)
			filter.terms = filter.termSmallCache;

		{
			TermIterator& termIter = iter.termIter;
			Term& term = termIter.term;
			I32 pivotTerm = iter.pivotTerm;
			bool match = false;
			do
			{
				EntityTable* targetTable = nullptr;
				// Process target from variables
				if (it->variableCount > 0)
				{
					if (world->IsIteratorVarConstrained(*it, 0))
					{
						targetTable = it->variables->range.table;
						ECS_ASSERT(targetTable != nullptr);
					}
				}

				// We need to match a new table for current id when matching count equal to zero
				bool first = iter.matchesLeft == 0;
				if (first)
				{
					if (targetTable != nullptr)
					{
						// If it->table equal target table, match failed
						if (targetTable == it->table)
							goto done;

						if (!world->SetTermIterator(&termIter, targetTable))
							goto done;

						ECS_ASSERT(termIter.table == targetTable);
					}
					else
					{
						if (!world->TermIteratorNext(&termIter))
							goto done;
					}

					ECS_ASSERT(termIter.matchCount != 0);

					iter.matchesLeft = termIter.matchCount;
					table = termIter.table;

					if (pivotTerm != -1)
					{
						I32 index = term.index;
						it->ids[index] = termIter.id;
						it->columns[index] = termIter.column;
					}

					match = FilterMatchTable(world, table, *it, pivotTerm, it->ids, it->columns);
					if (match == false)
					{
						it->table = table;
						iter.matchesLeft = 0;
						continue;
					}
				}

				// If this is not the first result for the table, and the table
				// is matched more than once, iterate remaining matches
				if (!first && iter.matchesLeft > 0)
				{
					// TODO...
					ECS_ASSERT(false);
				}

				match = iter.matchesLeft != 0;
				iter.matchesLeft--;
			}
			while (!match);

			goto yield;
		}

	done:
		world->FiniIterator(*it);
		return false;

	yield:
		IteratorPopulateData(world, *it, table, 0, it->sizes, it->ptrs);
		return true;
	}

	void InitFilterIter(World* world, const void* iterable, Iterator* it, Term* filter)
	{
	}

	bool NextFilterIter(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		return FilterNextInstanced(it);
	}

	void InitQueryIter(World* world, const void* iterable, Iterator* it, Term* filter)
	{
	}

	struct QueryIterCursor
	{
		int32_t first;
		int32_t count;
	};
	bool QueryNextInstanced(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		ECS_ASSERT(it->next == NextQueryIter);

		WorldImpl* world = static_cast<WorldImpl*>(it->world);

		if (ECS_BIT_IS_SET(it->flags, IteratorFlagNoResult))
		{
			world->FiniIterator(*it);
			return false;
		}

		ECS_BIT_SET(it->flags, IteratorFlagIsValid);

		QueryIterator* iter = &it->priv.iter.query;
		QueryImpl* query = iter->query;
		
		// Validate interator
		world->ValidateInteratorCache(*it);

		QueryIterCursor cursor;
		QueryTableMatch* node, * next;
		for (node = iter->node; node != nullptr; node = next)
		{
			EntityTable* table = node->table;
			next = (QueryTableMatch*)node->next;

			if (table != nullptr)
			{
				cursor.first = 0;
				cursor.count = world->GetTableCount(table);
				ECS_ASSERT(cursor.count != 0);

				I32 termCount = query->filter.termCount;
				for (int i = 0; i < termCount; i++)
				{
					Term& term = query->filter.terms[i];
					I32 index = term.index;
					it->ids[index] = node->ids[index];
					it->columns[index] = node->columns[index];
					it->sizes[index] = node->sizes[index];
				}
			}
			else
			{
				cursor.first = 0;
				cursor.count = 0;
			}

			IteratorPopulateData(world, *it, table, cursor.first, nullptr, it->ptrs);

			iter->node = next;
			iter->prev = node;
			goto yield;
		}

		world->FiniIterator(*it);
		return false;

	yield:
		return true;
	}

	bool NextQueryIter(Iterator* it)
	{
		ECS_ASSERT(it != nullptr);
		return QueryNextInstanced(it);
	}

	ECS_UNIQUE_PTR<World> World::Create()
	{
		return ECS_MAKE_UNIQUE<WorldImpl>();
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Builtin components
	////////////////////////////////////////////////////////////////////////////////

	static void BuiltinCompDtor(TriggerComponent)(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		WorldImpl* impl = static_cast<WorldImpl*>(world);
		TriggerComponent* comps = static_cast<TriggerComponent*>(ptr);
		for (int i = 0; i < count; i++)
		{
			if (comps[i].trigger != nullptr)
				impl->FiniTrigger(comps[i].trigger);
		}
	}

	static void BuiltinCompDtor(ObserverComponent)(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		WorldImpl* impl = static_cast<WorldImpl*>(world);
		ObserverComponent* comps = static_cast<ObserverComponent*>(ptr);
		for (int i = 0; i < count; i++)
		{
			if (comps[i].observer != nullptr)
				impl->FiniObserver(comps[i].observer);
		}
	}
}