#include "ecs.h"

namespace ECS
{
	struct WorldImpl;
	struct EntityTable;
	struct EntityInfo;

// EntityID
// FFFFffff   | FFFFffff
// Generation |  ID
#define ECS_ENTITY_MASK               (0xFFFFffffull)
#define ECS_GENERATION_MASK           (0xFFFFull << 32)
#define ECS_GENERATION(e)             ((e & ECS_GENERATION_MASK) >> 32)

	namespace
	{
		const U32 FirstUserComponentID = 32;
		const U32 FirstUserEntityID = HI_COMPONENT_ID + 128;

		U64 EntityTypeHash(const EntityType& entityType)
		{
			return Util::HashFunc(entityType.data(), entityType.size() * sizeof(EntityID));
		}

		EntityID StripGeneration(EntityID id)
		{
			return id & (~ECS_GENERATION_MASK);
		}

		U8* DataVectorAdd(std::vector<U8>& data, U32 elemSize, U32 alignment)
		{
			return nullptr;
		}

		U8* DataVectorAddN(std::vector<U8>& data, U32 elemSize, U32 alignment, U32 count)
		{
			if (count == 1)
				return DataVectorAdd(data, elemSize, alignment);

			return nullptr;
		}

		void DefaultCtor(World* world, EntityID* entities, size_t size, size_t count, void* ptr)
		{
			memset(ptr, 0, size * count);
		}
	}

	static bool RegisterTable(WorldImpl* world, EntityTable* table, EntityID id, I32 column);
	static bool UnregisterTable(WorldImpl* world, EntityTable* table, EntityID id, I32 column);
	
	using EntityIDAction = bool(*)(WorldImpl*, EntityTable*, EntityID, I32);
	bool ForEachEntityID(WorldImpl* world, EntityTable* table, EntityIDAction action);

	////////////////////////////////////////////////////////////////////////////////
	//// Definition
	////////////////////////////////////////////////////////////////////////////////

	struct ComponentColumnData
	{
		Util::StorageVector data;    // Column element data
		I64 size = 0;                // Column element size
		I64 alignment = 0;           // Column element alignment
	};

	struct EntityTableDiff
	{
		EntityIDs added;			// Components added between tables
		EntityIDs removed;			// Components removed between tables
	};

	struct EntityGraphEdge
	{
		EntityTable* next = nullptr;
		I32 diffIndex = -1;			// mapping to EntityGraphNode diffBuffer
	};
	struct EntityGraphEdges
	{
		EntityGraphEdge loEdges[HI_COMPONENT_ID];
		Hashmap<EntityGraphEdge> hiEdges;
	};
	struct EntityGraphNode
	{
		EntityGraphEdges add;
		EntityGraphEdges remove;
		std::vector<EntityTableDiff> diffs;
	};

	// Dual link list to manage TableRecords
	struct CompTableCacheListNode
	{
		EntityTable* table = nullptr;	// -> Owned table
		bool empty = false;
		CompTableCacheListNode* prev = nullptr;
		CompTableCacheListNode* next = nullptr;
	};
	struct CompTableCacheList
	{
		CompTableCacheListNode* first = nullptr;
		CompTableCacheListNode* last = nullptr;
		U32 count = 0;
	};

	struct CompTableRecord
	{
		CompTableCacheListNode header;
		EntityTable* table = nullptr;	// -> Owned table
		I32 column = 0;					// The column of comp in target table
		I32 count = 0;
	};

	struct EntityTableCache
	{
		Hashmap<CompTableCacheListNode*> tableRecordMap; // <TableID, CompTableRecord>
		CompTableCacheList records;
		CompTableCacheList emptyRecords;
	};

