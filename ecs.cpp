#include "ecs.h"

namespace ECS
{
	namespace
	{
		const U32 FirstUserComponentID = 32;
		const U32 FirstUserEntityID = HI_COMPONENT_ID + 128;;

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
	}

	EntityBuilder::EntityBuilder(World* world_) :
		world(world_)
	{
	}

	World::World()
	{
		// Initialize 
		idRecordMap.reserve(HI_COMPONENT_ID);
		entityPool.SetSourceID(&lastID);
		InitTable(&root);
		
		// Skip index '0'
		U64 id = tables.NewIndex();
		assert(id == 0);

		// Initialize build-in components
		InitBuildInComponents();
		RegisterBuildInComponents();
	}

	World::~World()
	{
	}

	EntityID World::FindEntityIDByName(const char* name)
	{
		// First find from entityNameMap
		auto it = entityNameMap.find(Util::HashFunc(name, strlen(name)));
		if (it != entityNameMap.end())
			return it->second;

		// Init a filter to get all entity which has NameComponent
		// TODO...

		return INVALID_ENTITY;
	}

	EntityID World::EntityIDAlive(EntityID id)
	{
		if (id == INVALID_ENTITY)
			return INVALID_ENTITY;

		if (entityPool.CheckExsist(id))
			return id;

		return false;
	}

	void World::DeleteEntity(EntityID id)
	{
	}

	InfoComponent* World::GetComponentInfo(EntityID compID)
	{
		return nullptr;
	}

	void* World::GetComponent(EntityID entity, EntityID compID)
	{
		EntityInfo* info = entityPool.Get(entity);
		if (info == nullptr || info->table == nullptr)
			return nullptr;

		EntityTable* table = info->table;
		if (compID != INVALID_ENTITY && table->storageTable == nullptr)
			assert(0);
	
		IDRecord* idRecord = FindIDRecord(compID);
		if (idRecord == nullptr)
			return nullptr;

		EntityTableRecord* tableRecord = GetTableCache(&idRecord->cache, *table->storageTable);
		if (tableRecord == nullptr)
			assert(0);

		return GetComponentWFromTable(*table, info->row, tableRecord->column);
	}

	void World::EnsureEntity(EntityID entity)
	{
		// Entity is alived
		if (EntityIDAlive(StripGeneration(entity)) == entity)
			return;

		entityPool.Ensure(entity);
	}

	void* World::GetComponentFromTable(EntityTable& table, I32 row, EntityID compID)
	{


		return nullptr;
	}

	void* World::GetComponentWFromTable(EntityTable& table, I32 row, I32 column)
	{
		assert(column < (I32)table.storageType.size());
		ComponentColumnData& columnData = table.storageColumns[column];
		assert(columnData.size != 0);
		return columnData.data.Get(columnData.size, columnData.alignment, row);
	}

	void World::InitBuildInComponents()
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

			U32 index = TableAppendNewEntity(table, compID, entityInfo, false);
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

