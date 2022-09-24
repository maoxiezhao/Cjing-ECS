#include "ecs_priv_types.h"
#include "ecs_reflect.h"
#include "ecs_world.h"
#include "ecs_table.h"
#include "ecs_system.h"
#include "ecs_query.h"
#include "ecs_observer.h"
#include "ecs_pipeline.h"
#include "ecs_stage.h"
#include "ecs_entity.h"

namespace ECS
{

	////////////////////////////////////////////////////////////////////////////////
	//// Definition
	////////////////////////////////////////////////////////////////////////////////

	const EntityID EcsRolePair = ((0x01ull) << 56);
	const EntityID EcsRoleShared = ((0x02ull) << 56);

	EntityID BuiltinComponentID = 1;
#define BUILTIN_COMPONENT_ID (BuiltinComponentID++)

	EntityID ECS_ENTITY_ID(InfoComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(NameComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(SystemComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(PipelineComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(TriggerComponent) = BUILTIN_COMPONENT_ID;
	EntityID ECS_ENTITY_ID(ObserverComponent) = BUILTIN_COMPONENT_ID;

	const EntityID EcsCompSystem = ECS_ENTITY_ID(SystemComponent);

	const size_t ENTITY_PAIR_FLAG = EcsRolePair;

	inline EntityID StripGeneration(EntityID id)
	{
		if (id & ECS_ROLE_MASK)
			return id;

		return id & (~ECS_GENERATION_MASK);
	}

	inline void DefaultCtor(void* ptr, size_t count, const ComponentTypeInfo* info)
	{
		memset(ptr, 0, info->size * count);
	}

	inline void DefaultCopyCtor(const void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		auto* hooks = &info->hooks;
		hooks->ctor(dstPtr, count, info);
		hooks->copy(srcPtr, dstPtr, count, info);
	}

	inline void DefaultMoveCtor(void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		auto* hooks = &info->hooks;
		hooks->ctor(dstPtr, count, info);
		hooks->move(srcPtr, dstPtr, count, info);
	}

	inline void DefaultMove(void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		auto* hooks = &info->hooks;
		hooks->move(srcPtr, dstPtr, count, info);
	}

	inline void DefaultMoveDtor(void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		auto* hooks = &info->hooks;
		hooks->move(srcPtr, dstPtr, count, info);
		hooks->dtor(srcPtr, count, info);
	}

	inline void DefaultNoMoveDtor(void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		auto* hooks = &info->hooks;
		hooks->dtor(dstPtr, count, info);
		memcpy(dstPtr, srcPtr, info->size * count);
	}

	////////////////////////////////////////////////////////////////////////////////
	//// Builtin ids
	////////////////////////////////////////////////////////////////////////////////
	const U32 FirstUserComponentID = 32;               // [32 - 256] user components	
	const U32 FirstUserEntityID = HiComponentID + 128; // [256 - 384] builtin tags

	EntityID BuiltinEntityID = HiComponentID;
	#define BUILTIN_ENTITY_ID (BuiltinEntityID++)

	// Core
	const EntityID EcsEcs = BUILTIN_ENTITY_ID;

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
#define BuiltinCompCopy(type) type##_copy
#define BuiltinCompMove(type) type##_move

	static void BuiltinCompDtor(NameComponent)(void* ptr, size_t count, const ComponentTypeInfo* info)
	{
		NameComponent* comps = static_cast<NameComponent*>(ptr);
		if (comps->name)
			ECS_FREE(comps->name);
	}

	static void BuiltinCompCopy(NameComponent)(const void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		const NameComponent* srcComp = static_cast<const NameComponent*>(srcPtr);
		NameComponent* dstComp = static_cast<NameComponent*>(dstPtr);
		ECS_STRSET(&dstComp->name, srcComp->name);
		dstComp->hash = srcComp->hash;
	}

	static void BuiltinCompMove(NameComponent)(void* srcPtr, void* dstPtr, size_t count, const ComponentTypeInfo* info)
	{
		NameComponent* srcComp = static_cast<NameComponent*>(srcPtr);
		NameComponent* dstComp = static_cast<NameComponent*>(dstPtr);
		ECS_STRSET(&dstComp->name, NULL);
		dstComp->name = srcComp->name;
		dstComp->hash = srcComp->hash;

		srcComp->name = NULL;
		srcComp->hash = 0;
	}

	static void BuiltinCompDtor(TriggerComponent)(void* ptr, size_t count, const ComponentTypeInfo* info)
	{
		TriggerComponent* comps = static_cast<TriggerComponent*>(ptr);
		for (int i = 0; i < count; i++)
		{
			if (comps[i].trigger != nullptr)
				FiniTrigger(comps[i].trigger);
		}
	}

	static void BuiltinCompDtor(ObserverComponent)(void* ptr, size_t count, const ComponentTypeInfo* info)
	{
		ObserverComponent* comps = static_cast<ObserverComponent*>(ptr);
		for (int i = 0; i < count; i++)
		{
			if (comps[i].observer != nullptr)
				FiniObserver(comps[i].observer);
		}
	}

	const U32 ECS_DEFINE_OBEJCT(WorldImpl) = 0x63632367;
	const U32 ECS_DEFINE_OBEJCT(Stage) = 0x63632368;

	////////////////////////////////////////////////////////////////////////////////
	//// SystemAPI
	////////////////////////////////////////////////////////////////////////////////

	EcsSystemAPI ecsSystemAPI = {};
	bool isSystemInit = false;

	static void* EcsSystemAPICalloc(size_t size)
	{
		return calloc(1, size);
	}

	static char* EcsSystemAPIStrdup(const char* str) 
	{
		if (str) 
		{
			int len = strlen(str);
			char* result = (char*)ECS_MALLOC(len + 1);
			ECS_ASSERT(result != NULL);
			strcpy_s(result, len + 1, str);
			return result;
		}
		else 
		{
			return NULL;
		}
	}

	void DefaultSystemAPI(EcsSystemAPI& api)
	{
		api.malloc_ = malloc;
		api.calloc_ = EcsSystemAPICalloc;
		api.realloc_ = realloc;
		api.free_ = free;
		api.strdup_ = EcsSystemAPIStrdup;
	}

	void SetSystemAPI(const EcsSystemAPI& api)
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

	void AddComponentForEntity(WorldImpl* world, EntityID entity, EntityInfo* info, EntityID compID)
	{
		EntityTableDiff diff = {};
		EntityTable* srcTable = info->table;
		EntityTable* dstTable = TableTraverseAdd(world, srcTable, compID, diff);
		CommitTables(world, entity, info, dstTable, diff, true);
	}

	void AddComponentForEntity(WorldImpl* world, EntityID entity, EntityID compID)
	{
		world = GetWorld(world);
		EntityInfo* info = world->entityPool.Ensure(entity);
		AddComponentForEntity(world, entity, info, compID);
	}

	EntityID InitNewComponent(WorldImpl* world, const ComponentCreateDesc& desc)
	{
		ECS_ASSERT(ECS_CHECK_OBJECT(&world->base, WorldImpl));

		EntityID entityID = CreateEntityID(world, desc.entity);
		if (entityID == INVALID_ENTITYID)
			return INVALID_ENTITYID;

		InfoComponent* info = static_cast<InfoComponent*>(GetMutableComponent(world, entityID, ECS_ENTITY_ID(InfoComponent)));
		if (info == nullptr)
			return INVALID_ENTITYID;

		info->size = desc.size;
		info->algnment = desc.alignment;

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
			if (obj != INVALID_ENTITYID)
			{
				obj = GetAliveEntity(world, obj);
				ECS_ASSERT(obj != INVALID_ENTITYID);
			}
		}
		return ret;
	}

	ComponentRecord* EnsureComponentRecord(WorldImpl* world, EntityID compID)
	{
		world = GetWorld(world);
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
		world = GetWorld(world);
		if (FreeComponentRecord(compRecord))
			world->compRecordMap.erase(StripGeneration(id));
	}

	ComponentRecord* GetComponentRecord(WorldImpl* world, EntityID id)
	{
		world = GetWorld(world);
		auto it = world->compRecordMap.find(StripGeneration(id));
		if (it == world->compRecordMap.end())
			return nullptr;
		return it->second;
	}

	void* GetMutableComponentImpl(WorldImpl* world, EntityID entity, EntityID compID, EntityInfo* info)
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

			// Flush defer queue, so we can fetch a stable component
			EndDefer(world);
			BeginDefer(world);

			ECS_ASSERT(info != nullptr);
			ECS_ASSERT(info->table != nullptr);
			ret = GetComponentFromTable(world , *info->table, info->row, compID);
		}

		return ret;
	}

	void* GetMutableComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(IsEntityValid(world, entity));

		auto stage = GetStageFromWorld(&world);
		void* out = nullptr;
		if (DeferSet(world, stage, entity, EcsOpMut, compID, 0, nullptr, &out))
			return out;

		EntityInfo* info = world->entityPool.Ensure(entity);
		void* comp = GetMutableComponentImpl(world, entity, compID, info);
		ECS_ASSERT(comp != nullptr);

		EndDefer(world);
		return comp;
	}

