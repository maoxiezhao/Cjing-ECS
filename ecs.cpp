#include "ecs.h"

// TODO
// 1. hierarchy (Complete)
// 1.1 ChildOf (Complete)
// 1.2 (Child-Parent (Complete)
// 
// 2. Query refactor
// 3. Table merage
// 4. Shared component (GetBaseComponent)
// 5. Work pipeline
// 6. Mult threads
// 7. Serialization

namespace ECS
{
	struct WorldImpl;
	struct EntityTable;

	// -----------------------------------------
	//            EntityID: 64                 |
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

	EntityID BuiltinComponentID = HiComponentID;
	#define BUILTIN_COMPONENT_ID (BuiltinComponentID++)

	// properties
	const EntityID EcsPropertyTag = BUILTIN_COMPONENT_ID;
	const EntityID EcsPropertyNone = BUILTIN_COMPONENT_ID;
	// Tags
	const EntityID EcsTagPrefab = BUILTIN_COMPONENT_ID;
	// Relations
	const EntityID EcsRelationIsA = BUILTIN_COMPONENT_ID;
	const EntityID EcsRelationChildOf = BUILTIN_COMPONENT_ID;

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

	static bool FlushTableState(WorldImpl* world, EntityTable* table, EntityID id, I32 column);
	using EntityIDAction = bool(*)(WorldImpl*, EntityTable*, EntityID, I32);
	bool ForEachEntityID(WorldImpl* world, EntityTable* table, EntityIDAction action);

	////////////////////////////////////////////////////////////////////////////////
	//// Entity info
	////////////////////////////////////////////////////////////////////////////////

