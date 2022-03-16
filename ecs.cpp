#include "ecs.h"

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

	const EntityID HiComponentID = 256;
	const U32 FirstUserComponentID = 32;               // [32 - 256] user components	
	const U32 FirstUserEntityID = HiComponentID + 128; // [256 - 384] build-in tags

	// Roles
	const EntityID EcsRolePair = ((0x01ull) << 56);
	const EntityID EcsRoleShared = ((0x02ull) << 56);

	// Tags
	const EntityID EcsTagPrefab = HiComponentID + 0;

	// Relations
	const EntityID EcsRelationIsA = HiComponentID + 1;

#define ECS_ENTITY_MASK               (0xFFFFffffull)	// 32
#define ECS_ROLE_MASK                 (0xFFull << 56)
#define ECS_COMPONENT_MASK            (~ECS_ROLE_MASK)	// 56
#define ECS_GENERATION_MASK           (0xFFFFull << 32)
#define ECS_GENERATION(e)             ((e & ECS_GENERATION_MASK) >> 32)

#define ECS_HAS_ROLE(e, p) ((e & ECS_ROLE_MASK) == p)
#define ECS_ENTITY_HI(e) (static_cast<U32>((e) >> 32))
#define ECS_ENTITY_LOW(e) (static_cast<U32>(e))
#define ECS_ENTITY_COMBO(lo, hi) ((static_cast<U64>(hi) << 32) + static_cast<U32>(lo))
#define ECS_MAKE_PAIR(re, obj) (EcsRolePair | ECS_ENTITY_COMBO(obj, re))
#define ECS_GET_PAIR_FIRST(e) (ECS_ENTITY_HI(e & ECS_COMPONENT_MASK))
#define ECS_GET_PAIR_SECOND(e) (ECS_ENTITY_LOW(e))
#define ECS_HAS_RELATION(e, rela) (ECS_HAS_ROLE(e, EcsRolePair) && ECS_GET_PAIR_FIRST(e) == rela)
	
	inline U64 EntityTypeHash(const EntityType& entityType)
	{
		return Util::HashFunc(entityType.data(), entityType.size() * sizeof(EntityID));
	}

	inline EntityID StripGeneration(EntityID id)
	{
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
	//// Definition
	////////////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////
	// Table graph

	struct EntityTableDiff
	{
		EntityIDs added;			// Components added between tablePool
		EntityIDs removed;			// Components removed between tablePool
	};

	static EntityTableDiff EMPTY_TABLE_DIFF;

	struct TableGraphEdgeListNode
	{
		struct TableGraphEdgeListNode* prev = nullptr;
		struct TableGraphEdgeListNode* next = nullptr;
	};

	struct TableGraphEdge
	{
		TableGraphEdgeListNode listNode;
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
		TableGraphEdgeListNode incomingEdges;
	};

	///////////////////////////////////////////////////////////////
	// Entity info

	// EntityID <-> EntityInfo
	struct EntityInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
	};

	struct EntityInternalInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
		EntityInfo* entityInfo = nullptr;
	};


	///////////////////////////////////////////////////////////////
	// Event

	enum class EntityTableEventType
	{
		Invalid,
		ComponentTypeInfo
	};

	struct EntityTableEvent
	{
		EntityTableEventType type = EntityTableEventType::Invalid;
		EntityID compID = INVALID_ENTITY;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// Table cache
	////////////////////////////////////////////////////////////////////////////////

	// Dual link list to manage TableRecords
	struct EntityTableCacheListNode
	{
		struct EntityTableCache* cache = nullptr;
		EntityTable* table = nullptr;	// -> Owned table
		bool empty = false;
		EntityTableCacheListNode* prev = nullptr;
		EntityTableCacheListNode* next = nullptr;
	};

	struct EntityTableCacheList
	{
		EntityTableCacheListNode* first = nullptr;
		EntityTableCacheListNode* last = nullptr;
		U32 count = 0;
	};

	struct EntityTableCacheListIter
	{
		EntityTableCacheListNode* cur = nullptr;
		EntityTableCacheListNode* next = nullptr;
	};

	struct EntityTableCache
	{
		Hashmap<EntityTableCacheListNode*> tableRecordMap; // <TableID, CompTableRecord>
		EntityTableCacheList tables;
		EntityTableCacheList emptyTables;

		struct CompTableRecord* GetTableRecordFromCache(const EntityTable* table);
		bool RemoveTableFromCache(EntityTable* table, EntityTableCacheListNode* cacheNode);
		void SetTableCacheState(EntityTable* table, bool isEmpty);
		void InsertTableIntoCache(const EntityTable* table, EntityTableCacheListNode* cacheNode);

	private:
		void RemoveTableCacheNode(EntityTableCacheList& list, EntityTableCacheListNode* node);
		void InsertTableCacheNode(EntityTableCacheList& list, EntityTableCacheListNode* node);
	};

	struct CompTableRecord
	{
		EntityTableCacheListNode header;
		U64 compID = 0;
		I32 column = 0;					// The column of comp in target table
		I32 count = 0;
	};

	struct CompRecord
	{
		EntityTableCache cache;
		U64 recordID;
	};

	///////////////////////////////////////////////////////////////
	// Query

	struct QueryItemIter
	{
		QueryItem currentItem;
		CompRecord* compRecord;
		EntityTableCacheListIter tableCacheIter;
		EntityTable* table;
		I32 curMatch;
		I32 matchCount;
		I32 column;
	};

	const size_t QUERY_ITEM_SMALL_CACHE_SIZE = 4;

	// TODO: too heavy
	struct QueryInst
	{
		U64 queryID;
		QueryItem* queryItems;
		Vector<QueryItem> queryItemsCache;
		QueryItem queryItemSmallCache[QUERY_ITEM_SMALL_CACHE_SIZE];
		I32 itemCount;
		EntityTableCache tableCache;
	};

	struct QueryTableMatchListNode
	{
		struct QueryTableMatch* match = nullptr;
		QueryTableMatchListNode* prev = nullptr;
		QueryTableMatchListNode* next = nullptr;
	};

	struct QueryTableMatch
	{
		QueryTableMatchListNode node;
		EntityTable* table = nullptr;
		I32 itemCount = 0;
		U64* componentIDs = nullptr;
		U32* columns = nullptr;
		size_t* sizes = nullptr;
		QueryTableMatch* next = nullptr;
	};

	struct QueryTableCached
	{
		EntityTableCacheListNode header;
		QueryTableMatch* first = nullptr;
		QueryTableMatch* last = nullptr;
	};

	struct QueryIterImpl
	{
		QueryItemIter itemIter;		
		I32 matchingLeft;
		I32 pivotItemIndex;

		Vector<EntityID> ids;
		Vector<I32> columns;
	};

	QueryIter::QueryIter()
	{
		impl = ECS_MALLOC_T(QueryIterImpl);
		assert(impl);
		new (impl) QueryIterImpl();
	}

	QueryIter::~QueryIter()
	{
		if (impl != nullptr)
		{
			impl->~QueryIterImpl();
			ECS_FREE(impl);
		}
	}

	QueryIter::QueryIter(QueryIter&& rhs)noexcept
	{
		*this = ECS_MOV(rhs);
	}

	void QueryIter::operator=(QueryIter&& rhs)noexcept
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
	//// EntityTable
	////////////////////////////////////////////////////////////////////////////////

	enum TableFlag
	{
		TableFlagIsPrefab = 1 << 0,
		TableFlagHasIsA = 1 << 1,
	};

	struct ComponentColumnData
	{
		Util::StorageVector data;    // Column element data
		I64 size = 0;                // Column element size
		I64 alignment = 0;           // Column element alignment
	};

	// Archetype table for entity, archtype is a set of componentIDs
	struct EntityTable
	{
	public:
		WorldImpl* world = nullptr;
		U64 tableID = 0;
		EntityType type;
		TableGraphNode graphNode;
		bool isInitialized = false;
		U32 flags = 0;
		I32 refCount = 0;

		EntityType storageType;
		Vector<I32> typeToStorageMap;
		Vector<I32> storageToTypeMap;

		EntityTable* storageTable = nullptr;		// Without tags
		Vector<EntityID> entities;
		Vector<EntityInfo*> entityInfos;
		Vector<ComponentColumnData> storageColumns; // Comp1,         Comp2,         Comp3
		Vector<ComponentTypeInfo*> compTypeInfos;   // CompTypeInfo1, CompTypeInfo2, CompTypeInfo3
		Vector<CompTableRecord> tableRecords;       // CompTable1,    CompTable2,    CompTable3

		bool InitTable(WorldImpl* world_);
		void Claim();
		void Release();
		void Free();
		void FiniData(bool updateEntity, bool deleted);
		void DeleteEntity(U32 index, bool destruct);
		void RemoveColumnLast();
		void RemoveColumn(U32 index);
		void GrowColumn(Vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo* compTypeInfo, size_t addCount, size_t newCapacity, bool construct);
		U32  AppendNewEntity(EntityID entity, EntityInfo* info, bool construct);
		void RegisterTableRecords();
		void UnregisterTableRecords();
		bool RegisterComponentRecord(EntityID compID, I32 column, I32 count, CompTableRecord& tableRecord);
		size_t Count()const;
	};

	////////////////////////////////////////////////////////////////////////////////
	//// BuildIn components
	////////////////////////////////////////////////////////////////////////////////

	struct InfoComponent
	{
		COMPONENT(InfoComponent)
		size_t size = 0;
		size_t algnment = 0;
	};

	struct NameComponent
	{
		COMPONENT(NameComponent)
		const char* name = nullptr;
		U64 hash = 0;
	};

	struct SystemComponent
	{
		COMPONENT(SystemComponent)
		EntityID entity;
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
		QueryInst* query;
	};

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
		Hashmap<CompRecord*> compRecordMap;
		Util::SparseArray<CompRecord> compRecordPool;
		Util::SparseArray<ComponentTypeInfo> compTypePool;	// Component reflect type info

		// Graph
		TableGraphEdge* freeEdge = nullptr;

		// Query
		Util::SparseArray<QueryInst> queryPool;

		bool isFini = false;

		WorldImpl()
		{
			compRecordMap.reserve(HiComponentID);
			entityPool.SetSourceID(&lastID);
			if (!root.InitTable(this))
				assert(0);

			// Skip id 0
			U64 id = tablePool.NewIndex();
			assert(id == 0);

			SetupComponentIDs();
			InitBuildInComponents();
			InitBuildInTags();
			InitSystemComponent();
			RegisterBuildInComponents();
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
			TableGraphEdgeListNode* cur, *next = &freeEdge->listNode;
			while ((cur = next))
			{
				next = cur->next;
				ECS_FREE(cur);
			}

			// Fini all queries
			FiniQueries();

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
			assert(entity != INVALID_ENTITY);
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
			SetComponent(entity, NameComponent::GetComponentID(), sizeof(NameComponent), &nameComp, false);
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

		void* GetComponent(EntityID entity, EntityID compID) override
		{
			EntityInfo* info = entityPool.Get(entity); 
			if (info == nullptr || info->table == nullptr)
				return nullptr;

			CompTableRecord* tableRecord = GetTableRecord(info->table, compID);
			if (tableRecord == nullptr)
				return nullptr;

			return GetComponentWFromTable(*info->table, info->row, tableRecord->column);
		}

		bool HasComponent(EntityID entity, EntityID compID) override
		{
			return GetComponent(entity, compID) != nullptr;
		}

		void AddRelation(EntityID entity, EntityID relation, EntityID compID)override
		{
			AddComponent(entity, ECS_MAKE_PAIR(relation, compID));
		}

		bool HasComponentTypeAction(EntityID compID)const override
		{
			return GetComponentTypInfo(compID) != nullptr;
		}

		ComponentTypeInfo* GetComponentTypInfo(EntityID compID) override
		{
			return compTypePool.Get(compID);
		}

		const ComponentTypeInfo* GetComponentTypInfo(EntityID compID)const override
		{
			return compTypePool.Get(compID);
		}

		void SetComponentAction(EntityID compID, const Reflect::ReflectInfo& info) override
		{
			InfoComponent* infoComponent = GetComponentInfo(compID);
			if (infoComponent == nullptr)
				return;

			ComponentTypeInfo* compTypeInfo = GetComponentTypInfo(compID);
			if (compTypeInfo == nullptr)
			{
				compTypeInfo = compTypePool.Ensure(compID);
				assert(compTypeInfo != nullptr);
				compTypeInfo->isSet = false;
			}

			if (compTypeInfo->isSet)
			{
				assert(compTypeInfo->reflectInfo.ctor != nullptr);
				assert(compTypeInfo->reflectInfo.dtor != nullptr);
			}
			else
			{
				compTypeInfo->reflectInfo = info;
				compTypeInfo->compID = compID;
				compTypeInfo->size = infoComponent->size;
				compTypeInfo->alignment = infoComponent->algnment;
				compTypeInfo->isSet = true;
			}

			EntityTableEvent ent = {};
			ent.type = EntityTableEventType::ComponentTypeInfo;
			ent.compID = compID;
			NotifyTables(0, ent);
		}

		EntityID InitNewComponent(const ComponentCreateDesc& desc) override
		{
			EntityID entityID = CreateEntityID(desc.entity);
			if (entityID == INVALID_ENTITY)
				return INVALID_ENTITY;

			bool added = false;
			InfoComponent* info = GetOrCreateMutableByID<InfoComponent>(entityID, &added);
			if (info == nullptr)
				return INVALID_ENTITY;

			if (added)
			{
				info->size = desc.size;
				info->algnment = desc.alignment;
			}
			else
			{
				assert(info->size == desc.size);
				assert(info->algnment == desc.alignment);
			}

			if (entityID >= lastComponentID && entityID < HiComponentID)
				lastComponentID = (U32)(entityID + 1);

			return entityID;
		}

		void* GetOrCreateComponent(EntityID entity, EntityID compID) override
		{
			bool isAdded = false;
			EntityInternalInfo info = {};
			void* comp = GetOrCreateMutable(entity, compID, &info, &isAdded);
			assert(comp != nullptr);
			return comp;
		}

		void AddComponent(EntityID entity, EntityID compID) override
		{
			assert(IsEntityAlive(entity));

			EntityInternalInfo info = {};
			GetEntityInternalInfo(info, entity);

			EntityTableDiff diff = {};
			EntityTable* newTable = TableTraverseAdd(info.table, compID, diff);
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
			SystemComponent* sysComponent = GetOrCreateMutableByID<SystemComponent>(entity, &newAdded);
			if (newAdded)
			{
				memset(sysComponent, 0, sizeof(SystemComponent));
				sysComponent->entity = entity;
				sysComponent->action = desc.action;
				sysComponent->invoker = desc.invoker;
				sysComponent->invokerDeleter = desc.invokerDeleter;

				QueryInst* queryInfo = InitNewQuery(desc.query);
				if (queryInfo == nullptr)
					return INVALID_ENTITY;

				sysComponent->query = queryInfo;
			}
			return entity;
		}

		void RunSystem(EntityID entity) override
		{
			assert(entity != INVALID_ENTITY);
			SystemComponent* sysComponent =static_cast<SystemComponent*>(GetComponent(entity, SystemComponent::GetComponentID()));
			if (sysComponent == nullptr)
				return;

			SystemAction action = sysComponent->action;
			assert(action != nullptr);
			assert(sysComponent->query != nullptr);
			assert(sysComponent->invoker != nullptr);

			QueryIter iter = GetQueryIterator(sysComponent->query->queryID);
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
			QueryInst* queryInfo = InitNewQuery(desc);
			if (queryInfo == nullptr)
				return INVALID_ENTITY;

			return queryInfo->queryID;
		}

		void DestroyQuery(QueryID queryID) override
		{
			QueryInst* queryInfo = queryPool.Get(queryID);
			if (queryInfo == nullptr)
				return;

			FiniQuery(queryInfo);
		}

		QueryTableMatch* AddTableMatchForCache(QueryTableCached* cache)
		{
			QueryTableMatch* tableMatch = ECS_MALLOC_T(QueryTableMatch);
			assert(tableMatch);
			tableMatch->node.match = tableMatch;
			if (cache->first == nullptr)
			{
				cache->first = tableMatch;
				cache->last = tableMatch;
			}
			else
			{
				cache->last->next = tableMatch;
				cache->last = tableMatch;
			}
		}

		void QueryMatchTables(QueryInst* query)
		{
			auto MatchTable = [&](EntityTable* table)->bool
			{
				if (table->type.empty())
					return false;

				if (table->flags & TableFlagIsPrefab)
					return false;

				if (TableSearchType(table, TableFlagIsPrefab))
					return false;

				for (int i = 0; i < query->itemCount; i++)
				{
					QueryItem& item = query->queryItems[i];
					if (!QueryItemMatchTable(table, item, nullptr, nullptr))
						return false;
				}
				return true;
			};

			size_t tabelCount = tablePool.Count();
			for (size_t i = 1; i < tabelCount; i++)
			{
				EntityTable* table = tablePool.GetByDense(i);
				if (table == nullptr)
					continue;

				if (MatchTable(table))
				{
					// Add table into query cache
					QueryTableCached* node = ECS_MALLOC_T(QueryTableCached);
					assert(node != nullptr);
					query->tableCache.InsertTableIntoCache(table, &node->header);

					// Create new tableMatch
					QueryTableMatch* tableMatch = AddTableMatchForCache(node);
					assert(tableMatch);

					I32 itemCount = query->itemCount;
					if (itemCount > 0)
					{

					}
					
					for (int compIndex = 0; itemCount; compIndex++)
					{

					}
				}
			}
		}

		QueryInst* InitNewQuery(const QueryCreateDesc& desc)
		{
			FlushPendingTables();

			QueryInst* ret = queryPool.Requset();
			assert(ret != nullptr);
			ret->queryID = queryPool.GetLastID();

			// Create query items
			I32 itemCount = 0;
			for (int i = 0; i < MAX_QUERY_ITEM_COUNT; i++)
			{
				if (desc.items[i].compID != INVALID_ENTITY)
					itemCount++;
			}

			ret->itemCount = itemCount;

			if (itemCount > 0)
			{
				QueryItem* itemPtr = nullptr;
				if (itemCount < QUERY_ITEM_SMALL_CACHE_SIZE)
				{
					itemPtr = ret->queryItemSmallCache;
				}
				else
				{
					ret->queryItemsCache.resize(itemCount);
					itemPtr = ret->queryItemsCache.data();
				}

				for (int i = 0; i < itemCount; i++)
					itemPtr[i] = desc.items[i];

				ret->queryItems = itemPtr;
			}
		
			// Create query cache
			if (desc.cached)
			{
				// Match exsiting tables and add into cache
				QueryMatchTables(ret);
			}
				
			return ret;
		}

		void FiniQuery(QueryInst* query)
		{
			if (query == nullptr)
				return;

			queryPool.Remove(query->queryID);
		}

		void FiniQueries()
		{
			size_t queryCount = queryPool.Count();
			for (size_t i = 0; i < queryCount; i++)
			{
				QueryInst* query = queryPool.Get(i);
				FiniQuery(query);
			}
		}

		QueryIter GetQueryIterator(QueryID queryID) override
		{
			FlushPendingTables();

			QueryInst* info = queryPool.Get(queryID);
			if (info == nullptr)
				return QueryIter();

			QueryIter iter = {};
			iter.world = this;
			iter.items = info->queryItems ? info->queryItems : nullptr;
			iter.itemCount = info->itemCount;

			// Find the pivot item with the smallest number of table
			auto GetPivotItem = [&](QueryIter& iter)->I32
			{
				I32 pivotItem = -1;
				I32 minTableCount = -1;
				for (int i = 0; i < iter.itemCount; i++)
				{
					QueryItem& item = iter.items[i];
					EntityID compID = item.compID;

					CompRecord* compRecord = GetComponentRecord(compID);
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
				return iter;
			}

			QueryIterImpl& impl = *iter.impl;
			impl.pivotItemIndex = pivotItem;
			impl.itemIter.currentItem = iter.items[pivotItem];
			impl.itemIter.compRecord = GetComponentRecord(impl.itemIter.currentItem.compID);
			impl.itemIter.tableCacheIter.cur = nullptr;
			impl.itemIter.tableCacheIter.next = impl.itemIter.compRecord->cache.tables.first;

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

		bool QueryIterMatchTable(EntityTable* table, QueryIter& iter, I32 pivotItem, Vector<EntityID>& ids, Vector<I32>& columns)
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
		 
		bool QueryIteratorNext(QueryIter& iter) override
		{
			// Find current talbe
			// According the node graph to find the next table

			QueryIterImpl& impl = *iter.impl;
			impl.ids.resize(iter.itemCount);
			impl.columns.resize(iter.itemCount);

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
						impl.ids[impl.pivotItemIndex] = impl.itemIter.currentItem.compID;
						impl.columns[impl.pivotItemIndex] = impl.itemIter.column;
					}

					match = QueryIterMatchTable(table, iter, impl.pivotItemIndex, impl.ids, impl.columns);
				}

				if (!first && impl.matchingLeft > 0)
				{
					// TODO
					assert(0);
				}

				match = impl.matchingLeft > 0;
				impl.matchingLeft--;
			}
			while (!match);

			if (table == nullptr)
				return false;

			// Populate datas
			iter.entityCount = table->entities.size();
			iter.entities = table->entities.data();
			iter.compDatas.resize(iter.itemCount);
			for (int i = 0; i < iter.itemCount; i++)
			{
				ComponentColumnData& columnData = table->storageColumns[impl.columns[i]];
				iter.compDatas[i] = columnData.data.Get(columnData.size, columnData.alignment, 0);
			}

			return true;
		}

		bool QueryItemIteratorNext(QueryItemIter* itemIter)
		{
			auto GetNextTable = [](QueryItemIter* itemIter)->CompTableRecord*
			{
				if (itemIter->compRecord == nullptr)
					return nullptr;

				EntityTableCacheListIter& cacheIter = itemIter->tableCacheIter;
				EntityTableCacheListNode* next = cacheIter.next;
				if (next == nullptr)
					return nullptr;

				cacheIter.cur = cacheIter.next;
				cacheIter.next = next->next;

				return (CompTableRecord*)next;
			};

			CompTableRecord* tableRecord = nullptr;
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
						assert(0);
					}
				}

				if (table == nullptr)
				{
					tableRecord = GetNextTable(itemIter);
					if (tableRecord == nullptr)
						return false;

					EntityTable* table = tableRecord->header.table;
					if (table == nullptr)
						return false;

					if (table->flags & TableFlagIsPrefab)
						continue;

					itemIter->table = table;
					itemIter->curMatch = 0;
					itemIter->matchCount = tableRecord->count;
					itemIter->column = tableRecord->column;
					break;
				}
			}while (true);
	
			return true;
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
			EntityInternalInfo info = {};
			if (!isNewEntity)
			{
				if (GetEntityInternalInfo(info, entity))
					table = info.table;
			}

			EntityTableDiff diff = {};

			// Add name component
			const char* name = desc.name;
			if (name && !nameAssigned)
				table = TableAppend(table, NameComponent::GetComponentID(), diff);

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
		
		CompRecord* EnsureCompRecord(EntityID id)
		{
			auto it = compRecordMap.find(StripGeneration(id));
			if (it != compRecordMap.end())
				return it->second;

			CompRecord* ret = compRecordPool.Requset();
			ret->recordID = compRecordPool.GetLastID();
			compRecordMap[StripGeneration(id)] = ret;
			return ret;
		}

		void RemoveCompRecord(EntityID id, CompRecord* compRecord)
		{
			CompRecord record = *compRecord;
			compRecordPool.Remove(compRecord->recordID);
			compRecordMap.erase(StripGeneration(id));
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

		CompRecord* GetComponentRecord(EntityID id)
		{
			auto it = compRecordMap.find(StripGeneration(id));
			if (it == compRecordMap.end())
				return nullptr;
			return it->second;
		}

		bool GetEntityInternalInfo(EntityInternalInfo& internalInfo, EntityID entity)
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

		////////////////////////////////////////////////////////////////////////////////
		//// Component
		////////////////////////////////////////////////////////////////////////////////

		// BuildIn components
		void RegisterBuildInComponents()
		{
			ComponentTypeRegister<InfoComponent>::RegisterComponent(*this);
			ComponentTypeRegister<NameComponent>::RegisterComponent(*this);
		}

		void SetupComponentIDs()
		{
			InfoComponent::componentID = 1;
			NameComponent::componentID = 2;
			SystemComponent::componentID = 3;
		}

		void InitBuildInComponents()
		{
			// Create build-in table for build-in components
			EntityTable* table = nullptr;
			{
				Vector<EntityID> compIDs = {
					InfoComponent::GetComponentID(),
					NameComponent::GetComponentID()
				};
				table = FindOrCreateTableWithIDs(compIDs);

				table->entities.reserve(FirstUserComponentID);
				table->storageColumns[0].data.Reserve<InfoComponent>(FirstUserComponentID);
				table->storageColumns[1].data.Reserve<NameComponent>(FirstUserComponentID);
			}

			// Initialize build-in components
			auto InitBuildInComponent = [&](EntityID compID, U32 size, U32 alignment, const char* compName) {
				EntityInfo* entityInfo = entityPool.Ensure(compID);
				entityInfo->table = table;

				U32 index = table->AppendNewEntity(compID, entityInfo, false);
				entityInfo->row = index;

				// Component info
				InfoComponent* componentInfo = table->storageColumns[0].data.Get<InfoComponent>(index);
				componentInfo->size = size;
				componentInfo->algnment = alignment;

				// Name component
				NameComponent* nameComponent = table->storageColumns[1].data.Get<NameComponent>(index);
				nameComponent->name = _strdup(compName);
				nameComponent->hash = Util::HashFunc(compName, strlen(compName));

				entityNameMap[nameComponent->hash] = compID;
			};

			InitBuildInComponent(InfoComponent::GetComponentID(), sizeof(InfoComponent), alignof(InfoComponent), Util::Typename<InfoComponent>());
			InitBuildInComponent(NameComponent::GetComponentID(), sizeof(NameComponent), alignof(NameComponent), Util::Typename<NameComponent>());

			// Set component action
			// Info component
			Reflect::ReflectInfo info = {};
			info.ctor = DefaultCtor;
			SetComponentAction(InfoComponent::GetComponentID(), info);

			// Name component
			info.ctor = Reflect::Ctor<NameComponent>();
			info.dtor = Reflect::Dtor<NameComponent>();
			info.copy = Reflect::Copy<NameComponent>();
			info.move = Reflect::Move<NameComponent>();
			SetComponentAction(NameComponent::GetComponentID(), info);

			lastComponentID = FirstUserComponentID;
			lastID = FirstUserEntityID;
		}

		void InitBuildInTags()
		{
			InfoComponent tagInfo = {};
			tagInfo.size = 0;

			auto InitTag = [&](EntityID tagID, const char* name)
			{
				EntityInfo* entityInfo = entityPool.Ensure(tagID);
				assert(entityInfo != nullptr);

				SetComponent(tagID, InfoComponent::GetComponentID(), sizeof(InfoComponent), &tagInfo, false);
				SetEntityName(tagID, name);
			};
			// Tags
			InitTag(EcsTagPrefab, "EcsTagPrefab");

			// Relation
			InitTag(EcsRelationIsA, "EcsRelationIsA");
		}

		void InitSystemComponent()
		{
			// System is a special build-in component, it build in a independent table.
			ComponentCreateDesc desc = {};
			desc.entity.entity = SystemComponent::componentID;
			desc.entity.name = Util::Typename<SystemComponent>();
			desc.entity.useComponentID = true;
			desc.size = sizeof(SystemComponent);
			desc.alignment = alignof(SystemComponent);
			SystemComponent::componentID = InitNewComponent(desc);

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
			SetComponentAction(SystemComponent::GetComponentID(), info);
		}

		template<typename C>
		C* GetOrCreateMutableByID(EntityID entity, bool* added)
		{
			return static_cast<C*>(GetOrCreateMutableByID(entity, C::GetComponentID(), added));
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
			assert(compID != 0);
			assert(row >= 0);
			CompTableRecord* tableRecord = GetTableRecord(&table, compID);
			if (tableRecord == nullptr)
				return nullptr;

			return GetComponentWFromTable(table, row, tableRecord->column);
		}

		void* GetComponentWFromTable(EntityTable& table, I32 row, I32 column)
		{
			assert(column < (I32)table.storageType.size());
			ComponentColumnData& columnData = table.storageColumns[column];
			assert(columnData.size != 0);
			return columnData.data.Get(columnData.size, columnData.alignment, row);
		}

		void* GetOrCreateMutableByID(EntityID entity, EntityID compID, bool* added)
		{
			EntityInternalInfo internalInfo = {};
			void* ret = GetOrCreateMutable(entity, compID, &internalInfo, added);
			assert(ret != nullptr);
			return ret;
		}

		void* GetOrCreateMutable(EntityID entity, EntityID compID, EntityInternalInfo* info, bool* isAdded)
		{
			void* ret = nullptr;
			if (GetEntityInternalInfo(*info, entity) && info->table != nullptr)
				ret = GetComponentFromTable(*info->table, info->row, compID);

			if (ret == nullptr)
			{
				EntityTable* oldTable = info->table;
				AddComponentForEntity(entity, info, compID);

				GetEntityInternalInfo(*info, entity);
				assert(info != nullptr);
				assert(info->table != nullptr);
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
			return static_cast<InfoComponent*>(GetComponent(compID, InfoComponent::GetComponentID()));
		}

		void SetComponent(EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove)
		{
			EntityInternalInfo info = {};
			void* dst = GetOrCreateMutable(entity, compID, &info, NULL);
			assert(dst != NULL);
			if (ptr)
			{
				ComponentTypeInfo* compTypeInfo = GetComponentTypInfo(compID);
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


		void AddComponentForEntity(EntityID entity, EntityInternalInfo* info, EntityID compID)
		{
			EntityTableDiff diff = {};
			EntityTable* srcTable = info->table;
			EntityTable* dstTable = TableTraverseAdd(srcTable, compID, diff);
			CommitTables(entity, info, dstTable, diff, true);
		}

		////////////////////////////////////////////////////////////////////////////////
		//// Table
		////////////////////////////////////////////////////////////////////////////////
		EntityTable* CreateNewTable(EntityType entityType)
		{
			EntityTable* ret = tablePool.Requset();
			assert(ret != nullptr);
			ret->tableID = tablePool.GetLastID();
			ret->type = entityType;
			if (!ret->InitTable(this))
			{
				assert(0);
				return nullptr;
			}

			tableTypeHashMap[EntityTypeHash(entityType)] = ret;
			return ret;
		}

		EntityTable* GetTable(EntityID entity)
		{
			EntityInfo* info = entityPool.Get(entity);
			if (info == nullptr)
				return nullptr;

			return info->table;
		}

		I32 TableSearchType(EntityTable* table, EntityID compID)
		{
			if (table == nullptr)
				return -1;

			CompTableRecord* record = GetTableRecord(table, compID);
			if (record == nullptr)
				return -1;

			return record->column;
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
					assert(0);
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
			assert(newTable);

			// Create from prefab if comp has relation of isA
			if (ECS_HAS_ROLE(compID, EcsRolePair) && ECS_GET_PAIR_FIRST(compID) == EcsRelationIsA)
			{
				EntityID prefab = ECS_GET_PAIR_SECOND(compID);
				newTable = FindOrCreateTableWithPrefab(newTable, prefab);
			}

			// Connect parent with new table 
			AddTableGraphEdge(edge, compID, parent, newTable);

			return newTable;
		}

		EntityTable* TableAppend(EntityTable* table, EntityID compID, EntityTableDiff& diff)
		{
			EntityTableDiff tempDiff = {};
			EntityTable* ret = TableTraverseAdd(table, compID, diff);
			assert(ret != nullptr);
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
				assert(ret != nullptr);
			}

			PopulateTableDiff(edge, compID, INVALID_ENTITY, diff);
			return ret;
		}

		void SetTableEmpty(EntityTable* table)
		{
			EntityTable** tablePtr = pendingTables.Ensure(table->tableID);
			assert(tablePtr != nullptr);
			(*tablePtr) = table;
		}

		CompTableRecord* GetTableRecord(EntityTable* table, EntityID compID)
		{
			CompRecord* compRecord = GetComponentRecord(compID);
			if (compRecord == nullptr)
				return nullptr;

			return compRecord->cache.GetTableRecordFromCache(table->storageTable);
		}
		
		void CommitTables(EntityID entity, EntityInternalInfo* info, EntityTable* dstTable, EntityTableDiff& diff, bool construct)
		{
			EntityTable* srcTable = info->table;
			if (srcTable == dstTable)
				return;

			assert(dstTable != nullptr);
			if (srcTable != nullptr)
			{
				if (!dstTable->type.empty())
				{
					EntityInfo* entityInfo = info->entityInfo;
					if (entityInfo == nullptr)
					{
						entityInfo = entityPool.Ensure(entity);
						info->entityInfo = entityInfo;
					}
					assert(entityInfo != nullptr && entityInfo == entityPool.Get(entity));

					// Add a new entity for dstTable (Just reserve storage)
					U32 newRow = dstTable->AppendNewEntity(entity, entityInfo, false);
					assert(srcTable->entities.size() > info->row);
					if (!srcTable->type.empty())
						MoveTableEntities(entity, srcTable, info->row, entity, dstTable, newRow, construct);

					entityInfo->row = newRow;
					entityInfo->table = dstTable;

					// Remove old table
					srcTable->DeleteEntity(info->row, false);

					info->row = newRow;
					info->table = dstTable;
				}
				else
				{
					// DeleteEntityFromTable(srcTable, info->row, diff);
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

		void MoveTableEntities(EntityID srcEntity, EntityTable* srcTable, I32 srcRow, EntityID dstEntity, EntityTable* dstTable, I32 dstRow, bool construct)
		{
			auto CompConstruct = [&](EntityTable* table, EntityID entity, size_t size, U32 columnIndex, U32 row)
			{
				ComponentTypeInfo* compTypeInfo = table->compTypeInfos[columnIndex];
				if (compTypeInfo != nullptr && compTypeInfo->reflectInfo.ctor != nullptr)
				{
					ComponentColumnData& columnData = table->storageColumns[columnIndex];
					void* mem = columnData.data.Get(columnData.size, columnData.alignment, row);
					compTypeInfo->reflectInfo.ctor(this, &entity, size, 1, mem);
				}
			};

			auto CompDestruct = [&](EntityTable* table, EntityID entity, size_t size, U32 columnIndex, U32 row)
			{
				ComponentTypeInfo* compTypeInfo = table->compTypeInfos[columnIndex];
				if (compTypeInfo != nullptr && compTypeInfo->reflectInfo.dtor != nullptr)
				{
					ComponentColumnData& columnData = table->storageColumns[columnIndex];
					void* mem = columnData.data.Get(columnData.size, columnData.alignment, row);
					compTypeInfo->reflectInfo.dtor(this, &entity, size, 1, mem);
				}
			};

			// Move entites from srcTable to dstTable
			// Always keep the order of ComponentIDs()
			bool sameEntity = srcEntity == dstEntity;
			U32 srcNumColumns = (U32)srcTable->storageType.size();
			U32 dstNumColumns = (U32)dstTable->storageType.size();
			U32 srcColumnIndex, dstColumnIndex;
			for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
			{
				EntityID srcComponentID = srcTable->storageType[srcColumnIndex];
				EntityID dstComponentID = dstTable->storageType[dstColumnIndex];
				ComponentColumnData* srcColumnData = &srcTable->storageColumns[srcColumnIndex];
				ComponentColumnData* dstColumnData = &dstTable->storageColumns[dstColumnIndex];

				if (srcComponentID == dstComponentID)
				{
					void* srcMem = srcColumnData->data.Get(srcColumnData->size, srcColumnData->alignment, srcRow);
					void* dstMem = dstColumnData->data.Get(dstColumnData->size, dstColumnData->alignment, dstRow);

					assert(srcMem != nullptr);
					assert(dstMem != nullptr);

					ComponentTypeInfo* compTypeInfo = srcTable->compTypeInfos[srcColumnIndex];
					assert(compTypeInfo != nullptr);
					if (sameEntity)
					{
						auto& reflectInfo = compTypeInfo->reflectInfo;
						if (reflectInfo.moveCtor != nullptr && reflectInfo.dtor != nullptr)
						{
							reflectInfo.moveCtor(this, &srcEntity, &srcEntity, srcColumnData->size, 1, srcMem, dstMem);
							reflectInfo.dtor(this, &srcEntity, srcColumnData->size, 1, srcMem);
						}
						else
						{
							memcpy(dstMem, srcMem, dstColumnData->size);
						}
					}
					else
					{
						if (compTypeInfo->reflectInfo.copyCtor != nullptr)
							compTypeInfo->reflectInfo.copyCtor(this, &srcEntity, &dstEntity, srcColumnData->size, 1, srcMem, dstMem);
						else
							memcpy(dstMem, srcMem, dstColumnData->size);
					}
				}
				else
				{
					if (dstComponentID < srcComponentID)
					{
						if (construct)
							CompConstruct(dstTable, dstEntity, dstColumnData->size, dstColumnIndex, dstRow);
					}
					else
					{
						CompDestruct(srcTable, srcEntity, srcColumnData->size, srcColumnIndex, srcRow);
					}
				}

				srcColumnIndex += (dstComponentID >= srcComponentID);
				dstColumnIndex += (dstComponentID <= srcComponentID);
			}

			// Construct remainning columns
			if (construct)
			{
				for (; dstColumnIndex < dstNumColumns; dstColumnIndex++)
					CompConstruct(dstTable, dstEntity, dstTable->storageColumns[dstColumnIndex].size, dstColumnIndex, dstRow);
			}

			// Destruct remainning columns
			for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
				CompDestruct(srcTable, srcEntity, srcTable->storageColumns[srcColumnIndex].size, srcColumnIndex, srcRow);
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
			U32 srcNumColumns = (U32)t1->storageType.size();
			U32 dstNumColumns = (U32)t2->storageType.size();
			U32 srcColumnIndex, dstColumnIndex;
			for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
			{
				EntityID srcComponentID = t1->storageType[srcColumnIndex];
				EntityID dstComponentID = t2->storageType[dstColumnIndex];
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
					&& !(t2->flags & TableFlagHasIsA));
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
				EntityID srcComponentID = t1->storageType[srcColumnIndex];
				EntityID dstComponentID = t2->storageType[dstColumnIndex];
				if (srcComponentID < dstComponentID)
					diff->removed.push_back(srcComponentID);
				else if (srcComponentID > dstComponentID)
					diff->added.push_back(dstComponentID);

				srcColumnIndex += srcComponentID <= dstComponentID;
				dstColumnIndex += dstComponentID <= srcComponentID;
			}

			for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
				diff->removed.push_back(t1->storageType[srcColumnIndex]);
			for (; dstColumnIndex < dstNumColumns; dstColumnIndex++)
				diff->added.push_back(t2->storageType[dstColumnIndex]);

			assert(diff->added.size() == addedCount);
			assert(diff->removed.size() == removedCount);
		}

		void AddTableGraphEdge(TableGraphEdge* edge, EntityID compID, EntityTable* from, EntityTable* to)
		{
			edge->from = from;
			edge->to = to;
			edge->compID = compID;

			EnsureHiTableGraphEdge(from->graphNode.add, compID);

			if (from != to)
			{
				TableGraphEdgeListNode* toNode = &to->graphNode.incomingEdges;
				TableGraphEdgeListNode* next = toNode->next;
				toNode->next = &edge->listNode;

				edge->listNode.prev = toNode;
				edge->listNode.next = next;

				if (next != nullptr)
					next->prev = &edge->listNode;

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
			assert(edge != nullptr);
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
		//// Table graph
		////////////////////////////////////////////////////////////////////////////////

		TableGraphEdge* RequestTableGraphEdge()
		{
			TableGraphEdge* ret = freeEdge;
			if (ret != nullptr)
				freeEdge = (TableGraphEdge*)ret->listNode.next;
			else
				ret = ECS_MALLOC_T(TableGraphEdge);

			assert(ret != nullptr);
			memset(ret, 0, sizeof(TableGraphEdge));
			return ret;
		}

		void FreeTableGraphEdge(TableGraphEdge* edge)
		{
			edge->listNode.next =(TableGraphEdgeListNode*)freeEdge;
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
			TableGraphEdgeListNode* cur, *next = graphNode.incomingEdges.next;
			while ((cur = next))
			{
				next = cur->next;

				TableGraphEdge* edge = (TableGraphEdge*)cur;
				DisconnectEdge(edge, edge->compID);
				if (edge->from != nullptr)
				{
					edge->from->graphNode.add.hiEdges.erase(edge->compID);
					edge->from->graphNode.remove.hiEdges.erase(edge->compID);
				}
			}
			TableGraphEdgeListNode* prev = graphNode.incomingEdges.prev;
			while ((cur = prev))
			{
				prev = cur->prev;

				TableGraphEdge* edge = (TableGraphEdge*)cur;
				DisconnectEdge(edge, edge->compID);
				if (edge->from != nullptr)
				{
					edge->from->graphNode.add.hiEdges.erase(edge->compID);
					edge->from->graphNode.remove.hiEdges.erase(edge->compID);
				}
			}

			graphNode.add.hiEdges.clear();
			graphNode.remove.hiEdges.clear();
		}

		void DisconnectEdge(TableGraphEdge* edge, EntityID compID)
		{
			assert(edge != nullptr);
			assert(edge->compID == compID);

			// TODO: is valid?
			if (edge->from == nullptr)
				return;

			// Remove node from list of Edges
			TableGraphEdgeListNode* prev = edge->listNode.prev;
			TableGraphEdgeListNode* next = edge->listNode.next;
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
		//// TableEvent
		////////////////////////////////////////////////////////////////////////////////

		void NotifyTableComponentTypeInfo(EntityTable* table, EntityID compID)
		{
			if (compID != INVALID_ENTITY && !CheckEntityTypeHasComponent(table->storageType, compID))
				return;

			size_t columnCount = table->storageColumns.size();
			if (columnCount <= 0)
				return;

			table->compTypeInfos.resize(columnCount);
			for (int i = 0; i < table->storageType.size(); i++)
			{
				EntityID compID = table->storageType[i];
				assert(compID != INVALID_ENTITY);
				table->compTypeInfos[i] = GetComponentTypInfo(compID);
			}
		}

		void NotifyTable(EntityTable* table, const EntityTableEvent& ent)
		{
			switch (ent.type)
			{
			case EntityTableEventType::ComponentTypeInfo:
				NotifyTableComponentTypeInfo(table, ent.compID);
				break;
			default:
				break;
			}
		}

		void NotifyTables(U64 tableID, const EntityTableEvent& ent)
		{
			if (tableID == 0)
			{
				for (int i = 0; i < tablePool.Count(); i++)
				{
					EntityTable* table = tablePool.GetByDense(i);
					NotifyTable(table, ent);
				}
			}
			else
			{
				EntityTable* table = tablePool.Get(tableID);
				if (table != nullptr)
					NotifyTable(table, ent);
			}
		}
	};

	////////////////////////////////////////////////////////////////////////////////
	//// TableCache
	////////////////////////////////////////////////////////////////////////////////
	CompTableRecord* EntityTableCache::GetTableRecordFromCache(const EntityTable* table)
	{
		auto it = tableRecordMap.find(table->tableID);
		if (it == tableRecordMap.end())
			return nullptr;
		return reinterpret_cast<CompTableRecord*>(it->second);
	}

	void EntityTableCache::InsertTableIntoCache(const EntityTable* table, EntityTableCacheListNode* cacheNode)
	{
		assert(table != nullptr);
		assert(cacheNode != nullptr);

		bool empty = table->entities.empty();
		cacheNode->cache = this;
		cacheNode->table = (EntityTable*)(table);
		cacheNode->empty = empty;

		tableRecordMap[table->tableID] = cacheNode;
		InsertTableCacheNode(empty ? emptyTables : tables, cacheNode);
	}

	bool EntityTableCache::RemoveTableFromCache(EntityTable* table, EntityTableCacheListNode* cacheNode)
	{
		auto it = tableRecordMap.find(table->tableID);
		if (it == tableRecordMap.end())
			return false;

		EntityTableCacheListNode* node = it->second;
		if (node == nullptr)
			return false;

		RemoveTableCacheNode(node->empty ? emptyTables : tables, node);

		tableRecordMap.erase(table->tableID);
		return true;
	}

	void EntityTableCache::InsertTableCacheNode(EntityTableCacheList& list, EntityTableCacheListNode* node)
	{
		EntityTableCacheListNode* last = list.last;
		list.last = node;
		list.count++;
		if (list.count == 1)
			list.first = node;

		node->next = nullptr;
		node->prev = last;

		if (last != nullptr)
			last->next = node;
	}

	void EntityTableCache::RemoveTableCacheNode(EntityTableCacheList& list, EntityTableCacheListNode* node)
	{
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

	void EntityTableCache::SetTableCacheState(EntityTable* table, bool isEmpty)
	{
		auto it = tableRecordMap.find(table->tableID);
		if (it == tableRecordMap.end())
			return;

		EntityTableCacheListNode* node = it->second;
		if (node == nullptr)
			return;

		if (node->empty == isEmpty)
			return;

		node->empty = isEmpty;

		if (isEmpty)
		{
			RemoveTableCacheNode(tables, node);
			InsertTableCacheNode(emptyTables, node);
		}
		else
		{
			RemoveTableCacheNode(emptyTables, node);
			InsertTableCacheNode(tables, node);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	//// EntityTableImpl
	////////////////////////////////////////////////////////////////////////////////

	bool EntityTable::InitTable(WorldImpl* world_)
	{
		assert(world_ != nullptr);
		world = world_;
		refCount = 1;

		// Ensure all ids used exist */
		for (auto& id : type)
			world->EnsureEntity(id);

		// Init table flags
		for (U32 i = 0; i < type.size(); i++)
		{
			EntityID compID = type[i];
			if (compID == EcsTagPrefab)
				flags |= TableFlagIsPrefab;

			if (ECS_HAS_ROLE(compID, EcsRolePair))
			{
				if (ECS_GET_PAIR_FIRST(compID) == EcsRelationIsA)
					flags |= TableFlagHasIsA;
			}
		}

		//  Register table records
		RegisterTableRecords();

		// Init storage table
		Vector<EntityID> storageIDs;
		for (U32 i = 0; i < type.size(); i++)
		{
			EntityID id = type[i];
			if (id == INVALID_ENTITY)
			{
				assert(0);
				continue;
			}

			// Check is build-in components
			if (id == InfoComponent::GetComponentID() || id == NameComponent::GetComponentID())
			{
				storageIDs.push_back(id);
				continue;
			}

			const InfoComponent* compInfo = world->GetComponentInfo(id);
			if (compInfo == nullptr || compInfo->size <= 0)	// Skip tag/Relation
				continue;

			storageIDs.push_back(id);
		}
		if (storageIDs.size() > 0)
		{
			if (storageIDs.size() != type.size())
			{
				storageTable = world->FindOrCreateTableWithIDs(storageIDs);
				storageTable->refCount++;
				storageType = storageTable->type;
			}
			else
			{
				storageTable = this;
				storageType = type;
			}
		}
		// Init storage map
		if (typeToStorageMap.empty() || storageToTypeMap.empty())
		{
			U32 numType = (U32)type.size();
			U32 numStorageType = (U32)storageType.size();

			typeToStorageMap.resize(numType);
			storageToTypeMap.resize(numStorageType);

			U32 t, s;
			for (s = 0, t = 0; (t < numType) && (s < numStorageType); )
			{
				EntityID id = type[t];
				EntityID storageID = storageType[s];
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

		// Init column datas
		if (storageType.size() > 0)
		{
			storageColumns.resize(storageType.size());
			for (U32 i = 0; i < storageType.size(); i++)
			{
				auto& column = storageColumns[i];
				EntityID compID = storageType[i];
				// First check is build-in component
				if (compID == InfoComponent::GetComponentID())
				{
					column.size = sizeof(InfoComponent);
					column.alignment = alignof(InfoComponent);
					continue;
				}
				else if (compID == NameComponent::GetComponentID())
				{
					column.size = sizeof(NameComponent);
					column.alignment = alignof(NameComponent);
					continue;
				}

				const InfoComponent* compInfo = world->GetComponentInfo(compID);
				assert(compInfo != nullptr);
				assert(compInfo->size > 0);
				column.size = compInfo->size;
				column.alignment = compInfo->algnment;
			}
		}

		// Notify event of component type info
		EntityTableEvent ent = {};
		ent.type = EntityTableEventType::ComponentTypeInfo;
		world->NotifyTables(0, ent);
		return true;
	}

	void EntityTable::Claim()
	{
		assert(refCount > 0);
		refCount++;
	}

	void EntityTable::Release()
	{
		assert(refCount > 0);
		if (--refCount == 0)
			Free();
	}

	void EntityTable::Free()
	{
		bool isRoot = this == &world->root;
		assert(isRoot || this->tableID != 0);
		assert(refCount == 0);

		// Fini data
		FiniData(true, true);
		
		// Clear all graph edges
		world->ClearTableGraphEdges(this);

		if (!isRoot)
		{
			//  Unregister table
			UnregisterTableRecords();

			// Remove from hashMap
			world->tableTypeHashMap.erase(EntityTypeHash(type));
		}

		if (storageTable != nullptr && storageTable != this)
			storageTable->Release();

		world->tablePool.Remove(tableID);
	}

	void EntityTable::FiniData(bool updateEntity, bool deleted)
	{
		// Dtor all components
		size_t count = entities.size();
		if (count > 0)
		{
			for (size_t row = 0; row < count; row++)
			{
				for (size_t col = 0; col < storageColumns.size(); col++)
				{
					ComponentColumnData& columnData = storageColumns[col];
					void* mem = columnData.data.Get(columnData.size, columnData.alignment, row);
					if (compTypeInfos[col]->reflectInfo.dtor != nullptr)
						compTypeInfos[col]->reflectInfo.dtor(world, &entities[row], columnData.size, 1, mem);
				}

				if (updateEntity)
				{
					EntityID entity = entities[row];
					assert(entity != INVALID_ENTITY);
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

		assert(entityInfos.size() == entities.size());

		for (int i = 0; i < storageColumns.size(); i++)
		{
			ComponentColumnData& columnData = storageColumns[i];
			assert(columnData.data.GetCount() == count);
			columnData.data.Clear();
		}
		storageColumns.clear();

		entities.clear();
		entityInfos.clear();
	}

	void EntityTable::DeleteEntity(U32 index, bool destruct)
	{
		U32 count = (U32)entities.size() - 1;
		assert(count >= 0);

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
			if (destruct)
			{
				for (int i = 0; i < storageType.size(); i++)
				{
					ComponentTypeInfo* compTypeInfo = compTypeInfos[i];
					if (compTypeInfo != nullptr && compTypeInfo->reflectInfo.dtor != nullptr)
					{
						auto& columnData = storageColumns[i];
						void* mem = columnData.data.Get(columnData.size, columnData.alignment, count);
						compTypeInfo->reflectInfo.dtor(world, &entityToDelete, compTypeInfo->size, 1, mem);
					}
				}
			}
			RemoveColumnLast();
		}
		else
		{
			// Swap target element and last element, then remove last element
			if (destruct)
			{
				for (int i = 0; i < storageType.size(); i++)
				{
					auto& reflectInfo = compTypeInfos[i]->reflectInfo;
					if (reflectInfo.move != nullptr && reflectInfo.dtor != nullptr)
					{
						auto& columnData = storageColumns[i];
						void* srcMem = columnData.data.Get(columnData.size, columnData.alignment, count);
						void* dstMem = columnData.data.Get(columnData.size, columnData.alignment, index);
						reflectInfo.move(world, &entityToMove, &entityToDelete, compTypeInfos[i]->size, 1, srcMem, dstMem);
						reflectInfo.dtor(world, &entityToDelete, compTypeInfos[i]->size, 1, srcMem);
					}
					else
					{
						auto& columnData = storageColumns[i];
						void* srcMem = columnData.data.Get(columnData.size, columnData.alignment, count);
						void* dstMem = columnData.data.Get(columnData.size, columnData.alignment, index);
						memcpy(dstMem, srcMem, columnData.size);
					}
				}
			}
			else
			{
				RemoveColumn(index);
			}
		}
	}

	void EntityTable::RemoveColumnLast()
	{
		for (int i = 0; i < storageType.size(); i++)
		{
			auto& columnData = storageColumns[i];
			columnData.data.RemoveLast();
		}
	}

	void EntityTable::RemoveColumn(U32 index)
	{
		for (int i = 0; i < storageType.size(); i++)
		{
			auto& columnData = storageColumns[i];
			columnData.data.Remove(columnData.size, columnData.alignment, index);
		}
	}

	void EntityTable::GrowColumn(Vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo* compTypeInfo, size_t addCount, size_t newCapacity, bool construct)
	{
		U32 oldCount = (U32)columnData.data.GetCount();
		U32 oldCapacity = (U32)columnData.data.GetCapacity();
		if (oldCapacity != newCapacity)
			columnData.data.Reserve(columnData.size, columnData.alignment, newCapacity);

		void* mem = columnData.data.PushBackN(columnData.size, columnData.alignment, addCount);
		if (construct && compTypeInfo && compTypeInfo->reflectInfo.ctor != nullptr)
			compTypeInfo->reflectInfo.ctor(world, &entities[oldCount], columnData.size, addCount, mem);
	}

	U32 EntityTable::AppendNewEntity(EntityID entity, EntityInfo* info, bool construct)
	{
		U32 index = (U32)entities.size();

		// Add a new entity for table
		entities.push_back(entity);
		entityInfos.push_back(info);

		// ensure that the columns have the same size as the entities and records.
		U32 newCapacity = (U32)entities.capacity();
		for (int i = 0; i < storageType.size(); i++)
		{
			ComponentColumnData& columnData = storageColumns[i];
			ComponentTypeInfo* compTypeInfo = nullptr;
			if (!compTypeInfos.empty())
				compTypeInfo = compTypeInfos[i];

			GrowColumn(entities, columnData, compTypeInfo, 1, newCapacity, construct);
		}

		// Pending empty table
		if (index == 0)
			world->SetTableEmpty(this);

		return index;
	}

	struct TableTypeItem
	{
		U32 pos = 0;
		U32 count = 0;
	};
	void EntityTable::RegisterTableRecords()
	{
		if (type.empty())
			return;

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
			}
		}

		tableRecords.resize(type.size() + relations.size() + objects.size());

		// Base type
		U32 index = 0;
		for (U32 i = 0; i < type.size(); i++)
		{
			RegisterComponentRecord(type[i], i, 1, tableRecords[i]);
			index++;
		}
			
		//// Relations
		//for (auto& kvp : relations)
		//	RegisterComponentRecord(kvp.first, kvp.second.pos, kvp.second.count, tableRecords[index++]);

		//// Objects
		//for (auto& kvp : objects)
		//	RegisterComponentRecord(kvp.first, kvp.second.pos, kvp.second.count, tableRecords[index++]);
	}

	void EntityTable::UnregisterTableRecords()
	{
		for (size_t i = 0; i < tableRecords.size(); i++)
		{
			CompTableRecord* tableRecord = &tableRecords[i];
			EntityTableCache* cache = tableRecord->header.cache;
			if (cache == nullptr)
				continue;

			cache->RemoveTableFromCache(storageTable, &tableRecord->header);

			if (cache->tableRecordMap.empty())
			{
				CompRecord* compRecord = reinterpret_cast<CompRecord*>(cache);
				world->RemoveCompRecord(tableRecord->compID, compRecord);
			}
		}
		tableRecords.clear();
	}

	bool EntityTable::RegisterComponentRecord(EntityID compID, I32 column, I32 count, CompTableRecord& tableRecord)
	{
		compID = StripGeneration(compID);
		CompRecord* compRecord = world->EnsureCompRecord(compID);
		assert(compRecord != nullptr);
		tableRecord.compID = compID;
		tableRecord.column = column;	// Index for component from entity type
		tableRecord.count = count;
		compRecord->cache.InsertTableIntoCache(this, &tableRecord.header);
		return true;
	}

	size_t EntityTable::Count()const
	{
		return entities.size();
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Static function impl
	////////////////////////////////////////////////////////////////////////////////

	bool FlushTableState(WorldImpl* world, EntityTable* table, EntityID id, I32 column)
	{
		CompRecord* compRecord = world->GetComponentRecord(id);
		if (compRecord == nullptr)
			return false;

		compRecord->cache.SetTableCacheState(table, table->Count() == 0);
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