	InfoComponent* GetComponentInfo(WorldImpl* world, EntityID compID)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(compID != INVALID_ENTITYID);
		world = GetWorld(world);
		return (InfoComponent*)(GetComponent(world, compID, ECS_ENTITY_ID(InfoComponent)));
	}

	bool IsCompIDValid(EntityID id)
	{
		if (id == INVALID_ENTITYID)
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

	void AddComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(IsEntityValid(world, entity));
		ECS_ASSERT(IsCompIDValid(compID));
	
		Stage* stage = GetStageFromWorld(&world);
		if (DeferAddRemoveID(world, stage, entity, EcsOpAdd, compID))
			return;
		
		AddComponentForEntity(world, entity, compID);

		EndDefer(world);
	}

	void RemoveComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(IsEntityValid(world, entity));
		ECS_ASSERT(IsCompIDValid(compID));

		Stage* stage = GetStageFromWorld(&world);
		if (DeferAddRemoveID(world, stage, entity, EcsOpRemove, compID))
			return;

		EntityInfo* info = world->entityPool.Get(entity);
		if (info == nullptr || info->table == nullptr)
		{
			EndDefer(world);
			return;
		}

		EntityTableDiff diff = {};
		EntityTable* newTable = TableTraverseRemove(world, info->table, compID, diff);
		CommitTables(world, entity, info, newTable, diff, true);

		EndDefer(world);
	}

	const void* GetComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(IsEntityValid(world, entity));

		world = GetWorld(world);

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
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(IsEntityValid(world, entity));
		ECS_ASSERT(compID != INVALID_ENTITYID);

		world = GetWorld(world);

		EntityTable* table = GetTable(world, entity);
		if (table == nullptr)
			return false;

		return TableSearchType(table, compID) != -1;
	}

	void ModifiedComponent(WorldImpl* world, EntityID entity, EntityID compID)
	{
		ECS_ASSERT(world != nullptr);
		auto stage = GetStageFromWorld(&world);
		if (DeferModified(world, stage, entity, compID))
			return;

		EntityInfo* info = world->entityPool.Get(entity);
		if (info->table != nullptr)
		{
			// Notify OnSetEvent
			TableNotifyOnSet(world, info->table, info->row, 1, compID);

			// Table column dirty
			info->table->SetColumnDirty(compID);
		}

		EndDefer(world);
	}

	I32 CountComponent(WorldImpl* world, EntityID compID)
	{
		ECS_ASSERT(world != nullptr);
		if (compID == ECS::INVALID_ENTITYID)
			return 0;

		I32 count = 0;
		Term term = { compID };
		auto it = GetTermIterator(world, term);
		it.flags |= IteratorFlagIsFilter;

		while (NextTermIter(&it)) {
			count += (I32)it.count;
		}
		return count;
	}

	void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove)
	{
		auto stage = GetStageFromWorld(&world);

		EntityInfo* info = world->entityPool.Ensure(entity);
		if (DeferSet(world, stage, entity, EcsOpSet, compID, size, ptr, nullptr))
			return;

		void* dst = GetMutableComponentImpl(world, entity, compID, info);
		ECS_ASSERT(dst != NULL);
		if (ptr)
		{
			ComponentTypeInfo* compTypeInfo = GetComponentTypeInfo(world, compID);
			if (compTypeInfo != nullptr)
			{
				if (isMove)
				{
					if (compTypeInfo->hooks.move != nullptr)
						compTypeInfo->hooks.move((void*)ptr, dst, 1, compTypeInfo);
					else
						memcpy(dst, ptr, size);
				}
				else
				{
					if (compTypeInfo->hooks.copy != nullptr)
						compTypeInfo->hooks.copy(ptr, dst, 1, compTypeInfo);
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

		// Table column dirty
		info->table->SetColumnDirty(compID);

		EndDefer(world);
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
		world = GetWorld(world);
		return world->compTypePool.Ensure(compID);
	}

	// Set component type info for component id
	void SetComponentTypeInfo(WorldImpl* world, EntityID compID, const ComponentTypeHooks& info)
	{
		world = GetWorld(world);

		ComponentTypeInfo* compTypeInfo = world->compTypePool.Ensure(compID);
		ECS_ASSERT(compTypeInfo != nullptr);
		ECS_ASSERT(compTypeInfo->compID == ECS::INVALID_ENTITYID || compTypeInfo->compID == compID);

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
		
		// Set base info
		compTypeInfo->compID = compID;
		compTypeInfo->size = size;
		compTypeInfo->alignment = alignment;

		// Set type hooks
		compTypeInfo->hooks = info;	

		// Set default constructor
		if (!info.ctor && (info.dtor || info.copy || info.move))
			compTypeInfo->hooks.ctor = DefaultCtor;

		// Set default copy constructor
		if (info.copy && !info.copyCtor)
			compTypeInfo->hooks.copyCtor = DefaultCopyCtor;

		// Set default move constructor
		if (info.move && !info.moveCtor)
			compTypeInfo->hooks.moveCtor = DefaultMoveCtor;

		// Set default move destructor
		if (!info.moveDtor)
		{
			if (info.move)
			{
				if (info.dtor)
					compTypeInfo->hooks.moveDtor = DefaultMoveDtor;
				else
					compTypeInfo->hooks.moveDtor = DefaultMove;

			}
			else if (info.dtor)
			{
				compTypeInfo->hooks.moveDtor = DefaultNoMoveDtor;
			}
		}
	}

	bool HasComponentTypeInfo(WorldImpl* world, EntityID compID)
	{
		world = GetWorld(world);
		return GetComponentTypeInfo(world, compID) != nullptr;
	}

	ComponentTypeInfo* GetComponentTypeInfo(WorldImpl* world, EntityID compID)
	{
		world = GetWorld(world);
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
		info.dtor = BuiltinCompDtor(NameComponent);
		info.copy = BuiltinCompCopy(NameComponent);
		info.move = BuiltinCompMove(NameComponent);
		info.onSet = [](Iterator* it) 
		{
			ECS_ASSERT(it->world != nullptr);
			WorldImpl* world = GetWorld(it->world);

			EntityID pair = ECS_MAKE_PAIR(EcsRelationChildOf, 0);
			I32 index = TableSearchType(it->table, ECS_MAKE_PAIR(EcsRelationChildOf, EcsPropertyNone));
			if (index >= 0)
				pair = it->table->type[index];

			auto idRecord = GetComponentRecord(world, pair);
			if (idRecord == nullptr)
				return;

			for (int i = 0; i < it->count; i++)
			{
				const char* name = ((NameComponent*)it->ptrs[i])->name;
				idRecord->entityNameMap[Util::HashFunc(name, strlen(name))] = it->entities[i];
			}
		};
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
		world->entityPool.Ensure(EcsEcs);
		SetEntityName(world, EcsEcs, "ECS");

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
			memset(nameComponent, 0, sizeof(NameComponent));
			ECS_STRSET(&nameComponent->name, compName);
			nameComponent->hash = Util::HashFunc(compName, strlen(compName));
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
			DefaultSystemAPI(ecsSystemAPI);
			isSystemInit = true;
		}

		WorldImpl* world = ECS_NEW_OBJECT<WorldImpl>();
		ECS_INIT_OBJECT(world, WorldImpl);

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
		ECS_ASSERT(!world->isReadonly);
		ECS_ASSERT(!world->isFini);
		ECS_ASSERT(world->stages[0].defer == 0);

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

		// Purge deferred operations
		PurgeDefer(world);

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

		isSystemInit = false;
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
}