	struct EntityInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
	};

	struct InternalEntityInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
		EntityInfo* entityInfo = nullptr;
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

	// Dual link list to manage TableRecords
	struct EntityTableCacheItem : Util::ListNode<EntityTableCacheItem>
	{
		struct EntityTableCacheBase* tableCache = nullptr;
		EntityTable* table = nullptr;	// -> Owned table
		bool empty = false;
	};

	struct EntityTableCacheIterator
	{
		Util::ListNode<EntityTableCacheItem>* cur = nullptr;
		Util::ListNode<EntityTableCacheItem>* next = nullptr;
	};

	template<typename T>
	struct EntityTableCacheItemInst : EntityTableCacheItem
	{
		T data;
	};

	struct EntityTableCacheBase
	{
		Hashmap<EntityTableCacheItem*> tableRecordMap; // <TableID, CacheItem>
		Util::List<EntityTableCacheItem> tables;
		Util::List<EntityTableCacheItem> emptyTables;
	};

	template<typename T, typename = int>
	struct EntityTableCache {};

	template<typename T>
	struct EntityTableCache<T, std::enable_if_t<std::is_base_of_v<EntityTableCacheItem, T>, int>> : EntityTableCacheBase
	{
		void InsertTableIntoCache(WorldImpl* world, const EntityTable* table, T* cacheNode);
		T* RemoveTableFromCache(WorldImpl* world, EntityTable* table);
	};

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
		I32 itemCount = 0;
		U64* componentIDs = nullptr;
		I32* columns = nullptr;
		size_t* sizes = nullptr;
		U64 groupID = 0;
	};

	using QueryTableMatchList = Util::List<QueryTableMatch>;

	struct QueryTableCacheData
	{
		QueryTableMatch* first = nullptr;
		QueryTableMatch* last = nullptr;
	};
	using QueryTableCache = EntityTableCacheItemInst<QueryTableCacheData>;

	// TODO: be refactored
	struct QueryImpl
	{
		U64 queryID;
		QueryItem* queryItems;
		Vector<QueryItem> queryItemsCache;
		QueryItem queryItemSmallCache[QUERY_ITEM_SMALL_CACHE_SIZE];
		I32 itemCount;
		I32 sortByItemIndex = 0;
		I32 matchingCount = 0;
		bool cached = false;

		EntityTableCache<QueryTableCache> matchedTableCache; // All matched tables <QueryTableCache>
		QueryTableMatchList orderedTableList; // Non-empty ordered tables

		// Group
		EntityID groupByID = INVALID_ENTITY;
		QueryItem* groupByItem = nullptr;
		Map<QueryTableMatchList> groups;
	};

	struct QueryItemIterator
	{
		QueryItem currentItem;
		ComponentRecord* compRecord;
		EntityTableCacheIterator tableCacheIter;
		EntityTable* table;
		I32 curMatch;
		I32 matchCount;
		I32 column;
	};

	struct QueryMatchIterator
	{
		I32 matchCount;
		Util::ListNode<QueryTableMatch>* curTableMatch;
	};

	const int MAX_QUERY_ITER_IMPL_CACHE_SIZE = 4;

	struct QueryIteratorImplCache
	{
		EntityID ids[MAX_QUERY_ITER_IMPL_CACHE_SIZE];
		I32 columns[MAX_QUERY_ITER_IMPL_CACHE_SIZE];
		bool compIDsAlloc;
		bool columnsAlloc;
	};

	struct QueryIteratorImpl
	{
		QueryItemIterator itemIter;
		QueryMatchIterator matchIter;
		I32 matchingLeft;
		I32 pivotItemIndex;
		QueryImpl* query;

		EntityID* compIDs;
		I32* columns;
		QueryIteratorImplCache cache;
	};

	QueryIterator::QueryIterator()
	{
		impl = ECS_CALLOC_T(QueryIteratorImpl);
		ECS_ASSERT(impl);
	}

	QueryIterator::~QueryIterator()
	{
		if (impl != nullptr)
		{
			if (impl->cache.compIDsAlloc)
				ECS_FREE(impl->compIDs);
			if (impl->cache.columnsAlloc)
				ECS_FREE(impl->columns);

			ECS_FREE(impl);
		}
	}

	QueryIterator::QueryIterator(QueryIterator&& rhs)noexcept
	{
		*this = ECS_MOV(rhs);
	}

	void QueryIterator::operator=(QueryIterator&& rhs)noexcept
	{
		std::swap(world, rhs.world);
		std::swap(items, rhs.items);
		std::swap(itemCount, rhs.itemCount);
		std::swap(invoker, rhs.invoker);
		std::swap(entityCount, rhs.entityCount);
		std::swap(entities, rhs.entities);
		std::swap(compDatas, rhs.compDatas);
		std::swap(impl, rhs.impl);
	}

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

	////////////////////////////////////////////////////////////////////////////////
	//// Builtin components
	////////////////////////////////////////////////////////////////////////////////

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

	EntityID ECS_ENTITY_ID(InfoComponent) = 1;
	EntityID ECS_ENTITY_ID(NameComponent) = 2;
	EntityID ECS_ENTITY_ID(SystemComponent) = 3;

	////////////////////////////////////////////////////////////////////////////////
	//// WorldImpl
	////////////////////////////////////////////////////////////////////////////////

	struct WorldImpl : public World
	{
		EntityBuilder entityBuilder = EntityBuilder(this);
		EntityID lastComponentID = 0;
		EntityID lastID = 0;

		// Entity
		Util::SparseArray<EntityInfo> entityPool;
		Hashmap<EntityID> entityNameMap;

		// Tables
		EntityTable root;
		Util::SparseArray<EntityTable> tablePool;
		Util::SparseArray<EntityTable*> pendingTables;
		Hashmap<EntityTable*> tableTypeHashMap;

		// Component
		Hashmap<ComponentRecord*> compRecordMap;
		Util::SparseArray<ComponentTypeInfo> compTypePool;	// Component reflect type info

		// Graph
		TableGraphEdge* freeEdge = nullptr;

		// Query
		Util::SparseArray<QueryImpl> queryPool;

		bool isFini = false;

		WorldImpl()
		{
			compRecordMap.reserve(HiComponentID);
			entityPool.SetSourceID(&lastID);
			if (!root.InitTable(this))
				ECS_ASSERT(0);

			// Skip id 0
			U64 id = tablePool.NewIndex();
			ECS_ASSERT(id == 0);
			id = queryPool.NewIndex();
			ECS_ASSERT(id == 0);

			SetupComponentIDs();
			InitBuiltinComponents();
			InitBuiltinTags();
			InitSystemComponent();
		}

		~WorldImpl()
		{	
			isFini = true;

			// Free all tables, neet to skip id 0
			size_t tabelCount = tablePool.Count();
			for (size_t i = 1; i < tabelCount; i++)
			{
				EntityTable* table = tablePool.GetByDense(i);
				if (table != nullptr)
					table->Release();
			}
			tablePool.Clear();
			pendingTables.Clear();

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

			// Clear entity pool
			entityPool.Clear();
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

		EntityID IsEntityAlive(EntityID entity)const override
		{
			if (entity == INVALID_ENTITY)
				return INVALID_ENTITY;

			if (entityPool.CheckExsist(entity))
				return entity;

			return false;
		}

		EntityType GetEntityType(EntityID entity)const override
		{
			return EMPTY_ENTITY_TYPE;
		}
			
		void DeleteEntity(EntityID entity) override
		{ 
			ECS_ASSERT(entity != INVALID_ENTITY);
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

		void EnsureEntity(EntityID entity) override
		{
			if (ECS_HAS_ROLE(entity, EcsRolePair))
			{
				EntityID re = ECS_GET_PAIR_FIRST(entity);
				EntityID comp = ECS_GET_PAIR_SECOND(entity);

				if (IsEntityAlive(re) != re)
					entityPool.Ensure(re);

				if (IsEntityAlive(comp) != comp)
					entityPool.Ensure(comp);
			}
			else
			{
				if (IsEntityAlive(StripGeneration(entity)) == entity)
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

		// Set component type info for component id
		void SetComponentTypeInfo(EntityID compID, const Reflect::ReflectInfo& info) override
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

			if (compTypeInfo->isSet)
			{
				ECS_ASSERT(compTypeInfo->reflectInfo.ctor != nullptr);
				ECS_ASSERT(compTypeInfo->reflectInfo.dtor != nullptr);
			}
			else
			{
				compTypeInfo->compID = compID;
				compTypeInfo->size = size;
				compTypeInfo->alignment = alignment;
				compTypeInfo->isSet = true;
				compTypeInfo->reflectInfo = info;
				
				// Set default constructor
				if (!info.ctor && (info.dtor || info.copy || info.move))
					compTypeInfo->reflectInfo.ctor = DefaultCtor;
			}
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
			InternalEntityInfo info = {};
			void* comp = GetOrCreateMutable(entity, compID, &info, &isAdded);
			ECS_ASSERT(comp != nullptr);
			return comp;
		}

		void AddComponent(EntityID entity, EntityID compID) override
		{
			ECS_ASSERT(IsEntityAlive(entity));

			InternalEntityInfo info = {};
			GetInternalEntityInfo(info, entity);

			EntityTableDiff diff = {};
			EntityTable* newTable = TableTraverseAdd(info.table, compID, diff);
			CommitTables(entity, &info, newTable, diff, true);
		}

		void RemoveComponent(EntityID entity, EntityID compID)override
		{
			ECS_ASSERT(IsEntityAlive(entity));

			InternalEntityInfo info = {};
			GetInternalEntityInfo(info, entity);

			EntityTableDiff diff = {};
			EntityTable* newTable = TableTraverseRemove(info.table, compID, diff);
			CommitTables(entity, &info, newTable, diff, true);
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

				QueryImpl* queryInfo = CreateNewQuery(desc.query);
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

			QueryIterator iter = GetQueryIterator(sysComponent->query->queryID);
			iter.invoker = sysComponent->invoker;
			while (QueryIteratorNext(iter))
				action(&iter);
		}

	public:
		////////////////////////////////////////////////////////////////////////////////
		//// Query
		////////////////////////////////////////////////////////////////////////////////

		QueryID CreateQuery(const QueryCreateDesc& desc) override
		{
			QueryImpl* queryInfo = CreateNewQuery(desc);
			if (queryInfo == nullptr)
				return INVALID_ENTITY;

			return queryInfo->queryID;
		}

		void DestroyQuery(QueryID queryID) override
		{
			QueryImpl* queryInfo = queryPool.Get(queryID);
			if (queryInfo == nullptr)
				return;

			FiniQuery(queryInfo);
		}

		QueryTableMatch* QueryAddTableMatchForCache(QueryTableCache* cache)
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
				QueryTableMatchList& list = query->orderedTableList;
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
					query->orderedTableList.last = node;
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
			// Insert table matched node into list
			ECS_ASSERT(node->prev == nullptr && node->next == nullptr);

			bool groupByID = query->groupByID != INVALID_ENTITY;
			if (groupByID)
				node->groupID = ComputeGroupID(query, node);
			else
				node->groupID = 0;

			QueryTableMatchList* list = nullptr;
			if (groupByID)
				list = &query->groups[node->groupID];
			else
				list = &query->orderedTableList;

			if (list->last)
			{
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
					if (query->orderedTableList.last == last)
						query->orderedTableList.last = node;
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
				query->orderedTableList.count++;

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

			QueryTableMatchList& list = query->orderedTableList;
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

		bool QueryCheckTableMatched(QueryImpl* query, EntityTable* table)
		{
			if (table->type.empty())
				return false;

			if (table->flags & TableFlagIsPrefab)
				return false;

			if (TableSearchType(table, EcsTagPrefab) != -1)
				return false;

			for (int i = 0; i < query->itemCount; i++)
			{
				QueryItem& item = query->queryItems[i];
				if (!QueryItemMatchTable(table, item, nullptr, nullptr))
					return false;
			}

			return true;
		}

		void QueryAddMatchedTable(QueryImpl* query, EntityTable* table)
		{
			// 1. Add to cache if table is matched
			// 2. Add to non-empty-table list if table is non-emtpy

			// Add table into query cache
			QueryTableCache* node = ECS_CALLOC_T(QueryTableCache);
			ECS_ASSERT(node != nullptr);
			query->matchedTableCache.InsertTableIntoCache(this, table, node);

			// Create new tableMatch
			QueryTableMatch* tableMatch = QueryAddTableMatchForCache(node);
			ECS_ASSERT(tableMatch);
			tableMatch->table = table;
			tableMatch->itemCount = query->itemCount;
			tableMatch->componentIDs = ECS_CALLOC_T_N(EntityID, query->itemCount);
			ECS_ASSERT(tableMatch->componentIDs);

			tableMatch->columns = ECS_CALLOC_T_N(I32, query->itemCount);
			ECS_ASSERT(tableMatch->columns);

			tableMatch->sizes = ECS_CALLOC_T_N(size_t, query->itemCount);
			ECS_ASSERT(tableMatch->sizes);

			for (int t = 0; t < query->itemCount; t++)
			{
				EntityID compID = query->queryItems[t].compID;
				if (compID != INVALID_ENTITY)
				{
					InfoComponent* compInfo = GetComponentInfo(compID);
					if (compInfo)
						tableMatch->sizes[t] = compInfo->size;
					else
						tableMatch->sizes[t] = 0;

					// Add one to the column, as column zero is reserved 
					tableMatch->columns[t] = TableSearchType(table, compID);
				}
				else
				{
					tableMatch->sizes[t] = 0;
					tableMatch->columns[t] = 0;
				}

				tableMatch->componentIDs[t] = compID;
			}

			// Insert to Non-emtpy-list if table is not empty
			if (table->Count() != 0)
				QueryInsertTableMatchNode(query, tableMatch);
		}

		void QueryMatchTable(QueryImpl* query, EntityTable* table)
		{
			if (QueryCheckTableMatched(query, table))
				QueryAddMatchedTable(query, table);
		}

		void QueryUnmatchTable(QueryImpl* query, EntityTable* table)
		{
			QueryTableCache* item = query->matchedTableCache.RemoveTableFromCache(this, table);
			if (item != nullptr)
				QueryFreeTableCache(query, item);
		}

		// Match exsiting tables for query
		void QueryMatchTables(QueryImpl* query)
		{
			if (query->itemCount <= 0)
				return;

			// Check all exsiting tables
			size_t tabelCount = tablePool.Count();
			for (size_t i = 1; i < tabelCount; i++)
			{
				EntityTable* table = tablePool.GetByDense(i);
				if (table == nullptr)
					continue;

				QueryMatchTable(query, table);
			}
		}

		void ProcessQueryFlags(QueryImpl* query)
		{
			for (int i = 0; i < query->itemCount; i++)
			{
				QueryItem& queryItem = query->queryItems[i];
				if (queryItem.set.flags & QueryItemFlagCascade)
				{
					ECS_ASSERT(query->sortByItemIndex == 0);
					query->sortByItemIndex = i + 1;
				}
			}
		}

		void InitQueryItems(QueryImpl* impl, const QueryCreateDesc& desc)
		{
			I32 itemCount = 0;
			for (int i = 0; i < MAX_QUERY_ITEM_COUNT; i++)
			{
				if (desc.items[i].pred != INVALID_ENTITY)
					itemCount++;
			}
			impl->itemCount = itemCount;

			if (itemCount > 0)
			{
				// Get query item ptr
				QueryItem* itemPtr = nullptr;
				if (itemCount < QUERY_ITEM_SMALL_CACHE_SIZE)
				{
					itemPtr = impl->queryItemSmallCache;
				}
				else
				{
					impl->queryItemsCache.resize(itemCount);
					itemPtr = impl->queryItemsCache.data();
				}

				for (int i = 0; i < itemCount; i++)
					itemPtr[i] = desc.items[i];

				impl->queryItems = itemPtr;

				// Finalize query items
				for (int i = 0; i < itemCount; i++)
				{
					QueryItem& item = itemPtr[i];
					
					// Query item compID
					EntityID pred = item.pred;
					EntityID obj = item.obj;
					if (ECS_HAS_ROLE(pred, EcsRolePair))
					{
						ECS_ASSERT(item.obj != INVALID_ENTITY);
						pred = ECS_GET_PAIR_FIRST(item.pred);
						obj = ECS_GET_PAIR_SECOND(item.obj);
					
						item.pred = pred;
						item.obj = obj;
					}

					ECS_ASSERT(item.pred != INVALID_ENTITY);
					ECS_ASSERT(item.role == 0);	// Other roles are not supported

					if (obj != INVALID_ENTITY)
					{
						item.compID = (EcsRolePair | ECS_ENTITY_COMBO(obj, pred));
						item.role = EcsRolePair;
					}
					else
					{
						item.compID = pred;
						item.role = 0;
					}
					
					// Query item flags
					if (item.set.flags & QueryItemFlagParent)
						item.set.relation = EcsRelationChildOf;
				}
			}
		}

		// Create a new query
		QueryImpl* CreateNewQuery(const QueryCreateDesc& desc)
		{
			FlushPendingTables();

			QueryImpl* ret = queryPool.Requset();
			ECS_ASSERT(ret != nullptr);
			ret->queryID = queryPool.GetLastID();

			// Init query items
			InitQueryItems(ret, desc);

			// Process query flags
			ProcessQueryFlags(ret);

			// Set group context
			if (ret->sortByItemIndex > 0)
			{
				ret->groupByID = ret->queryItems[ret->sortByItemIndex - 1].compID;
				ret->groupByItem = &ret->queryItems[ret->sortByItemIndex - 1];
			}

			if (desc.cached)
			{
				// Match exsiting tables and add into cache if query cached
				QueryMatchTables(ret);
			}
			ret->cached = desc.cached;
				
			return ret;
		}

		void QueryFreeTableCache(QueryImpl*query, QueryTableCache* queryTable)
		{
			Util::ListNode<QueryTableMatch>* cur, *next;
			for (cur = queryTable->data.first; cur != nullptr; cur = next)
			{
				QueryTableMatch* match = cur->Cast();
				ECS_FREE(match->componentIDs);
				ECS_FREE(match->columns);
				ECS_FREE(match->sizes);

				// Remove non-emtpy table
				if (!queryTable->empty)
					QueryRemoveTableMatchNode(query, match);

				next = cur->next;
				ECS_FREE(cur);
			}
			ECS_FREE(queryTable);
		}

		void FiniQuery(QueryImpl* query)
		{
			if (query == nullptr)
				return;

			if (query->cached)
			{
				// TODO: reinterpret_cast 
				// Free query table cache 
				QueryTableCache* cache = nullptr;
				EntityTableCacheIterator iter = GetTableCacheListIter(&query->matchedTableCache, false);
				while (cache = (QueryTableCache*)GetTableCacheListIterNext(iter))
					QueryFreeTableCache(query, cache);

				iter = GetTableCacheListIter(&query->matchedTableCache, true);
				while (cache = (QueryTableCache*)GetTableCacheListIterNext(iter))
					QueryFreeTableCache(query, cache);
			}

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

		void GetQueryIteratorFromCache(QueryImpl* query, QueryIterator& iter)
		{
			QueryIteratorImpl& impl = *iter.impl;
			impl.query = query;
			impl.matchIter.curTableMatch = query->orderedTableList.first;
			impl.matchIter.matchCount = query->matchingCount;
		}

		void GetQueryIteratorByFilter(QueryImpl* query, QueryIterator& iter)
		{
			// Find the pivot item with the smallest number of table
			auto GetPivotItem = [&](QueryIterator& iter)->I32
			{
				I32 pivotItem = -1;
				I32 minTableCount = -1;
				for (int i = 0; i < iter.itemCount; i++)
				{
					QueryItem& item = iter.items[i];
					EntityID compID = item.compID;

					ComponentRecord* compRecord = GetComponentRecord(compID);
					if (compRecord == nullptr)
						return -2;

					I32 tableCount = compRecord->cache.tables.count;
					if (minTableCount == -1 || tableCount < minTableCount)
					{
						pivotItem = i;
						minTableCount = tableCount;
					}
				}
				return pivotItem;
			};
			I32 pivotItem = GetPivotItem(iter);
			if (pivotItem == -2)
			{
				iter.items = nullptr;
				return;
			}

			QueryIteratorImpl& impl = *iter.impl;
			impl.query = nullptr;
			impl.pivotItemIndex = pivotItem;
			impl.itemIter.currentItem = iter.items[pivotItem];
			impl.itemIter.compRecord = GetComponentRecord(impl.itemIter.currentItem.compID);
			impl.itemIter.tableCacheIter.cur = nullptr;
			impl.itemIter.tableCacheIter.next = impl.itemIter.compRecord->cache.tables.first;
		}

		QueryIterator GetQueryIterator(QueryID queryID) override
		{
			FlushPendingTables();

			QueryImpl* query = queryPool.Get(queryID);
			if (query == nullptr)
				return QueryIterator();

			QueryIterator iter = {};
			iter.world = this;
			iter.items = query->queryItems ? query->queryItems : nullptr;
			iter.itemCount = query->itemCount;

			if (query->cached)
				GetQueryIteratorFromCache(query, iter);
			else
				GetQueryIteratorByFilter(query, iter);

			// Sort tables

			return ECS_MOV(iter);
		}

		bool QueryItemMatchTable(EntityTable* table, QueryItem& item, EntityID* outID, I32* outColumn)
		{
			I32 column = TableSearchType(table, item.compID);
			if (column == -1)
				return false;

			if (outID) 
				*outID = item.compID;
			if (outColumn) 
				*outColumn = column;

			return true;
		}

		bool QueryIterMatchTable(EntityTable* table, QueryIterator& iter, I32 pivotItem, EntityID* ids, I32* columns)
		{
			// Check current table includes all compsIDs from items
			for (int i = 0; i < iter.itemCount; i++)
			{
				if (i == pivotItem)
					continue;

				if (!QueryItemMatchTable(table, iter.items[i], &ids[i], &columns[i]))
					return false;
			}

			return true;
		}

		bool QueryItemIteratorNext(QueryItemIterator* itemIter)
		{
			auto GetNextTable = [&](QueryItemIterator* itemIter)->TableComponentRecord*
			{
				if (itemIter->compRecord == nullptr)
					return nullptr;

				EntityTableCacheItem* item = nullptr;
				item = GetTableCacheListIterNext(itemIter->tableCacheIter);
				if (item == nullptr)
					return nullptr;

				return (TableComponentRecord*)item;
			};

			TableComponentRecord* tableRecord = nullptr;
			EntityTable* table = itemIter->table;
			do
			{
				if (table != nullptr)
				{
					itemIter->curMatch++;
					if (itemIter->curMatch >= itemIter->matchCount)
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
					tableRecord = GetNextTable(itemIter);
					if (tableRecord == nullptr)
						return false;

					EntityTable* table = tableRecord->table;
					if (table == nullptr)
						return false;

					if (table->flags & TableFlagIsPrefab)
						continue;

					itemIter->table = table;
					itemIter->curMatch = 0;
					itemIter->matchCount = tableRecord->data.count;
					itemIter->column = tableRecord->data.column;
					break;
				}
			} while (true);

			return true;
		}

		void QueryPopulateTableData(QueryIterator& iter, QueryIteratorImpl& impl, EntityTable* table)
		{
			iter.entityCount = table->entities.size();
			iter.entities = table->entities.data();
			iter.compDatas.resize(iter.itemCount);

			for (int i = 0; i < iter.itemCount; i++)
			{
				ComponentTypeInfo& typeInfo = table->compTypeInfos[impl.columns[i]];
				ComponentColumnData& columnData = table->storageColumns[impl.columns[i]];
				iter.compDatas[i] = columnData.Get(typeInfo.size, typeInfo.alignment, 0);
			}
		}

		void QueryIteratorInit(QueryIterator& iter)
		{
			QueryIteratorImpl& impl = *iter.impl;
			// Component ids
			if (impl.compIDs == nullptr)
			{
				if (iter.itemCount > MAX_QUERY_ITER_IMPL_CACHE_SIZE)
				{
					impl.compIDs = ECS_CALLOC_T_N(EntityID, iter.itemCount);
					impl.cache.compIDsAlloc = true;
				}
				else
				{
					impl.compIDs = impl.cache.ids;
				}
			}

			// Columns
			if (impl.columns == nullptr)
			{
				if (iter.itemCount > MAX_QUERY_ITER_IMPL_CACHE_SIZE)
				{
					impl.columns = ECS_CALLOC_T_N(I32, iter.itemCount);
					impl.cache.columnsAlloc = true;
				}
				else
				{
					impl.columns = impl.cache.columns;
				}
			}
		}

		bool QueryIteratorNextByFilter(QueryIterator& iter)
		{
			// Find current talbe
			// According the node graph to find the next table

			QueryIteratorInit(iter);

			QueryIteratorImpl& impl = *iter.impl;
			EntityTable* table = nullptr;
			bool match = false;
			do
			{
				// We need to match a new table for current id when matching count equal to zero
				bool first = impl.matchingLeft == 0;
				if (first)
				{
					if (!QueryItemIteratorNext(&impl.itemIter))
						return false;

					impl.matchingLeft = impl.itemIter.matchCount;
					table = impl.itemIter.table;

					if (impl.pivotItemIndex != -1)
					{
						impl.compIDs[impl.pivotItemIndex] = impl.itemIter.currentItem.compID;
						impl.columns[impl.pivotItemIndex] = impl.itemIter.column;
					}

					match = QueryIterMatchTable(table, iter, impl.pivotItemIndex, impl.compIDs, impl.columns);
				}

				if (!first && impl.matchingLeft > 0)
				{
					// TODO
					ECS_ASSERT(0);
				}

				match = impl.matchingLeft > 0;
				impl.matchingLeft--;
			} 
			while (!match);

			if (table == nullptr)
				return false;

			// Populate table data
			QueryPopulateTableData(iter, impl, table);

			return true;
		}
		 
		bool QueryIteratorNextFromCache(QueryIterator& iter)
		{
			QueryIteratorImpl& impl = *iter.impl;
			if (impl.query == nullptr || !impl.query->cached)
				return false;

			QueryImpl* query = impl.query;
			if (query->matchingCount <= 0)
				return false;

			bool ret = false;
			QueryMatchIterator& matchIter = iter.impl->matchIter;
			Util::ListNode<QueryTableMatch>* cur = matchIter.curTableMatch;
			Util::ListNode<QueryTableMatch>* next = nullptr;
			for (; cur != nullptr; cur = next)
			{
				next = cur->next;

				QueryTableMatch* match = cur->Cast();
				if (match->table == nullptr)
					continue;

				impl.compIDs = match->componentIDs;
				impl.columns = match->columns;

				// Populate table data
				QueryPopulateTableData(iter, impl, match->table);
				matchIter.curTableMatch = next;
				ret = true;
				break;
			}

			return ret;
		}

		bool QueryIteratorNext(QueryIterator& iter) override
		{
			QueryIteratorImpl& impl = *iter.impl;
			if (impl.query == nullptr)
				return false;

			if (impl.query->cached)
				return QueryIteratorNextFromCache(iter);
			else
				return QueryIteratorNextByFilter(iter);
		}

		void NotifyQuery(QueryImpl* query, const QueryEvent& ent)
		{
			switch (ent.type)
			{
			case QueryEventType::MatchTable:
				QueryMatchTable(query, ent.table);
				break;
			case QueryEventType::UnmatchTable:
				QueryUnmatchTable(query, ent.table);
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
			EntityTable* srcTable = nullptr, * table = nullptr;
			InternalEntityInfo info = {};
			if (!isNewEntity)
			{
				if (GetInternalEntityInfo(info, entity))
					table = info.table;
			}

			EntityTableDiff diff = {};

			// Add name component
			const char* name = desc.name;
			if (name && !nameAssigned)
				table = TableAppend(table, ECS_ENTITY_ID(NameComponent), diff);

			// Commit entity table
			if (srcTable != table)
			{
				CommitTables(entity, &info, table, diff, true);
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
			if (record->cache.tables.count > 0)
				return false;

			// No more tables in this record, free it
			if (record->cache.emptyTables.count == 0)
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

		bool GetInternalEntityInfo(InternalEntityInfo& internalInfo, EntityID entity)
		{
			internalInfo.entityInfo = nullptr;
			internalInfo.row = 0;
			internalInfo.table = nullptr;

			EntityInfo* info = entityPool.Get(entity);
			if (info == nullptr)
				return false;

			internalInfo.entityInfo = info;
			internalInfo.row = info->row;
			internalInfo.table = info->table;
			return true;
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

			ECS_ASSERT(ECS_GENERATION(entity) == 0);

			return entityPool.GetAliveIndex(entity);
		}

		// Check id is PropertyNone
		bool CheckIDHasPropertyNone(EntityID id)
		{
			return (id == EcsPropertyNone) || (ECS_HAS_ROLE(id, EcsRolePair)
				&& (ECS_GET_PAIR_FIRST(id) || ECS_GET_PAIR_SECOND(id)));
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
			compRecord->cache.InsertTableIntoCache(this, table, &tableRecord);

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

		void SetupComponentIDs()
		{
			InitBuiltinComponentTypeInfo<InfoComponent>(ECS_ENTITY_ID(InfoComponent));
			InitBuiltinComponentTypeInfo<NameComponent>(ECS_ENTITY_ID(NameComponent));
			InitBuiltinComponentTypeInfo<SystemComponent>(ECS_ENTITY_ID(SystemComponent));

			// Info component
			Reflect::ReflectInfo info = {};
			info.ctor = DefaultCtor;
			SetComponentTypeInfo(ECS_ENTITY_ID(InfoComponent), info);

			// Name component
			info.ctor = Reflect::Ctor<NameComponent>();
			info.dtor = Reflect::Dtor<NameComponent>();
			info.copy = Reflect::Copy<NameComponent>();
			info.move = Reflect::Move<NameComponent>();
			SetComponentTypeInfo(ECS_ENTITY_ID(NameComponent), info);
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

			lastComponentID = FirstUserComponentID;
			lastID = FirstUserEntityID;

			//ComponentTypeRegister<InfoComponent>::RegisterComponent(*this);
			//ComponentTypeRegister<NameComponent>::RegisterComponent(*this);
		}

		void InitBuiltinTags()
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
			Reflect::ReflectInfo info = {};
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
				} while (IsEntityAlive(ret) != INVALID_ENTITY && ret <= HiComponentID);
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
			InternalEntityInfo internalInfo = {};
			void* ret = GetOrCreateMutable(entity, compID, &internalInfo, added);
			ECS_ASSERT(ret != nullptr);
			return ret;
		}

		void* GetOrCreateMutable(EntityID entity, EntityID compID, InternalEntityInfo* info, bool* isAdded)
		{
			void* ret = nullptr;
			if (GetInternalEntityInfo(*info, entity) && info->table != nullptr)
				ret = GetComponentFromTable(*info->table, info->row, compID);

			if (ret == nullptr)
			{
				EntityTable* oldTable = info->table;
				AddComponentForEntity(entity, info, compID);

				GetInternalEntityInfo(*info, entity);
				ECS_ASSERT(info != nullptr);
				ECS_ASSERT(info->table != nullptr);
				ret = GetComponentFromTable(*info->table, info->row, compID);

				if (isAdded != nullptr)
					*isAdded = oldTable != info->table;
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
			InternalEntityInfo info = {};
			void* dst = GetOrCreateMutable(entity, compID, &info, NULL);
			ECS_ASSERT(dst != NULL);
			if (ptr)
			{
				ComponentTypeInfo* compTypeInfo = GetComponentTypeInfo(compID);
				if (compTypeInfo != nullptr)
				{
					if (isMove)
					{
						if (compTypeInfo->reflectInfo.move != nullptr)
							compTypeInfo->reflectInfo.move(this, &entity, &entity, compTypeInfo->size, 1, (void*)ptr, dst);
						else
							memcpy(dst, ptr, size);
					}
					else
					{
						if (compTypeInfo->reflectInfo.copy != nullptr)
							compTypeInfo->reflectInfo.copy(this, &entity, &entity, compTypeInfo->size, 1, ptr, dst);
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

		void AddComponentForEntity(EntityID entity, InternalEntityInfo* info, EntityID compID)
		{
			EntityTableDiff diff = {};
			EntityTable* srcTable = info->table;
			EntityTable* dstTable = TableTraverseAdd(srcTable, compID, diff);
			CommitTables(entity, info, dstTable, diff, true);
		}

		void CtorComponent(ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			ECS_ASSERT(columnData != nullptr);

			if (typeInfo != nullptr && typeInfo->reflectInfo.ctor != nullptr)
			{
				void* mem = columnData->Get(typeInfo->size, typeInfo->alignment, row);
				typeInfo->reflectInfo.ctor(this, entities, typeInfo->size, count, mem);
			}
		}

		void DtorComponent(ComponentTypeInfo* typeInfo, ComponentColumnData* columnData, EntityID* entities, EntityID compID, I32 row, I32 count)
		{
			ECS_ASSERT(columnData != nullptr);

			if (typeInfo == nullptr || typeInfo->reflectInfo.dtor != nullptr)
			{
				void* mem = columnData->Get(typeInfo->size, typeInfo->alignment, row);
				typeInfo->reflectInfo.dtor(this, entities, typeInfo->size, count, mem);
			}
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
			EntityTable** tablePtr = pendingTables.Ensure(table->tableID);
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
		
		I32 MoveTableEntity(EntityID entity, InternalEntityInfo* info, EntityTable* srcTable, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
		{
			// Get entity info
			EntityInfo* entityInfo = info->entityInfo;
			if (entityInfo == nullptr)
			{
				entityInfo = entityPool.Ensure(entity);
				info->entityInfo = entityInfo;
			}
			ECS_ASSERT(entityInfo != nullptr && entityInfo == entityPool.Get(entity));

			// Add a new entity for dstTable (Just reserve storage)
			U32 newRow = dstTable->AppendNewEntity(entity, entityInfo, false);
			ECS_ASSERT(srcTable->entities.size() > info->row);

			// Move comp datas from src table to new table of entity
			if (!srcTable->type.empty())
				MoveTableEntityImpl(entity, srcTable, info->row, entity, dstTable, newRow, construct);

			entityInfo->row = newRow;
			entityInfo->table = dstTable;

			// Remove old entity from src table
			srcTable->DeleteEntity(info->row, false);

			return newRow;
		}

		void CommitTables(EntityID entity, InternalEntityInfo* info, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
		{
			EntityTable* srcTable = info->table;
			if (srcTable == dstTable)
				return;

			ECS_ASSERT(dstTable != nullptr);
			if (srcTable != nullptr)
			{
				if (!dstTable->type.empty())
				{
					info->row = MoveTableEntity(entity, info, srcTable, dstTable, diff, construct);
					info->table = dstTable;
				}
				else
				{
					srcTable->DeleteEntity(info->row, true);
					EntityInfo* entityInfo = entityPool.Get(entity);
					if (entityInfo)
					{
						entityInfo->table = nullptr;
						entityInfo->row = 0;
					}
				}
			}
			else
			{
				EntityInfo* entityInfo = info->entityInfo;
				if (entityInfo == nullptr)
					entityInfo = entityPool.Ensure(entity);

				U32 newRow = dstTable->AppendNewEntity(entity, entityInfo, construct);
				entityInfo->row = newRow;
				entityInfo->table = dstTable;

				info->entityInfo = entityInfo;
				info->row = newRow;
				info->table = dstTable;
			}
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
						auto moveCtor = typeInfo.reflectInfo.moveCtor;
						auto dtor = typeInfo.reflectInfo.dtor;
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
						if (typeInfo.reflectInfo.copyCtor != nullptr)
							typeInfo.reflectInfo.copyCtor(this, &srcEntity, &dstEntity, typeInfo.size, 1, srcMem, dstMem);
						else
							memcpy(dstMem, srcMem, typeInfo.size);
					}
				}
				else
				{
					if (dstComponentID < srcComponentID)
					{
						if (construct)
							CtorComponent(
								&dstTable->compTypeInfos[dstColumnIndex],
								&dstTable->storageColumns[dstColumnIndex],
								&dstEntity,
								dstComponentID,
								dstRow,
								1);
					}
					else
					{
						DtorComponent(
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
					CtorComponent(
						&dstTable->compTypeInfos[dstColumnIndex],
						&dstTable->storageColumns[dstColumnIndex],
						&dstEntity,
						dstTable->storageIDs[dstColumnIndex],
						dstRow,
						1);
			}

			// Destruct remainning columns
			for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
				DtorComponent(
					&srcTable->compTypeInfos[srcColumnIndex],
					&srcTable->storageColumns[srcColumnIndex],
					&srcEntity,
					srcTable->storageIDs[srcColumnIndex],
					srcRow,
					1);
		}

		void FlushPendingTables()
		{
			if (pendingTables.Count() == 0)
				return;

			for (size_t i = 0; i < pendingTables.Count(); i++)
			{
				EntityTable* table = *pendingTables.GetByDense(i);
				if (table == nullptr || table->tableID == 0)
					continue;

				// Flush state of pending table
				ForEachEntityID(this, table, FlushTableState);
			}
			pendingTables.Clear();	// TODO
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

		void InsertTableIntoCache(EntityTableCacheBase* cache, const EntityTable* table, EntityTableCacheItem* cacheNode)
		{
			ECS_ASSERT(table != nullptr);
			ECS_ASSERT(cacheNode != nullptr);

			bool empty = table->entities.empty();
			cacheNode->tableCache = cache;
			cacheNode->table = (EntityTable*)(table);
			cacheNode->empty = empty;

			cache->tableRecordMap[table->tableID] = cacheNode;
			InsertTableCacheNode(cache, cacheNode, empty);
		}

		EntityTableCacheItem* RemoveTableFromCache(EntityTableCacheBase* cache, EntityTable* table)
		{
			auto it = cache->tableRecordMap.find(table->tableID);
			if (it == cache->tableRecordMap.end())
				return nullptr;

			EntityTableCacheItem* node = it->second;
			if (node == nullptr)
				return nullptr;

			RemoveTableCacheNode(cache, node, node->empty);

			cache->tableRecordMap.erase(table->tableID);
			return node;
		}

		void InsertTableCacheNode(EntityTableCacheBase* cache, EntityTableCacheItem* node, bool isEmpty)
		{
			Util::List<EntityTableCacheItem>& list = isEmpty ? cache->emptyTables : cache->tables;
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

		void RemoveTableCacheNode(EntityTableCacheBase* cache, EntityTableCacheItem* node, bool isEmpty)
		{
			Util::List<EntityTableCacheItem>& list = isEmpty ? cache->emptyTables : cache->tables;
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

		void SetTableCacheState(EntityTableCacheBase* cache, EntityTable* table, bool isEmpty)
		{
			auto it = cache->tableRecordMap.find(table->tableID);
			if (it == cache->tableRecordMap.end())
				return;

			EntityTableCacheItem* node = it->second;
			if (node == nullptr)
				return;

			if (node->empty == isEmpty)
				return;

			node->empty = isEmpty;

			if (isEmpty)
			{
				RemoveTableCacheNode(cache, node, false);
				InsertTableCacheNode(cache, node, true);
			}
			else
			{
				RemoveTableCacheNode(cache, node, true);
				InsertTableCacheNode(cache, node, false);
			}
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
					world->DtorComponent(
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

					if (typeInfo.reflectInfo.move != nullptr && typeInfo.reflectInfo.dtor != nullptr)
					{
						typeInfo.reflectInfo.move(world, &entityToMove, &entityToDelete, typeInfo.size, 1, srcMem, dstMem);
						typeInfo.reflectInfo.dtor(world, &entityToDelete, typeInfo.size, 1, srcMem);
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
		if (construct && compTypeInfo && compTypeInfo->reflectInfo.ctor != nullptr)
			compTypeInfo->reflectInfo.ctor(world, &entities[oldCount], compTypeInfo->size, addCount, mem);
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

			world->RemoveTableFromCache(cache, this);

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
		for (int i = 0; i < type.size(); i++)
		{
			TableComponentRecord* tableRecord = &tableRecords[i];
			ComponentRecord* compRecord = reinterpret_cast<ComponentRecord*>(tableRecord->tableCache);
			ECS_ASSERT(compRecord != nullptr && compRecord->typeInfoInited);
			compTypeInfos[i] = *compRecord->typeInfo;
			
			Reflect::ReflectInfo& reflectInfo = compRecord->typeInfo->reflectInfo;
			if (reflectInfo.ctor)
				flags |= TableFlagHasCtors;
			if (reflectInfo.dtor)
				flags |= TableFlagHasDtors;
			if (reflectInfo.copy)
				flags |= TableFlagHasCopy;
			if (reflectInfo.move)
				flags |= TableFlagHasMove;
		}
	}

	template<typename T>
	void EntityTableCache<T, std::enable_if_t<std::is_base_of_v<EntityTableCacheItem, T>, int>>::InsertTableIntoCache(WorldImpl* world, const EntityTable* table, T* cacheNode)
	{
		world->InsertTableIntoCache(this, table, cacheNode);
	}

	template<typename T>
	T* EntityTableCache<T, std::enable_if_t<std::is_base_of_v<EntityTableCacheItem, T>, int>>::RemoveTableFromCache(WorldImpl* world, EntityTable* table)
	{
		return reinterpret_cast<T*>(world->RemoveTableFromCache(this, table));
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Static function impl
	////////////////////////////////////////////////////////////////////////////////

	bool FlushTableState(WorldImpl* world, EntityTable* table, EntityID id, I32 column)
	{
		ComponentRecord* compRecord = world->GetComponentRecord(id);
		if (compRecord == nullptr)
			return false;

		world->SetTableCacheState(&compRecord->cache, table, table->Count() == 0);
		return true;
	}

	bool ForEachEntityID(WorldImpl* world, EntityTable* table, EntityIDAction action)
	{
		bool ret = false;
		for (U32 i = 0; i < table->type.size(); i++)
		{
			EntityID id = table->type[i];
			ret |= action(world, table, StripGeneration(id), i);
		}
		return ret;
	}

	ECS_UNIQUE_PTR<World> World::Create()
	{
		return ECS_MAKE_UNIQUE<WorldImpl>();
	}
}