	// EntityID <-> EntityInfo
	struct EntityInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
	};

	struct ComponentRecord
	{
		EntityTableCache cache;	// Record all columns and tables which used this comp  
		Hashmap<EntityTable*> addRefs;
		Hashmap<EntityTable*> removeRefs;
		U64 recordID;
	};

	struct EntityInternalInfo
	{
		EntityTable* table = nullptr;
		I32 row = 0;
		EntityInfo* entityInfo = nullptr;
	};

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
	//// EntityTable
	////////////////////////////////////////////////////////////////////////////////

	// Archetype for entity, archtype is a set of componentIDs
	struct EntityTable
	{
	public:
		WorldImpl* world = nullptr;
		U64 tableID = 0;
		EntityType type;
		EntityGraphNode graphNode;
		bool isInitialized = false;
		U32 flags = 0;
		I32 refCount = 0;

		EntityTable* storageTable = nullptr;
		EntityType storageType;
		std::vector<I32> typeToStorageMap;
		std::vector<I32> storageToTypeMap;

		std::vector<EntityID> entities;
		std::vector<EntityInfo*> entityInfos;
		std::vector<ComponentColumnData> storageColumns; // Comp1,         Comp2,         Comp3
		std::vector<ComponentTypeInfo*> compTypeInfos;   // CompTypeInfo1, CompTypeInfo2, CompTypeInfo3
	
		bool InitTable(WorldImpl* world_);
		void Claim();
		void Release();
		void Free();
		void FiniData(bool updateEntity, bool deleted);
		EntityGraphEdge* FindOrCreateEdge(EntityGraphEdges& egdes, EntityID compID);
		EntityGraphEdge* FindEdge(EntityGraphEdges& egdes, EntityID compID);
		void ClearEdge(EntityID compID, bool isAdded);
		void DeleteEntity(U32 index, bool destruct);
		void RemoveColumnLast();
		void RemoveColumn(U32 index);
		void GrowColumn(std::vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo* compTypeInfo, size_t addCount, size_t newCapacity, bool construct);
		U32  AppendNewEntity(EntityID entity, EntityInfo* info, bool construct);
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
		Util::SparseArray<EntityTable> tables;
		Util::SparseArray<EntityTable*> pendingTables;
		Hashmap<EntityTable*> tableTypeHashMap;

		// Component
		Hashmap<ComponentRecord*> compRecordMap;
		Util::SparseArray<ComponentRecord> compRecordPool;
		Util::SparseArray<ComponentTypeInfo> compTypePool;

		WorldImpl()
		{
			compRecordMap.reserve(HI_COMPONENT_ID);
			entityPool.SetSourceID(&lastID);
			if (!root.InitTable(this))
				assert(0);

			// Skip id 0
			U64 id = tables.NewIndex();
			assert(id == 0);

			// Initialize build-in components
			InitBuildInComponents();
			RegisterBuildInComponents();
		}

		~WorldImpl()
		{	
			// Skip id 0
			size_t tabelCount = tables.Count();
			for (size_t i = 1; i < tabelCount; i++)
			{
				EntityTable* table = tables.Get(i);
				if (table != nullptr)
					table->Release();
			}
			tables.Clear();
			pendingTables.Clear();
			root.Release();
			entityPool.Clear();
		}

		const EntityBuilder& CreateEntity(const char* name) override
		{
			entityBuilder.entity = CreateEntityID(name);
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

		EntityID EntityIDAlive(EntityID entity) override
		{
			if (entity == INVALID_ENTITY)
				return INVALID_ENTITY;

			if (entityPool.CheckExsist(entity))
				return entity;

			return false;
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

			if (tableID > 0 && tables.CheckExsist(tableID))
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
			if (EntityIDAlive(StripGeneration(entity)) == entity)
				return;

			entityPool.Ensure(entity);
		}

		void* GetComponent(EntityID entity, EntityID compID)override
		{
			EntityInfo* info = entityPool.Get(entity);
			if (info == nullptr || info->table == nullptr)
				return nullptr;

			EntityTable* table = info->table;
			if (compID != INVALID_ENTITY && table->storageTable == nullptr)
				assert(0);

			ComponentRecord* compRecord = FindCompRecord(compID);
			if (compRecord == nullptr)
				return nullptr;

			CompTableRecord* tableRecord = GetTableRecordFromCache(&compRecord->cache, *table->storageTable);
			if (tableRecord == nullptr)
				assert(0);

			return GetComponentWFromTable(*table, info->row, tableRecord->column);
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
			InfoComponent* info = GetComponentMutableByID<InfoComponent>(entityID, InfoComponent::GetComponentID(), &added);
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

			if (entityID >= lastComponentID && entityID < HI_COMPONENT_ID)
				lastComponentID = (U32)(entityID + 1);

			return entityID;
		}

		void* GetOrCreateComponent(EntityID entity, EntityID compID) override
		{
			bool isAdded = false;
			EntityInternalInfo info = {};
			void* comp = GetComponentMutable(entity, compID, &info, &isAdded);
			assert(comp != nullptr);
			return comp;
		}

	public:
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
		
		ComponentRecord* EnsureCompRecord(EntityID id)
		{
			auto it = compRecordMap.find(StripGeneration(id));
			if (it != compRecordMap.end())
				return it->second;

			ComponentRecord* ret = compRecordPool.Requset();
			ret->recordID = compRecordPool.GetLastID();
			compRecordMap[StripGeneration(id)] = ret;
			return ret;
		}

		void RemoveCompRecord(EntityID id, ComponentRecord* compRecord)
		{
			ComponentRecord record = *compRecord;
			compRecordPool.Remove(compRecord->recordID);
			compRecordMap.erase(StripGeneration(id));

			for (auto kvp : record.addRefs)
				kvp.second->ClearEdge(id, true);
			for (auto kvp : record.removeRefs)
				kvp.second->ClearEdge(id, false);
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

		bool MergeIDToEntityType(EntityType& entityType, EntityID compID)
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

		ComponentRecord* FindCompRecord(EntityID id)
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
			RegisterComponent<InfoComponent>();
			RegisterComponent<NameComponent>();
		}

		void InitBuildInComponents()
		{
			InfoComponent::componentID = 1;
			NameComponent::componentID = 2;

			// Create build-in table for build-in components
			EntityTable* table = nullptr;
			{
				std::vector<EntityID> compIDs = {
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

			// Set component type action
			Reflect::ReflectInfo info = {};
			info.ctor = DefaultCtor;
			SetComponentAction(InfoComponent::GetComponentID(), info);

			info.ctor = Reflect::Ctor<NameComponent>();
			info.dtor = Reflect::Dtor<NameComponent>();
			info.copy = Reflect::Copy<NameComponent>();
			info.move = Reflect::Move<NameComponent>();
			SetComponentAction(NameComponent::GetComponentID(), info);

			lastComponentID = FirstUserComponentID;
			lastID = FirstUserEntityID;
		}

		template<typename C>
		C* GetComponentMutableByID(EntityID entity, EntityID compID, bool* added)
		{
			return static_cast<C*>(GetComponentMutableByID(entity, compID, added));
		}

		EntityID CreateNewComponentID()
		{
			EntityID ret = INVALID_ENTITY;
			if (lastComponentID < HI_COMPONENT_ID)
			{
				do {
					ret = lastComponentID++;
				} while (EntityIDAlive(ret) != INVALID_ENTITY && ret <= HI_COMPONENT_ID);
			}

			if (ret == INVALID_ENTITY || ret >= HI_COMPONENT_ID)
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

		void* GetComponentMutableByID(EntityID entity, EntityID compID, bool* added)
		{
			EntityInternalInfo internalInfo = {};
			void* ret = GetComponentMutable(entity, compID, &internalInfo, added);
			assert(ret != nullptr);
			return ret;
		}

		void* GetComponentMutable(EntityID entity, EntityID compID, EntityInternalInfo* info, bool* isAdded)
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
				assert(info->table != nullptr && info->table->storageTable != nullptr);
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
			void* dst = GetComponentMutable(entity, compID, &info, NULL);
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
			EntityTable* ret = tables.Requset();
			assert(ret != nullptr);
			ret->tableID = tables.GetLastID();
			ret->type = entityType;
			if (!ret->InitTable(this))
			{
				assert(0);
				return nullptr;
			}

			tableTypeHashMap.insert(std::make_pair(EntityTypeHash(entityType), ret));
			return ret;
		}

		EntityTable* FindOrCreateTableWithIDs(const std::vector<EntityID>& compIDs)
		{
			auto it = tableTypeHashMap.find(EntityTypeHash(compIDs));
			if (it != tableTypeHashMap.end())
				return it->second;

			return CreateNewTable(compIDs);
		}

		EntityTable* FindOrCreateTableWithID(EntityTable* node, EntityID compID, EntityGraphEdge* edge)
		{
			assert(node != nullptr);

			// Merge current EntityType and compID to a new EntityType
			EntityType entityType = node->type;
			if (!MergeIDToEntityType(entityType, compID))
			{
				assert(0);	// Debug
				return nullptr;
			}

			if (entityType.empty())
				return &root;

			auto it = tableTypeHashMap.find(EntityTypeHash(entityType));
			if (it != tableTypeHashMap.end())
				return it->second;

			auto ret = CreateNewTable(entityType);
			assert(ret);

			// Compute table diff (Call PopulateTableDiff to get all diffs)
			ComputeTableDiff(node, ret, edge);

			// Add table ref
			if (node != ret)
				TableRegisterAddRef(node, compID);

			return ret;
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
			assert(compID != 0);

			EntityTable* node = table != nullptr ? table : &root;
			EntityGraphEdge* edge = node->FindOrCreateEdge(node->graphNode.add, compID);
			assert(edge != nullptr);
			if (edge->next == nullptr)
			{
				edge->next = FindOrCreateTableWithID(node, compID, edge);
				assert(edge->next != nullptr);
			}

			PopulateTableDiff(node, edge, compID, INVALID_ENTITY, diff);
			return edge->next;
		}

		void SetTableEmpty(EntityTable* table)
		{
			EntityTable** tablePtr = pendingTables.Ensure(table->tableID);
			assert(tablePtr != nullptr);
			(*tablePtr) = table;
		}

		void AppendTableDiff(EntityTableDiff& dst, EntityTableDiff& src)
		{
			dst.added.insert(dst.added.end(), src.added.begin(), src.added.end());
			dst.removed.insert(dst.removed.end(), src.removed.begin(), src.removed.end());
		}

		void ComputeTableDiff(EntityTable* t1, EntityTable* t2, EntityGraphEdge* edge)
		{
			if (t1 == t2)
				return;

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

			trivialEdge &= (addedCount + removedCount) < 1;
			if (trivialEdge)
			{
				edge->diffIndex = -1;
				return;
			}
			else
			{
				EntityTableDiff& diff = t1->graphNode.diffs.emplace_back();
				edge->diffIndex = (I32)t1->graphNode.diffs.size();
				if (addedCount > 0)
					diff.added.reserve(addedCount);
				if (removedCount > 0)
					diff.removed.reserve(removedCount);

				for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
				{
					EntityID srcComponentID = t1->storageType[srcColumnIndex];
					EntityID dstComponentID = t2->storageType[dstColumnIndex];
					if (srcComponentID < dstComponentID)
						diff.removed.push_back(srcComponentID);
					else if (srcComponentID > dstComponentID)
						diff.added.push_back(dstComponentID);

					srcColumnIndex += srcComponentID <= dstComponentID;
					dstColumnIndex += dstComponentID <= srcComponentID;
				}

				for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
					diff.removed.push_back(t1->storageType[srcColumnIndex]);
				for (; dstColumnIndex < dstNumColumns; dstColumnIndex++)
					diff.added.push_back(t2->storageType[dstColumnIndex]);

				assert(diff.added.size() == addedCount);
				assert(diff.removed.size() == removedCount);
			}
		}

		void PopulateTableDiff(EntityTable* table, EntityGraphEdge* edge, EntityID addID, EntityID removeID, EntityTableDiff& diff)
		{
			assert(table != nullptr);
			assert(edge != nullptr);
			if (edge->diffIndex > 0)
			{
				diff = table->graphNode.diffs[edge->diffIndex - 1];
			}
			else
			{
				if (addID != INVALID_ENTITY)
					diff.added.push_back(addID);

				if (removeID != INVALID_ENTITY)
					diff.removed.push_back(removeID);
			}
		}

		CompTableRecord* GetTableRecord(EntityTable* table, EntityID compID)
		{
			ComponentRecord* compRecord = FindCompRecord(compID);
			if (compRecord == nullptr)
				return nullptr;

			return GetTableRecordFromCache(&compRecord->cache, *table);
		}

		void TableRegisterAddRef(EntityTable* table, EntityID id)
		{
			ComponentRecord* compRecord = EnsureCompRecord(id);
			assert(compRecord != nullptr);
			compRecord->addRefs[id] = table;
		}

		void TableRegisterRemoveRef(EntityTable* table, EntityID id)
		{
			// TODO...
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
					void* srcMem = srcColumnData->data.Get(srcColumnData->size, srcColumnData->alignment, srcColumnIndex);
					void* dstMem = dstColumnData->data.Get(dstColumnData->size, dstColumnData->alignment, dstColumnIndex);

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

		////////////////////////////////////////////////////////////////////////////////
		//// TableCache
		////////////////////////////////////////////////////////////////////////////////
		CompTableRecord* GetTableRecordFromCache(EntityTableCache* cache, const EntityTable& table)
		{
			auto it = cache->tableRecordMap.find(table.tableID);
			if (it == cache->tableRecordMap.end())
				return nullptr;
			return reinterpret_cast<CompTableRecord*>(it->second);
		}

		CompTableRecord* InsertTableIntoCache(EntityTableCache* cache, const EntityTable* table)
		{
			assert(cache != nullptr);
			assert(table != nullptr);
			bool empty = table->entities.empty();
			CompTableCacheListNode* node = static_cast<CompTableCacheListNode*>(malloc(sizeof(CompTableRecord)));
			assert(node != nullptr);
			node->table = (EntityTable*)(table);
			node->empty = empty;

			cache->tableRecordMap[table->tableID] = node;
			InsertTableCacheNode(empty ? cache->emptyRecords : cache->records, node);
			return reinterpret_cast<CompTableRecord*>(node);
		}

		bool RemoveTableFromCache(EntityTableCache* cache, EntityTable* table)
		{
			auto it = cache->tableRecordMap.find(table->tableID);
			if (it == cache->tableRecordMap.end())
				return false;

			CompTableCacheListNode* node = it->second;
			if (node == nullptr)
				return false;

			RemoveTableCacheNode(node->empty ? cache->emptyRecords : cache->records, node);
			free(node);

			cache->tableRecordMap.erase(table->tableID);
			return true;
		}

		void InsertTableCacheNode(CompTableCacheList& list, CompTableCacheListNode* node)
		{
			CompTableCacheListNode* last = list.last;
			list.last = node;
			list.count++;
			if (list.count == 1)
				list.first = node;

			node->next = nullptr;
			node->prev = last;

			if (last != nullptr)
				last->next = node;
		}

		void RemoveTableCacheNode(CompTableCacheList& list, CompTableCacheListNode* node)
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
				for (int i = 0; i < tables.Count(); i++)
				{
					EntityTable* table = tables.Get(i);
					NotifyTable(table, ent);
				}
			}
			else
			{
				EntityTable* table = tables.Get(tableID);
				if (table != nullptr)
					NotifyTable(table, ent);
			}
		}
	};

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

		//  Register table
		ForEachEntityID(world, this, RegisterTable);

		// Init storage table
		std::vector<EntityID> storageIDs;
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
			if (compInfo == nullptr || compInfo->size <= 0)
				continue;

			storageIDs.push_back(id);
		}
		if (storageIDs.size() > 0)
		{
			if (storageIDs.size() != type.size())
			{
				assert(0);
				// TODO
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

		// Finit data
		FiniData(true, true);

		if (!isRoot)
		{
			//  Unregister table
			ForEachEntityID(world, this, UnregisterTable);

			// Remove from hashMap
			world->tableTypeHashMap.erase(EntityTypeHash(type));
		}

		world->tables.Remove(tableID);
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

	EntityGraphEdge* EntityTable::FindOrCreateEdge(EntityGraphEdges& egdes, EntityID compID)
	{
		EntityGraphEdge* edge = nullptr;
		if (compID < HI_COMPONENT_ID)
		{
			edge = &egdes.loEdges[compID];
		}
		else
		{
			auto it = egdes.hiEdges.find(compID);
			if (it != egdes.hiEdges.end())
			{
				edge = &it->second;
			}
			else
			{
				auto it = egdes.hiEdges.emplace(compID, EntityGraphEdge());
				edge = &it.first->second;
			}
		}
		return edge;
	}

	EntityGraphEdge* EntityTable::FindEdge(EntityGraphEdges& egdes, EntityID compID)
	{
		EntityGraphEdge* edge = nullptr;
		if (compID < HI_COMPONENT_ID)
		{
			edge = &egdes.loEdges[compID];
		}
		else
		{
			auto it = egdes.hiEdges.find(compID);
			if (it != egdes.hiEdges.end())
				edge = &it->second;
		}
		return edge;
	}

	void EntityTable::ClearEdge(EntityID compID, bool isAdded)
	{
		EntityGraphEdge* edge = nullptr;
		if (isAdded)
			edge = FindEdge(graphNode.add, compID);
		else
			edge = FindEdge(graphNode.remove, compID);
		
		if (edge != nullptr)
		{
			edge->next = nullptr;
			edge->diffIndex = 0;
		}
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
		// if (count == 0)
		//	SetTableEmpty(table);

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

	void EntityTable::GrowColumn(std::vector<EntityID>& entities, ComponentColumnData& columnData, ComponentTypeInfo* compTypeInfo, size_t addCount, size_t newCapacity, bool construct)
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

		return index;
	}

	bool RegisterTable(WorldImpl* world, EntityTable* table, EntityID id, I32 column)
	{
		// Create new table record (from compID record cache)
		ComponentRecord* compRecord = world->EnsureCompRecord(id);
		assert(compRecord != nullptr);

		// Create table record
		CompTableRecord* tableRecord = world->GetTableRecordFromCache(&compRecord->cache, *table);
		if (tableRecord != nullptr)
		{
			tableRecord->count++;
		}
		else
		{
			tableRecord = world->InsertTableIntoCache(&compRecord->cache, table);
			tableRecord->table = table;
			tableRecord->column = column;	// Index for component from entity type
			tableRecord->count = 1;
		}
		return true;
	}

	bool UnregisterTable(WorldImpl* world, EntityTable* table, EntityID id, I32 column)
	{
		ComponentRecord* compRecord = world->FindCompRecord(id);
		if (compRecord == nullptr)
			return false;

		EntityTableCache& cache = compRecord->cache;
		auto it = compRecord->cache.tableRecordMap.find(table->tableID);
		if (it == compRecord->cache.tableRecordMap.end())
			return false;

		if (world->RemoveTableFromCache(&cache, table))
		{
			// Remove the component id
			world->RemoveCompRecord(id, compRecord);
		}
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

	std::unique_ptr<World> World::Create()
	{
		return std::make_unique<WorldImpl>();
	}
}