		lastComponentID = FirstUserComponentID;
		lastID = FirstUserEntityID;
	}

	EntityID World::CreateEntityID(const EntityDesc& desc)
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

	EntityID World::CreateNewEntityID()
	{
		return entityPool.NewIndex();
	}

	EntityID World::CreateNewComponentID()
	{
		EntityID ret = INVALID_ENTITY;
		if (lastComponentID < HI_COMPONENT_ID)
		{
			do {
				ret = lastComponentID++;
			}
			while (EntityIDAlive(ret) != INVALID_ENTITY && ret <= HI_COMPONENT_ID);
		}

		if (ret == INVALID_ENTITY || ret >= HI_COMPONENT_ID)
			ret = CreateNewEntityID();

		return ret;
	}

	void World::MoveTableEntities(EntityID srcEntity, EntityTable* srcTable, EntityID dstEntity, EntityTable* dstTable, bool construct)
	{
		// Move entites from srcTable to dstTable
		// Always keep the order of ComponentIDs()

		bool sameEntity = srcEntity == dstEntity;
		U32 srcNumColumns = srcTable->storageType.size();
		U32 dstNumColumns = dstTable->storageType.size();
		U32 srcColumnIndex, dstColumnIndex;
		for (srcColumnIndex = 0, dstColumnIndex = 0; (srcColumnIndex < srcNumColumns) && (dstColumnIndex < dstNumColumns); )
		{
			EntityID srcComponentID = srcTable->storageType[srcColumnIndex];
			EntityID dstComponentID = dstTable->storageType[dstColumnIndex];
			if (srcComponentID == dstComponentID)
			{
				ComponentColumnData* srcColumnData = &srcTable->storageColumns[srcColumnIndex];
				ComponentColumnData* dstColumnData = &dstTable->storageColumns[dstColumnIndex];
				void* srcMem = srcColumnData->data.Get(srcColumnData->size, srcColumnData->alignment, srcColumnIndex);
				void* dstMem = dstColumnData->data.Get(dstColumnData->size, dstColumnData->alignment, dstColumnIndex);
				
				assert(srcMem != nullptr);
				assert(dstMem != nullptr);
				
				if (sameEntity)
				{
					bool typeHasMove = false;
					if (typeHasMove)
					{
						// TODO: Call Reflect::Move();
					}
					else
					{
						memcpy(dstMem, srcMem, dstColumnData->size);
					}

				}
				else
				{
					bool typeHasCopy = false;
					if (typeHasCopy)
					{
						// TODO: Call Reflect::Copy();
					}
					else
					{
						memcpy(dstMem, srcMem, dstColumnData->size);
					}
				}
			}
			else
			{
				if (dstComponentID < srcComponentID)
				{
					if (construct)
					{
						// TODO: Call Reflect::Ctor()
					}
				}
				else
				{
					// TODO: Call Reflect::Dtor()
				}
			}

			dstComponentID += dstComponentID <= srcComponentID;
			srcComponentID += dstComponentID >= srcComponentID;
		}

		// Construct remainning columns
		if (construct)
		{
			for (; dstColumnIndex < dstNumColumns; dstColumnIndex++)
			{
				// TODO: Call Reflect::Ctor()
			}
		}

		// Destruct remainning columns
		for (; srcColumnIndex < srcNumColumns; srcColumnIndex++)
		{
			// TODO: Call Reflect::Dtor()
		}
	}

	EntityID World::InitNewComponent(const EntityDesc& desc)
	{
		EntityID entityID = CreateEntityID(desc);
		if (entityID == INVALID_ENTITY)
			return INVALID_ENTITY;

		InfoComponent* info = GetMutableComponentInfo(entityID, InfoComponent::GetComponentID());
		if (info == nullptr)
			return INVALID_ENTITY;

		return entityID;
	}

	InfoComponent* World::GetMutableComponentInfo(EntityID id, EntityID compID)
	{
		return nullptr;
	}

	void* World::GetOrCreateComponent(EntityID entity, EntityID compID)
	{
		bool isAdded = false;

		EntityInternalInfo info = {};
		void* comp = GetComponentMutable(entity, compID, &info, &isAdded);
		assert(comp != nullptr);

		return comp;
	}

	void* World::GetComponentMutable(EntityID entity, EntityID compID, EntityInternalInfo* info, bool* isAdded)
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


	bool World::EntityTraverseAdd(EntityID entity, const EntityDesc& desc, bool nameAssigned, bool isNewEntity)
	{
		EntityTable* srcTable = nullptr, *table = nullptr;
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
		{
			table = TableAppend(table, NameComponent::GetComponentID(), diff);
		}

		// Commit entity table
		if (srcTable != table)
		{
			CombitTables(entity, &info, table, diff);
		}

		return true;
	}

	IDRecord* World::EnsureIDRecord(EntityID id)
	{
		auto it = idRecordMap.find(id);
		if (it != idRecordMap.end())
			return it->second;

		IDRecord* ret = idRecordPool.Requset();
		ret->recordID = idRecordPool.GetLastID();
		return ret;
	}

	EntityTable* World::TableAppend(EntityTable* table, EntityID compID, EntityTableDiff& diff)
	{
		EntityTableDiff tempDiff = {};
		EntityTable* ret = TableTraverseAdd(table, compID, diff);
		assert(ret != nullptr);
		AppendTableDiff(diff, tempDiff);
		return ret;
	}

	EntityTable* World::TableTraverseAdd(EntityTable* table, EntityID compID, EntityTableDiff& diff)
	{
		assert(compID != 0);

		EntityTable* node = table != nullptr ? table : &root;

		// Find target edge by compID
		EntityGraphEdge* edge = FindOrCreateEdge(node->node.add, compID);
		assert(edge != nullptr);

		// Create new table if the table dose not exsist
		if (edge->next == nullptr)
		{
			edge->next = FindOrCreateTableWithID(table, compID, edge);
			assert(edge->next != nullptr);
		}

		PopulateTableDiff(node, edge, compID, INVALID_ENTITY, diff);
		return edge->next;
	}

	void World::AddComponentForEntity(EntityID entity, EntityInternalInfo* info, EntityID compID)
	{
		EntityTableDiff diff = {};
		EntityTable* srcTable = info->table;
		EntityTable* dstTable = TableTraverseAdd(srcTable, compID, diff);
		CombitTables(entity, info, dstTable, diff);
	}

	EntityTable* World::FindOrCreateTableWithIDs(const std::vector<EntityID>& compIDs)
	{
		auto it = tableMap.find(EntityTypeHash(compIDs));
		if (it != tableMap.end())
			return it->second;

		// ComponentIDs => EntityType
		EntityType entityType = compIDs;
		return CreateNewTable(entityType);
	}

	bool World::GetEntityInternalInfo(EntityInternalInfo& internalInfo, EntityID entity)
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

	EntityTable* World::FindOrCreateTableWithID(EntityTable* node, EntityID compID, EntityGraphEdge* edge)
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

		auto it = tableMap.find(EntityTypeHash(entityType));
		if (it != tableMap.end())
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

	EntityTable* World::CreateNewTable(EntityType entityType)
	{
		EntityTable* ret = tables.Requset();
		assert(ret != nullptr);
		ret->tableID = tables.GetLastID();
		ret->type = entityType;

		if (!InitTable(ret))
		{
			assert(0);
			return nullptr;
		}

		tableMap.insert(std::make_pair(EntityTypeHash(entityType), ret));
		return ret;
	}

	EntityGraphEdge* World::FindOrCreateEdge(EntityGraphEdges& egdes, EntityID compID)
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

	EntityTableRecord* World::GetTableCache(EntityTableCache* cache, const EntityTable& table)
	{
		auto it = cache->tableIDMap.find(table.tableID);
		if (it == cache->tableIDMap.end())
			return nullptr;

		EntityTableRecord* ret;
		I32 index = it->second;
		if (index >= 0)
		{
			ret = &cache->records[index];
		}
		else
		{
			assert((-1 * index - 1) >= 0);
			U32 realIndex = (U32)(-1 * index - 1); // real index = -index - 1
			ret = &cache->emptyRecords[realIndex]; 
		}
		return ret;
	}

	void World::MoveTable(EntityTable* srcTable, EntityTable* dstTable)
	{
	}

	void World::DeleteEntityFromTable(EntityTable* table, U32 index, bool destruct)
	{
		assert(table != nullptr);
		U32 count = table->entities.size();
		assert(count > 0);

		// Remove target entity
		EntityID entityToMove = table->entities[count];
		EntityID entityToDelete = table->entities[index];
		table->entities[index] = entityToMove;
		table->entities.pop_back();

		// Remove target entity info ptr
		EntityInfo* entityInfoToMove = table->entityInfos[count];
		table->entityInfos[index] = entityInfoToMove;
		table->entityInfos.pop_back();

		// Retarget row of entity info
		if (index != count && entityInfoToMove != nullptr)
			entityInfoToMove->row = index;

		// Pending empty table
		if (count == 0)
			SetTableEmpty(table);

		if (index == count)
		{
			// Is the last element
			bool hasDestructor = false;
			if (destruct && hasDestructor)
			{
				for (int i = 0; i < table->storageType.size(); i++)
				{
					// Call Reflect::Dtor
				}
			}
			TableRemoveColumnLast(table);
		}
		else
		{
			// Swap target element and last element, then remove last element
			bool hasDestructor = false;
			if (destruct && hasDestructor)
			{
				for (int i = 0; i < table->storageType.size(); i++)
				{
					// Call Reflect::Dtor
				}
			}
			else
			{
				TableRemoveColumn(table, index);
			}
		}
	}

	void World::SetTableEmpty(EntityTable* table)
	{
		(*pendingTables.Ensure(table->tableID)) = table;
	}

	void World::TableRemoveColumnLast(EntityTable* table)
	{
		for (int i = 0; i < table->storageType.size(); i++)
		{
			auto& columnData = table->storageColumns[i];
			columnData.data.RemoveLast();
		}
	}

	void World::TableRemoveColumn(EntityTable* table, U32 index)
	{
		for (int i = 0; i < table->storageType.size(); i++)
		{
			auto& columnData = table->storageColumns[i];
			columnData.data.Remove(columnData.size, columnData.alignment, index);
		}
	}

	EntityTableRecord* World::InsertTableCache(EntityTableCache* cache, const EntityTable& table)
	{
		assert(cache != nullptr);
		bool empty = table.entities.empty();

		I32 recordIndex;
		EntityTableRecord* ret = nullptr;
		if (empty)
		{
			ret = &cache->emptyRecords.emplace_back();
			recordIndex = -(I32)cache->emptyRecords.size();	// [-1, -INF] -index for emptyRecord
		}
		else
		{
			ret = &cache->records.emplace_back();
			recordIndex = (I32)cache->emptyRecords.size();  // [0, INF] +index for record
		}
		cache->tableIDMap[table.tableID] = recordIndex;

		ret->table = (EntityTable*)(&table);
		ret->empty = empty;
		return ret;
	}

	void World::TableRegisterAddRef(EntityTable* table, EntityID id)
	{
		IDRecord* idRecord = EnsureIDRecord(id);
		assert(idRecord != nullptr);
		idRecord->addRefs[id] = table;
	}

	void World::TableRegisterRemoveRef(EntityTable* table, EntityID id)
	{
	}

	U32 World::TableAppendNewEntity(EntityTable* table, EntityID entity, EntityInfo* info, bool construct)
	{
		U32 index = table->entities.size();

		// Add a new entity for table
		table->entities.push_back(entity);
		table->entityInfos.push_back(info);
		
		// ensure that the columns have the same size as the entities and records.
		U32 newCapacity = table->entities.capacity();
		for (int i = 0; i < table->storageType.size(); i++)
		{
			ComponentColumnData& columnData = table->storageColumns[i];
			U32 oldCapacity = columnData.data.GetCapacity();
			bool needRealloc = oldCapacity != newCapacity;
			bool hasLifeCycle = false;
			if (hasLifeCycle && needRealloc)
			{
				assert(0);
				// TODO...
			}
			else
			{
				if (needRealloc)
					columnData.data.Reserve(columnData.size, columnData.alignment, newCapacity);

				void* mem = columnData.data.PushBack(columnData.size, columnData.alignment);
				if (construct)
				{
					// TODO...
				}
			}
		}

		if (index == 0)
			SetTableEmpty(table);

		return index;
	}

	void World::CombitTables(EntityID entity, EntityInternalInfo* info, EntityTable* dstTable, EntityTableDiff& diff)
	{
		EntityTable* srcTable = info->table;
		if (srcTable == dstTable)
			return;

		assert(srcTable != nullptr);
		assert(dstTable != nullptr);

		if (srcTable != nullptr)
		{
			if (!dstTable->type.empty())
			{
				EntityInfo* entityInfo = info->entityInfo;
				assert(entityInfo != nullptr && entityInfo == entityPool.Get(entity));

				// Add a new entity for dstTable (Reserve storage)
				U32 newRow = TableAppendNewEntity(dstTable, entity, entityInfo, false);
				assert(srcTable->entities.size() > info->row);
				if (!srcTable->type.empty())
					MoveTableEntities(entity, srcTable, entity, dstTable, false);

				entityInfo->row = newRow;
				entityInfo->table = dstTable;

				// Remove old table
				DeleteEntityFromTable(srcTable, info->row, false);
				
				info->row = newRow;
				info->table = dstTable;
			}
			else
			{
				// DeleteEntityFromTable(srcTable, info->row, diff);
			}
		}
	}

	void World::AppendTableDiff(EntityTableDiff& dst, EntityTableDiff& src)
	{
		dst.added.insert(dst.added.end(), src.added.begin(), src.added.end());
		dst.removed.insert(dst.removed.end(), src.removed.begin(), src.removed.end());
	}

	void World::ComputeTableDiff(EntityTable* t1, EntityTable* t2, EntityGraphEdge* edge)
	{
		if (t1 == t2)
			return;

		bool trivialEdge = true;


		if (trivialEdge)
		{
			edge->diffIndex = -1;
			return;
		}
	}

	void World::PopulateTableDiff(EntityTable* table, EntityGraphEdge* edge, EntityID addID, EntityID removeID, EntityTableDiff& diff)
	{
		assert(table != nullptr);
		assert(edge != nullptr);

		if (edge->diffIndex > 0)
		{
			assert(0);
			// TODO...
		}
		else
		{
			if (addID != INVALID_ENTITY)
				diff.added.push_back(addID);

			if (removeID != INVALID_ENTITY)
				diff.removed.push_back(removeID);
		}
	}

	bool World::ForEachEntityID(EntityTable* table, EntityIDAction action)
	{
		bool ret = false;
		for (U32 i = 0; i < table->type.size(); i++)
		{
			EntityID id = table->type[i];
			ret |= action(this, table, StripGeneration(id), i);
		}

		return ret;
	}

	bool World::RegisterTable(World* world, EntityTable* table, EntityID id, I32 column)
	{
		// Create new table record (from compID record cache)

		IDRecord* idRecord = world->EnsureIDRecord(id);
		assert(idRecord != nullptr);
		EntityTableRecord* tableRecord = world->GetTableCache(&idRecord->cache, *table);
		if (tableRecord != nullptr)
		{
			tableRecord->count++;
		}
		else
		{
			tableRecord = world->InsertTableCache(&idRecord->cache, *table);
			tableRecord->table = table;
			tableRecord->column = column; // Index for component from entity type
			tableRecord->count = 1;
		}

		return true;
	}

	bool World::InitTable(EntityTable* table)
	{
		assert(table != nullptr);

		// Ensure all ids used exist */
		for (auto& id : table->type)
			EnsureEntity(id);

		//  Register table
		ForEachEntityID(table, RegisterTable);

		// Init storage table
		std::vector<EntityID> storageIDs;
		for (U32 i = 0; i < table->type.size(); i++)
		{
			EntityID id = table->type[i];
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

			const InfoComponent* compInfo = GetComponentInfo(id);
			if (compInfo == nullptr || compInfo->size <= 0)
				continue;

			storageIDs.push_back(id);
		}
		if (storageIDs.size() > 0)
		{
			if (storageIDs.size() != table->type.size())
			{
				assert(0);
				// TODO
			}
			else
			{
				table->storageTable = table;
				table->storageType = table->type;
			}
		}
		// Init storage map
		if (table->typeToStorageMap.empty() || table->storageToTypeMap.empty())
		{
			auto& typeToStorageMap = table->typeToStorageMap;
			auto& storageToTypeMap = table->storageToTypeMap;
			U32 numType = table->type.size();
			U32 numStorageType = table->storageType.size();

			typeToStorageMap.resize(numType);
			storageToTypeMap.resize(numStorageType);

			U32 t, s;
			for (s = 0, t = 0; (t < numType) && (s < numStorageType); )
			{
				EntityID id = table->type[t];
				EntityID storageID = table->storageType[s];
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

			//  Process remainder of type
			for (; (t < numType); t++) {
				typeToStorageMap[t] = -1;
			}
		}

		// Init column datas
		if (table->storageType.size() > 0)
		{
			table->storageColumns.resize(table->storageType.size());
			for (U32 i = 0; i < table->storageType.size(); i++)
			{
				auto& column = table->storageColumns[i];
				EntityID compID = table->storageType[i];
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

				const InfoComponent* compInfo = GetComponentInfo(compID);
				assert(compInfo != nullptr);
				assert(compInfo->size > 0);
				column.size = compInfo->size;
				column.alignment = compInfo->algnment;
			}
		}

		return true;
	}

	bool World::MergeIDToEntityType(EntityType& entityType, EntityID compID)
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

	IDRecord* World::FindIDRecord(EntityID id)
	{
		auto it = idRecordMap.find(id);
		if (it == idRecordMap.end())
			return nullptr;
		return it->second;
	}
}