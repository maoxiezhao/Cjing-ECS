#include "ecs_priv_types.h"
#include "ecs_reflect.h"
#include "ecs_world.h"
#include "ecs_table.h"
#include "ecs_system.h"
#include "ecs_query.h"
#include "ecs_observer.h"
#include "ecs_pipeline.h"

namespace ECS
{

	////////////////////////////////////////////////////////////////////////////////
	//// Definition
	////////////////////////////////////////////////////////////////////////////////

	const EntityID EcsRolePair = ((0x01ull) << 56);
	const EntityID EcsRoleShared = ((0x02ull) << 56);

	const size_t ENTITY_PAIR_FLAG = EcsRolePair;

	inline EntityID StripGeneration(EntityID id)
	{
		if (id & ECS_ROLE_MASK)
			return id;

		return id & (~ECS_GENERATION_MASK);
	}

	inline void DefaultCtor(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		memset(ptr, 0, size * count);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Builtin ids
	////////////////////////////////////////////////////////////////////////////////
	const U32 FirstUserComponentID = 32;               // [32 - 256] user components	
	const U32 FirstUserEntityID = HiComponentID + 128; // [256 - 384] builtin tags

	EntityID BuiltinEntityID = HiComponentID;
	#define BUILTIN_ENTITY_ID (BuiltinEntityID++)

	// properties
	const EntityID EcsPropertyTag = BUILTIN_ENTITY_ID;
	const EntityID EcsPropertyNone = BUILTIN_ENTITY_ID;
	const EntityID EcsPropertyThis = BUILTIN_ENTITY_ID;
	const EntityID EcsPropertyAny = BUILTIN_ENTITY_ID;
	// Tags
	const EntityID EcsTagPrefab = BUILTIN_ENTITY_ID;
	const EntityID EcsTagDisabled = BUILTIN_ENTITY_ID;
	// Events
	const EntityID EcsEventTableEmpty = BUILTIN_ENTITY_ID;
	const EntityID EcsEventTableFill = BUILTIN_ENTITY_ID;
	const EntityID EcsEventOnAdd = BUILTIN_ENTITY_ID;
	const EntityID EcsEventOnRemove = BUILTIN_ENTITY_ID;
	// Relations
	const EntityID EcsRelationIsA = BUILTIN_ENTITY_ID;
	const EntityID EcsRelationChildOf = BUILTIN_ENTITY_ID;

	////////////////////////////////////////////////////////////////////////////////
	//// Builtin components
	////////////////////////////////////////////////////////////////////////////////

#define BuiltinCompDtor(type) type##_dtor

	static void BuiltinCompDtor(TriggerComponent)(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		TriggerComponent* comps = static_cast<TriggerComponent*>(ptr);
		for (int i = 0; i < count; i++)
		{
			if (comps[i].trigger != nullptr)
				FiniTrigger(comps[i].trigger);
		}
	}

	static void BuiltinCompDtor(ObserverComponent)(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		ObserverComponent* comps = static_cast<ObserverComponent*>(ptr);
		for (int i = 0; i < count; i++)
		{
			if (comps[i].observer != nullptr)
				FiniObserver(comps[i].observer);
		}
	}

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

	EntityID BuiltinComponentID = 1;
#define BUILTIN_COMPONENT_ID (BuiltinComponentID++)

	EntityID ECS_ENTITY_ID(InfoComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(NameComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(SystemComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(PipelineComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(TriggerComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(ObserverComponent) = BUILTIN_COMPONENT_ID;

	const EntityID EcsCompSystem = ECS_ENTITY_ID(SystemComponent);

	////////////////////////////////////////////////////////////////////////////////
	//// SystemAPI
	////////////////////////////////////////////////////////////////////////////////

	EcsSystemAPI ecsSystemAPI = {};
	bool isSystemInit = false;

	static void* EcsSystemAPICalloc(size_t size)
	{
		return calloc(1, size);
	}

	void SetSystemDefaultAPI()
	{
		ecsSystemAPI.malloc_ = malloc;
		ecsSystemAPI.calloc_ = EcsSystemAPICalloc;
		ecsSystemAPI.realloc_ = realloc;
		ecsSystemAPI.free_ = free;
	}

	void SetECSSystemAPI(const EcsSystemAPI& api)
	{
		if (isSystemInit == false)
		{
			ecsSystemAPI = api;
			isSystemInit = true;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	//// WorldImpl
	////////////////////////////////////////////////////////////////////////////////

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

	// Get alive entity, is essentially used for PariEntityID
	EntityID GetAliveEntity(WorldImpl* world, EntityID entity)
	{
		// if entity has generation, just check is alived
		// if entity does not have generation, get a new entity with generation

		if (entity == INVALID_ENTITY)
			return INVALID_ENTITY;

		if (IsEntityAlive(world, entity))
			return entity;

		// Make sure id does not have generation
		ECS_ASSERT((U32)entity == entity);

		// Get current alived entity with generation
		EntityID current = world->entityPool.GetAliveIndex(entity);
		if (current == INVALID_ENTITY)
			return INVALID_ENTITY;

		return current;
	}

	InfoComponent* GetComponentInfo(WorldImpl* world, EntityID compID)
	{
		return static_cast<InfoComponent*>(GetComponent(world, compID, ECS_ENTITY_ID(InfoComponent)));
	}

	EntityID GetRealTypeID(WorldImpl* world, EntityID compID)
	{
		if (compID == ECS_ENTITY_ID(InfoComponent) ||
			compID == ECS_ENTITY_ID(NameComponent))
			return compID;

		if (ECS_HAS_ROLE(compID, EcsRolePair))
		{
			EntityID relation = ECS_GET_PAIR_FIRST(compID);
			if (relation == EcsRelationChildOf)
				return 0;

			relation = GetAliveEntity(world, relation);

			// Tag dose not have type info, return zero
			if (HasComponent(world, relation, EcsPropertyTag))
				return INVALID_ENTITY;

			InfoComponent* info = GetComponentInfo(world, relation);
			if (info && info->size != 0)
				return relation;

			EntityID object = ECS_GET_PAIR_SECOND(compID);
			if (object != INVALID_ENTITY)
			{
				object = GetAliveEntity(world, object);
				info = GetComponentInfo(world, object);
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
			InfoComponent* info = GetComponentInfo(world, compID);
			if (!info || info->size == 0)
				return 0;
		}

		return compID;
	}


	EntityID CreateNewEntityID(WorldImpl* world)
	{
		return world->entityPool.NewIndex();
	}

	EntityID CreateNewComponentID(WorldImpl* world)
	{
		EntityID ret = INVALID_ENTITY;
		if (world->lastComponentID < HiComponentID)
		{
			do {
				ret = world->lastComponentID++;
			} while (EntityExists(world, ret) != INVALID_ENTITY && ret <= HiComponentID);
		}

		if (ret == INVALID_ENTITY || ret >= HiComponentID)
			ret = CreateNewEntityID(world);

		return ret;
	}

	bool EntityTraverseAdd(WorldImpl* world, EntityID entity, const EntityCreateDesc& desc, bool nameAssigned, bool isNewEntity)
	{
		EntityTable* srcTable = nullptr, * table = nullptr;
		EntityInfo* info = nullptr;

		// Get existing table
		if (!isNewEntity)
		{
			info = world->entityPool.Get(entity);
			if (info != nullptr)
				table = info->table;
		}

		EntityTableDiff diff = EMPTY_TABLE_DIFF;

		// Add name component
		const char* name = desc.name;
		if (name && !nameAssigned)
			table = TableAppend(world, table, ECS_ENTITY_ID(NameComponent), diff);

		// Commit entity table
		if (srcTable != table)
		{
			CommitTables(world, entity, info, table, diff, true);
		}

		if (name && !nameAssigned)
		{
			SetEntityName(world, entity, name);
			world->entityNameMap[Util::HashFunc(name, strlen(name))] = entity;
		}

		return true;
	}

	EntityID CreateEntityID(WorldImpl* world, const EntityCreateDesc& desc)
	{
		const char* name = desc.name;
		bool isNewEntity = false;
		bool nameAssigned = false;
		EntityID result = desc.entity;
		if (result == INVALID_ENTITY)
		{
			if (name != nullptr)
			{
				result = FindEntityIDByName(world, name);
				if (result != INVALID_ENTITY)
					nameAssigned = true;
			}

			if (result == INVALID_ENTITY)
			{
				if (desc.useComponentID)
					result = CreateNewComponentID(world);
				else
					result = CreateNewEntityID(world);

				isNewEntity = true;
			}
		}

		if (!EntityTraverseAdd(world, result, desc, nameAssigned, isNewEntity))
			return INVALID_ENTITY;

		return result;
	}

	EntityID FindEntityIDByName(WorldImpl* world, const char* name)
	{
		// First find from entityNameMap
		auto it = world->entityNameMap.find(Util::HashFunc(name, strlen(name)));
		if (it != world->entityNameMap.end())
			return it->second;

		// Init a filter to get all entity which has NameComponent
		// TODO...

		return INVALID_ENTITY;
	}

	void SetEntityName(WorldImpl* world, EntityID entity, const char* name)
	{
		NameComponent nameComp = {};
		nameComp.name = _strdup(name);
		nameComp.hash = Util::HashFunc(name, strlen(name));
		SetComponent(world, entity, ECS_ENTITY_ID(NameComponent), sizeof(NameComponent), &nameComp, false);
	}

	const char* GetEntityName(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(IsEntityValid(world, entity));
		const NameComponent* ptr = static_cast<const NameComponent*>(GetComponent(world, entity, ECS_ENTITY_ID(NameComponent)));
		return ptr ? ptr->name : nullptr;
	}

	bool DeferDeleteEntity(WorldImpl* world, EntityID entity)
	{
		// TODO
		if (world->defer > 0)
		{
			// TODO: Add a job into the defer queue
			return true;
		}

		return false;
	}

	void DeleteEntity(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(entity != INVALID_ENTITY);

		if (DeferDeleteEntity(world, entity))
			return;

		EntityInfo* entityInfo = world->entityPool.Get(entity);
		if (entityInfo == nullptr)
			return;

		U64 tableID = 0;
		if (entityInfo->table)
			tableID = entityInfo->table->tableID;

		if (tableID > 0 && world->tablePool.CheckExsist(tableID))
			entityInfo->table->DeleteEntity(entityInfo->row, true);

		entityInfo->row = 0;
		entityInfo->table = nullptr;
		world->entityPool.Remove(entity);
	}

	const EntityType& GetEntityType(WorldImpl* world, EntityID entity)
	{
		const EntityInfo* info = world->entityPool.Get(entity);
		if (info == nullptr || info->table == nullptr)
			return EMPTY_ENTITY_TYPE;

		return info->table->type;
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

	EntityID GetRelationObject(WorldImpl* world, EntityID entity, EntityID relation, U32 index = 0)
	{
		EntityTable* table = GetTable(world, entity);
		if (table == nullptr)
			return INVALID_ENTITY;

		TableComponentRecord* record = GetTableRecord(world, table, ECS_MAKE_PAIR(relation, EcsPropertyNone));
		if (record == nullptr)
			return INVALID_ENTITY;

		if (index >= (U32)record->data.count)
			return INVALID_ENTITY;

		return ECS_GET_PAIR_SECOND(table->type[record->data.column + index]);
	}

	EntityID GetParent(WorldImpl* world, EntityID entity)
	{
		return GetRelationObject(world, entity, EcsRelationChildOf, 0);
	}

	void EnableEntity(WorldImpl* world, EntityID entity, bool enabled)
	{
		if (enabled)
			RemoveComponent(world, entity, EcsTagDisabled);
		else
			AddComponent(world, entity, EcsTagDisabled);
	}

	void EnsureEntity(WorldImpl* world, EntityID entity)
	{
		if (ECS_HAS_ROLE(entity, EcsRolePair))
		{
			EntityID re = ECS_GET_PAIR_FIRST(entity);
			EntityID comp = ECS_GET_PAIR_SECOND(entity);

			if (GetAliveEntity(world, re) != re)
				world->entityPool.Ensure(re);

			if (GetAliveEntity(world, comp) != comp)
				world->entityPool.Ensure(comp);
		}
		else
		{
			if (GetAliveEntity(world, StripGeneration(entity)) == entity)
				return;

			world->entityPool.Ensure(entity);
		}
	}

	bool IsEntityAlive(WorldImpl* world, EntityID entity)
	{
		return world->entityPool.Get(entity) != nullptr;
	}

	bool EntityExists(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(entity != INVALID_ENTITY);
		return world->entityPool.CheckExsist(entity);
	}

	bool IsEntityValid(WorldImpl* world, EntityID entity)
	{
		if (entity == INVALID_ENTITY)
			return false;

		// Entity identifiers should not contain flag bits
		if (entity & ECS_ROLE_MASK)
			return false;

		if (!EntityExists(world, entity))
			return ECS_GENERATION(entity) == 0;

		return IsEntityAlive(world, entity);
	}

	void AddComponentForEntity(WorldImpl* world, EntityID entity, EntityInfo* info, EntityID compID)
	{
		EntityTableDiff diff = {};
		EntityTable* srcTable = info->table;
		EntityTable* dstTable = TableTraverseAdd(world, srcTable, compID, diff);
		CommitTables(world, entity, info, dstTable, diff, true);
	}

	void AddComponentForEntity(WorldImpl* world, EntityID entity, EntityID compID)
	{
		EntityInfo* info = world->entityPool.Ensure(entity);
		AddComponentForEntity(world, entity, info, compID);
	}

	EntityID InitNewComponent(WorldImpl* world, const ComponentCreateDesc& desc)
	{
		EntityID entityID = CreateEntityID(world, desc.entity);
		if (entityID == INVALID_ENTITY)
			return INVALID_ENTITY;

		bool added = false;
		InfoComponent* info = static_cast<InfoComponent*>(GetOrCreateMutableByID(world, entityID, ECS_ENTITY_ID(InfoComponent), &added));
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

		if (entityID >= world->lastComponentID && entityID < HiComponentID)
			world->lastComponentID = (U32)(entityID + 1);

		return entityID;
	}

	void* GetComponentPtrFromTable(EntityTable& table, I32 row, I32 column)
	{
		ECS_ASSERT(column < (I32)table.storageCount);
		ComponentColumnData& columnData = table.storageColumns[column];
		ComponentTypeInfo& typeInfo = table.compTypeInfos[column];
		ECS_ASSERT(typeInfo.size != 0);
		return columnData.Get(typeInfo.size, typeInfo.alignment, row);
	}

	void* GetComponentFromTable(WorldImpl* world, EntityTable& table, I32 row, EntityID compID)
	{
		ECS_ASSERT(compID != 0);
		ECS_ASSERT(row >= 0);

		if (table.storageTable == nullptr)
			return nullptr;

		TableComponentRecord* tableRecord = GetTableRecord(world, table.storageTable, compID);
		if (tableRecord == nullptr)
			return nullptr;

		return GetComponentPtrFromTable(table, row, tableRecord->data.column);
	}

	ComponentRecord* CreateComponentRecord(WorldImpl* world, EntityID compID)
	{
		ComponentRecord* ret = ECS_NEW_OBJECT<ComponentRecord>();
		if (ECS_HAS_ROLE(compID, EcsRolePair))
		{
			EntityID rel = ECS_GET_PAIR_FIRST(compID);
			ECS_ASSERT(rel != 0);

			EntityID obj = ECS_GET_PAIR_SECOND(compID);
			if (obj != INVALID_ENTITY)
			{
				obj = GetAliveEntity(world, obj);
				ECS_ASSERT(obj != INVALID_ENTITY);
			}
		}
		return ret;
	}

	ComponentRecord* EnsureComponentRecord(WorldImpl* world, EntityID compID)
	{
		auto it = world->compRecordMap.find(StripGeneration(compID));
		if (it != world->compRecordMap.end())
			return it->second;

		ComponentRecord* ret = CreateComponentRecord(world, compID);
		world->compRecordMap[StripGeneration(compID)] = ret;
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

	void RemoveComponentRecord(WorldImpl* world, EntityID id, ComponentRecord* compRecord)
	{
		if (FreeComponentRecord(compRecord))
			world->compRecordMap.erase(StripGeneration(id));
	}

	ComponentRecord* GetComponentRecord(WorldImpl* world, EntityID id)
	{
		auto it = world->compRecordMap.find(StripGeneration(id));
		if (it == world->compRecordMap.end())
			return nullptr;
		return it->second;
	}

	void* GetOrCreateMutable(WorldImpl* world, EntityID entity, EntityID compID, EntityInfo* info, bool* isAdded)
	{
		ECS_ASSERT(compID != 0);
		ECS_ASSERT(info != nullptr);
		ECS_ASSERT((compID & ECS_COMPONENT_MASK) == compID || ECS_HAS_ROLE(compID, EcsRolePair));

		void* ret = nullptr;
		if (info->table != nullptr)
			ret = GetComponentFromTable(world, *info->table, info->row, compID);

		if (ret == nullptr)
		{
			AddComponentForEntity(world, entity, info, compID);

			ECS_ASSERT(info != nullptr);
			ECS_ASSERT(info->table != nullptr);
			ret = GetComponentFromTable(world , *info->table, info->row, compID);

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

	void* GetOrCreateMutableByID(WorldImpl* world, EntityID entity, EntityID compID, bool* added)
	{
		EntityInfo* info = world->entityPool.Ensure(entity);
		void* ret = GetOrCreateMutable(world, entity, compID, info, added);
		ECS_ASSERT(ret != nullptr);
		return ret;
	}

	void* GetOrCreateComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		bool isAdded = false;
		void* comp = GetOrCreateMutableByID(world, entity, compID, &isAdded);
		ECS_ASSERT(comp != nullptr);
		return comp;
	}

	void AddComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(IsEntityValid(world, entity));
		ECS_ASSERT(IsCompIDValid(compID));
		AddComponentForEntity(world, entity, compID);
	}

	void RemoveComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(IsEntityValid(world, entity));
		ECS_ASSERT(IsCompIDValid(compID));

		EntityInfo* info = world->entityPool.Get(entity);
		if (info == nullptr || info->table == nullptr)
			return;

		EntityTableDiff diff = {};
		EntityTable* newTable = TableTraverseRemove(world, info->table, compID, diff);
		CommitTables(world, entity, info, newTable, diff, true);
	}

	void* GetComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		EntityInfo* info = world->entityPool.Get(entity);
		if (info == nullptr || info->table == nullptr)
			return nullptr;

		EntityTable* table = info->table;
		if (table->storageTable == nullptr)
			return nullptr;

		TableComponentRecord* tableRecord = GetTableRecord(world, table->storageTable, compID);
		if (tableRecord == nullptr)
			return nullptr;

		return GetComponentPtrFromTable(*info->table, info->row, tableRecord->data.column);
	}

	bool HasComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(compID != INVALID_ENTITY);
		EntityTable* table = GetTable(world, entity);
		if (table == nullptr)
			return false;

		return TableSearchType(table, compID) != -1;
	}

	void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove)
	{
		EntityInfo* info = world->entityPool.Ensure(entity);
		void* dst = GetOrCreateMutable(world, entity, compID, info, NULL);
		ECS_ASSERT(dst != NULL);
		if (ptr)
		{
			ComponentTypeInfo* compTypeInfo = GetComponentTypeInfo(world, compID);
			if (compTypeInfo != nullptr)
			{
				if (isMove)
				{
					if (compTypeInfo->hooks.move != nullptr)
						compTypeInfo->hooks.move(world, &entity, &entity, compTypeInfo->size, 1, (void*)ptr, dst);
					else
						memcpy(dst, ptr, size);
				}
				else
				{
					if (compTypeInfo->hooks.copy != nullptr)
						compTypeInfo->hooks.copy(world, &entity, &entity, compTypeInfo->size, 1, ptr, dst);
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

	void Instantiate(WorldImpl* world, EntityID entity, EntityID prefab)
	{
		AddComponent(world, entity, ECS_MAKE_PAIR(EcsRelationIsA, prefab));
	}

	void ChildOf(WorldImpl* world, EntityID entity, EntityID parent)
	{
		AddComponent(world, entity, ECS_MAKE_PAIR(EcsRelationChildOf, parent));
	}


	ComponentTypeInfo* EnsureComponentTypInfo(WorldImpl* world, EntityID compID)
	{
		return world->compTypePool.Ensure(compID);
	}

	// Set component type info for component id
	void SetComponentTypeInfo(WorldImpl* world, EntityID compID, const ComponentTypeHooks& info)
	{
		ComponentTypeInfo* compTypeInfo = world->compTypePool.Ensure(compID);
		size_t size = compTypeInfo->size;
		size_t alignment = compTypeInfo->alignment;
		if (size == 0)
		{
			InfoComponent* infoComponent = GetComponentInfo(world, compID);
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

	bool HasComponentTypeInfo(WorldImpl* world, EntityID compID)
	{
		return GetComponentTypeInfo(world, compID) != nullptr;
	}

	ComponentTypeInfo* GetComponentTypeInfo(WorldImpl* world, EntityID compID)
	{
		return world->compTypePool.Get(compID);
	}

	const ComponentTypeHooks* GetComponentTypeHooks(WorldImpl* world, EntityID compID)
	{
		auto typeInfo = GetComponentTypeInfo(world, compID);
		if (typeInfo != nullptr)
			return &typeInfo->hooks;
		return nullptr;
	}

	void FiniComponentTypeInfo(ComponentTypeInfo* typeinfo)
	{
		ComponentTypeHooks& hooks = typeinfo->hooks;
		if (hooks.invoker != nullptr && hooks.invokerDeleter != nullptr)
			hooks.invokerDeleter(hooks.invoker);
	}

	void FiniComponentTypeInfos(WorldImpl* world)
	{
		size_t count = world->compTypePool.Count();
		for (size_t i = 0; i < count; i++)
		{
			ComponentTypeInfo* typeinfo = world->compTypePool.GetByDense(i);
			if (typeinfo != nullptr)
				FiniComponentTypeInfo(typeinfo);
		}
	}

	template<typename C>
	void InitBuiltinComponentTypeInfo(WorldImpl* world, EntityID id)
	{
		ComponentTypeInfo* typeInfo = EnsureComponentTypInfo(world, id);
		typeInfo->size = sizeof(C);
		typeInfo->alignment = alignof(C);
	}

	void SetupComponentTypes(WorldImpl* world)
	{
		InitBuiltinComponentTypeInfo<InfoComponent>(world, ECS_ENTITY_ID(InfoComponent));
		InitBuiltinComponentTypeInfo<NameComponent>(world, ECS_ENTITY_ID(NameComponent));
		InitBuiltinComponentTypeInfo<SystemComponent>(world, ECS_ENTITY_ID(SystemComponent));
		InitBuiltinComponentTypeInfo<PipelineComponent>(world, ECS_ENTITY_ID(PipelineComponent));
		InitBuiltinComponentTypeInfo<TriggerComponent>(world, ECS_ENTITY_ID(TriggerComponent));
		InitBuiltinComponentTypeInfo<ObserverComponent>(world, ECS_ENTITY_ID(ObserverComponent));

		// Info component
		ComponentTypeHooks info = {};
		info.ctor = DefaultCtor;
		SetComponentTypeInfo(world, ECS_ENTITY_ID(InfoComponent), info);

		// Name component
		info.ctor = Reflect::Ctor<NameComponent>();
		info.dtor = Reflect::Dtor<NameComponent>();
		info.copy = Reflect::Copy<NameComponent>();
		info.move = Reflect::Move<NameComponent>();
		SetComponentTypeInfo(world, ECS_ENTITY_ID(NameComponent), info);

		// Trigger component
		info = {};
		info.ctor = DefaultCtor;
		info.dtor = BuiltinCompDtor(TriggerComponent);
		SetComponentTypeInfo(world, ECS_ENTITY_ID(TriggerComponent), info);

		// Observer component
		info = {};
		info.ctor = DefaultCtor;
		info.dtor = BuiltinCompDtor(ObserverComponent);
		SetComponentTypeInfo(world, ECS_ENTITY_ID(ObserverComponent), info);
	}

	void InitBuiltinComponents(WorldImpl* world)
	{
		// Create builtin table for builtin components
		EntityTable* table = nullptr;
		{
			Vector<EntityID> compIDs = {
				ECS_ENTITY_ID(InfoComponent),
				ECS_ENTITY_ID(NameComponent)
			};
			table = FindOrCreateTableWithIDs(world, compIDs);

			table->entities.reserve(FirstUserComponentID);
			table->storageColumns[0].Reserve<InfoComponent>(FirstUserComponentID);
			table->storageColumns[1].Reserve<NameComponent>(FirstUserComponentID);
		}

		// Initialize builtin components
		auto InitBuiltinComponent = [&](EntityID compID, U32 size, U32 alignment, const char* compName) {
			EntityInfo* entityInfo = world->entityPool.Ensure(compID);
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

			world->entityNameMap[nameComponent->hash] = compID;
		};

		InitBuiltinComponent(ECS_ENTITY_ID(InfoComponent), sizeof(InfoComponent), alignof(InfoComponent), Util::Typename<InfoComponent>());
		InitBuiltinComponent(ECS_ENTITY_ID(NameComponent), sizeof(NameComponent), alignof(NameComponent), Util::Typename<NameComponent>());
		InitBuiltinComponent(ECS_ENTITY_ID(TriggerComponent), sizeof(TriggerComponent), alignof(TriggerComponent), Util::Typename<TriggerComponent>());
		InitBuiltinComponent(ECS_ENTITY_ID(ObserverComponent), sizeof(ObserverComponent), alignof(ObserverComponent), Util::Typename<ObserverComponent>());

		world->lastComponentID = FirstUserComponentID;
		world->lastID = FirstUserEntityID;
	}

	void InitBuiltinEntites(WorldImpl* world)
	{
		InfoComponent tagInfo = {};
		tagInfo.size = 0;

		// TEMP:
		// Ensure EcsPropertyNone
		world->entityPool.Ensure(EcsPropertyNone);

		auto InitTag = [&](EntityID tagID, const char* name)
		{
			EntityInfo* entityInfo = world->entityPool.Ensure(tagID);
			ECS_ASSERT(entityInfo != nullptr);

			SetComponent(world, tagID, ECS_ENTITY_ID(InfoComponent), sizeof(InfoComponent), &tagInfo, false);
			SetEntityName(world, tagID, name);
		};
		// Property
		InitTag(EcsPropertyTag, "EcsPropertyTag");
		InitTag(EcsPropertyThis, "EcsPropertyThis");
		// Tags
		InitTag(EcsTagPrefab, "EcsTagPrefab");
		InitTag(EcsTagDisabled, "EcsTagDisabled");
		// Relation
		InitTag(EcsRelationIsA, "EcsRelationIsA");
		InitTag(EcsRelationChildOf, "EcsRelationChildOf");
		// Events
		InitTag(EcsEventTableEmpty, "EcsEventTableEmpty");
		InitTag(EcsEventTableFill, "EcsEventTableFill");
		InitTag(EcsEventOnAdd, "EcsEventOnAdd");
		InitTag(EcsEventOnRemove, "EcsEventOnRemove");

		// RelationIsA has peropty of tag
		AddComponent(world, EcsRelationIsA, EcsPropertyTag);
		AddComponent(world, EcsRelationChildOf, EcsPropertyTag);
	}


	WorldImpl* InitWorld()
	{
		if (isSystemInit == false)
		{
			SetSystemDefaultAPI();
			isSystemInit = true;
		}

		WorldImpl* world = ECS_NEW_OBJECT<WorldImpl>();

		world->pendingTables = ECS_NEW_OBJECT<Util::SparseArray<EntityTable*>>();
		world->pendingBuffer = ECS_NEW_OBJECT<Util::SparseArray<EntityTable*>>();

		world->compRecordMap.reserve(HiComponentID);
		world->entityPool.SetSourceID(&world->lastID);
		if (!world->root.InitTable(world))
			ECS_ASSERT(0);

		// Skip id 0
		U64 id = world->tablePool.NewIndex();
		ECS_ASSERT(id == 0);
		id = world->queryPool.NewIndex();
		ECS_ASSERT(id == 0);

		// Create default stage
		SetStageCount(world, 1);

		SetupComponentTypes(world);
		InitBuiltinComponents(world);
		InitBuiltinEntites(world);
		InitSystemComponent(world);
		InitPipelineComponent(world);

		return world;
	}

	void FiniComponentRecords(WorldImpl* world)
	{
		for (auto kvp : world->compRecordMap)
			FreeComponentRecord( kvp.second);

		world->compRecordMap.clear();
	}

	void FiniWorld(WorldImpl* world)
	{
		world->isFini = true;

		// Begin defer to discard all operations (Add/Delete/Dtor)
		BeginDefer(world);

		// Free all tables, neet to skip id 0
		size_t tabelCount = world->tablePool.Count();
		for (size_t i = 1; i < tabelCount; i++)
		{
			EntityTable* table = world->tablePool.GetByDense(i);
			if (table != nullptr)
				table->Release();
		}
		world->tablePool.Clear();
		world->pendingTables->Clear();
		world->pendingBuffer->Clear();

		// Free root table
		world->root.Release();

		// Free graph edges
		Util::ListNode<TableGraphEdge>* cur, * next = world->freeEdge;
		while ((cur = next))
		{
			next = cur->next;
			ECS_FREE(cur);
		}

		// Fini all queries
		FiniQueries(world);

		// Fini component records
		FiniComponentRecords(world);

		// Fini component type infos
		FiniComponentTypeInfos(world);

		// Fini stages
		SetStageCount(world, 0);

		// Clear entity pool
		world->entityPool.Clear();

		ECS_DELETE_OBJECT(world->pendingBuffer);
		ECS_DELETE_OBJECT(world->pendingTables);
		ECS_DELETE_OBJECT(world);
	}

	bool FlushDefer(WorldImpl* world)
	{
		if (--world->defer > 0)
			return false;

		// TODO
		// Flush defer queue
		return true;
	}

	I32 GetStageCount(WorldImpl* world)
	{
		return 0;
	}

	void SetStageCount(WorldImpl* world, I32 stageCount)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT((stageCount > 1 && !world->isFini) || stageCount <= 1);

		if (stageCount == world->stageCount)
			return;

		// Fini existing stages
		if (world->stageCount > 0 && stageCount != world->stageCount)
		{
			for (int i = 0; i < world->stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				ECS_ASSERT(stage->thread == 0);
				FiniStage(world, stage);
			}

			ECS_FREE(world->stages);
		}

		world->stages = nullptr;
		world->stageCount = stageCount;

		// Init new stages
		if (stageCount > 0)
		{
			world->stages = ECS_MALLOC_T_N(Stage, stageCount);
			for (int i = 0; i < stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				InitStage(world, stage);
				stage->id = i;
			}
		}
	}

	void InitStage(WorldImpl* world, Stage* stage)
	{
		ECS_NEW_PLACEMENT(stage, Stage);
		stage->async = false;
		stage->world = world;
	}

	void FiniStage(WorldImpl* world, Stage* stage)
	{
		stage->~Stage();
	}

	void SetThreads(WorldImpl* world, I32 threads, bool startThreads)
	{
		I32 stageCount = GetStageCount(world);
		if (stageCount != threads)
		{
			SetStageCount(world, threads);

			if (threads > 1 && startThreads)
			{
				ECS_ASSERT(false);
				// TODO
			}
		}
	}

	void BeginDefer(WorldImpl* world)
	{
		// TODO
		world->defer++;
	}

	void EndDefer(WorldImpl* world)
	{
		// TODO
		FlushDefer(world);
	}

	Stage* GetStage(WorldImpl* world, I32 stageID)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(world->stageCount > stageID);
		return &world->stages[stageID];
	}

	Stage* GetStageFromWorld(WorldImpl* world)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(world->stageCount <= 1 || !world->isReadonly);
		return &world->stages[0];
	}
}