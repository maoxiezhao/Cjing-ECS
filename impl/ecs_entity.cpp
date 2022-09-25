#include "ecs_priv_types.h"
#include "ecs_reflect.h"
#include "ecs_entity.h"
#include "ecs_world.h"
#include "ecs_table.h"
#include "ecs_system.h"
#include "ecs_query.h"
#include "ecs_observer.h"
#include "ecs_pipeline.h"
#include "ecs_stage.h"

namespace ECS
{
	inline EntityID StripGeneration(EntityID id)
	{
		if (id & ECS_ROLE_MASK)
			return id;

		return id & (~ECS_GENERATION_MASK);
	}

	// Check id is PropertyNone
	bool CheckIDHasPropertyNone(EntityID id)
	{
		return (id == EcsPropertyNone) || (ECS_HAS_ROLE(id, EcsRolePair)
			&& (ECS_GET_PAIR_FIRST(id) == EcsPropertyNone ||
				ECS_GET_PAIR_SECOND(id) == EcsPropertyNone));
	}

	// Get alive entity, is essentially used for PariEntityID
	EntityID GetAliveEntity(WorldImpl* world, EntityID entity)
	{
		// if entity has generation, just check is alived
		// if entity does not have generation, get a new entity with generation

		if (entity == INVALID_ENTITYID)
			return INVALID_ENTITYID;

		if (IsEntityAlive(world, entity))
			return entity;

		// Make sure id does not have generation
		ECS_ASSERT((U32)entity == entity);

		// Get current alived entity with generation
		world = GetWorld(world);
		EntityID current = world->entityPool.GetAliveIndex(entity);
		if (current == INVALID_ENTITYID)
			return INVALID_ENTITYID;

		return current;
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
				return INVALID_ENTITYID;

			InfoComponent* info = GetComponentInfo(world, relation);
			if (info && info->size != 0)
				return relation;

			EntityID object = ECS_GET_PAIR_SECOND(compID);
			if (object != INVALID_ENTITYID)
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
		world = GetWorld(world);
		if (world->isMultiThreaded)
		{
			ECS_ASSERT(world->lastID < UINT_MAX);
			return Util::AtomicIncrement((I64*)&world->lastID);
		}
		else
		{
			return world->entityPool.NewIndex();
		}
	}

	EntityID CreateNewComponentID(WorldImpl* world)
	{
		world = GetWorld(world);
		if (world->isReadonly)
		{
			// Can't create new component id when in multithread
			ECS_ASSERT(GetStageCount(world) <= 1);
		}

		EntityID ret = INVALID_ENTITYID;
		if (world->lastComponentID < HiComponentID)
		{
			do {
				ret = world->lastComponentID++;
			} while (EntityExists(world, ret) != INVALID_ENTITYID && ret <= HiComponentID);
		}

		if (ret == INVALID_ENTITYID || ret >= HiComponentID)
			ret = CreateNewEntityID(world);

		return ret;
	}

	bool EntityTraverseAdd(WorldImpl* world, EntityID entity, EntityID scope, const EntityCreateDesc& desc, bool nameAssigned, bool isNewEntity)
	{
		world = GetWorld(world);

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
			SetEntityPath(world, entity, scope, ".", name);
			ECS_ASSERT(GetEntityName(world, entity) != nullptr);
		}

		return true;
	}

	// In deferred mode, we should add components for entity one by one
	void DeferredAddEntity(WorldImpl* world, Stage* stage, EntityID scope, EntityID entity, const char* name, const EntityCreateDesc& desc, bool newEntity, bool nameAssigned)
	{
		I32 stageCount = GetStageCount(world);
		if (name && !nameAssigned)
		{
			if (stageCount <= 1)
			{
				SuspendReadonlyState state;

				// Running in a single thread, we can just leave readonly mode
				// to be enable to add tow entities with same name
				SuspendReadonly(world, &state);
				SetEntityPath(world, entity, scope, ".", name);
				ResumeReadonly(world, &state);
			}
			else
			{
				// In multithreaded mode we can't leave readonly mode
				// There is a risk to create entities with same name
				SetEntityPath(world, entity, scope, ".", name);
			}
		}
	}

	EntityID CreateEntityID(WorldImpl* world, const EntityCreateDesc& desc)
	{
		ECS_ASSERT(world != nullptr);
		Stage* stage = GetStageFromWorld(&world);
		EntityID scope = stage->scope;

		const char* name = desc.name;
		bool isNewEntity = false;
		bool nameAssigned = false;
		EntityID result = desc.entity;
		if (result == INVALID_ENTITYID)
		{
			if (name != nullptr)
			{
				result = FindEntityByPath(world, scope, ".", name);
				if (result != INVALID_ENTITYID)
					nameAssigned = true;
			}

			if (result == INVALID_ENTITYID)
			{
				if (desc.useComponentID)
					result = CreateNewComponentID(world);
				else
					result = CreateNewEntityID(world);

				isNewEntity = true;
			}
		}
		else
		{
			EnsureEntity(world, result);
		}

		if (stage->defer)
			DeferredAddEntity(world, stage, result, scope, name, desc, isNewEntity, nameAssigned);
		else
			if (!EntityTraverseAdd(world, result, scope, desc, nameAssigned, isNewEntity))
				return INVALID_ENTITYID;

		return result;
	}


	EntityID GetScope(WorldImpl* world)
	{
		ECS_ASSERT(world != nullptr);
		Stage* stage = GetStageFromWorld(&world);
		return stage->scope;
	}

	EntityID SetScope(WorldImpl* world, EntityID scope)
	{
		ECS_ASSERT(world != nullptr);
		Stage* stage = GetStageFromWorld(&world);
		EntityID prev = stage->scope;
		stage->scope = scope;
		return prev;
	}

	EntityID GetParentFromPath(WorldImpl* world, EntityID parent, const char** pathPtr, const char* prefix, bool newEntity)
	{
		bool startFromRoot = false;
		const char* path = *pathPtr;
		if (prefix)
		{
			size_t len = strlen(prefix);
			if (!strncmp(path, prefix, len))
			{
				path += len;
				parent = 0;
				startFromRoot = true;
			}
		}

		if (!startFromRoot && !parent && newEntity) {
			parent = GetScope(world);
		}

		*pathPtr = path;
		return parent;
	}

	bool ElementIsSep(const char** ptr, const char* sep)
	{
		size_t len = strlen(sep);
		if (!strncmp(*ptr, sep, len))
		{
			*ptr += len;
			return true;
		}
		else
		{
			return false;
		}
	}

	EntityID LookupChild(WorldImpl* world, EntityID parent, const char* name)
	{
		ECS_ASSERT(world != nullptr);
		world = GetWorld(world);
		EntityID pair = ECS_MAKE_PAIR(EcsRelationChildOf, parent);
		auto idRecord = GetComponentRecord(world, pair);
		if (idRecord == nullptr)
			return INVALID_ENTITYID;

		auto& hashMap = idRecord->entityNameMap;
		auto it = hashMap.find(Util::HashFunc(name, strlen(name)));
		return it != hashMap.end() ? it->second : INVALID_ENTITYID;
	}

	const char* GetPathElement(const char* path, const char* sep, I32* len)
	{
		int32_t count = 0;
		char ch;
		const char* ptr;
		for (ptr = path; (ch = *ptr); ptr++)
		{
			if (ElementIsSep(&ptr, sep))
				break;
			count++;
		}

		if (len)
			*len = count;

		return count > 0 ? ptr : nullptr;
	}

	EntityID FindEntityByPath(WorldImpl* world, EntityID parent, const char* sep, const char* path)
	{
		ECS_ASSERT(world != nullptr);
		Stage* stage = GetStageFromWorld(&world);
		parent = GetParentFromPath(world, parent, &path, sep, true);

		char buff[ECS_NAME_BUFFER_LENGTH];
		char* elem = buff;
		int32_t len, size = ECS_NAME_BUFFER_LENGTH;

		EntityID ret = parent;
		const char* ptr = path;
		const char* ptrStart = ptr;
		while ((ptr = GetPathElement(ptr, sep, &len)))
		{
			if (len < size)
			{
				memcpy(elem, ptrStart, len);
			}
			else
			{
				if (size == ECS_NAME_BUFFER_LENGTH)
					elem = NULL;

				elem = (char*)ECS_REALLOC(elem, len + 1);
				memcpy(elem, ptrStart, len);
				size = len + 1;
			}

			elem[len] = '\0';
			ptrStart = ptr;

			ret = LookupChild(world, ret, elem);
			if (ret == INVALID_ENTITYID)
				break;
		}

		if (elem != buff)
			ECS_FREE(elem);

		return ret;
	}

	EntityID GetRelationObject(WorldImpl* world, EntityID entity, EntityID relation, U32 index = 0)
	{
		EntityTable* table = GetTable(world, entity);
		if (table == nullptr)
			return INVALID_ENTITYID;

		TableComponentRecord* record = GetTableRecord(world, table, ECS_MAKE_PAIR(relation, EcsPropertyNone));
		if (record == nullptr)
			return INVALID_ENTITYID;

		if (index >= (U32)record->data.count)
			return INVALID_ENTITYID;

		return ECS_GET_PAIR_SECOND(table->type[record->data.column + index]);
	}

	EntityID FindEntityIDByName(WorldImpl* world, const char* name)
	{
		ECS_ASSERT(world != nullptr);
		Stage* stage = GetStageFromWorld(&world);
		return FindEntityByPath(world, stage->scope, ".", name);
	}

	void SetEntityName(WorldImpl* world, EntityID entity, const char* name)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(entity != INVALID_ENTITYID);

		if (name == nullptr)
		{
			RemoveComponent(world, entity, ECS_ENTITY_ID(NameComponent));
			return;
		}

		NameComponent nameComp = {};
		ECS_STRSET(&nameComp.name, name);
		nameComp.hash = Util::HashFunc(name, strlen(name));
		SetComponent(world, entity, ECS_ENTITY_ID(NameComponent), sizeof(NameComponent), &nameComp, false);
		ModifiedComponent(world, entity, ECS_ENTITY_ID(NameComponent));
	}

	const char* GetEntityName(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(IsEntityValid(world, entity));
		const NameComponent* ptr = static_cast<const NameComponent*>(GetComponent(world, entity, ECS_ENTITY_ID(NameComponent)));
		return ptr ? ptr->name : nullptr;
	}

	void GetEntityPath(WorldImpl* world, EntityID parent, EntityID entity, const char* sep, String& buf)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(entity != INVALID_ENTITYID);
		world = GetWorld(world);

		const char* name;
		char tmp[32];
		EntityID cur = INVALID_ENTITYID;
		if (IsEntityValid(world, entity))
		{
			cur = GetRelationObject(world, entity, EcsRelationChildOf, 0);
			if (cur != INVALID_ENTITYID)
			{
				if (cur != parent)
				{
					GetEntityPath(world, parent, cur, sep, buf);
					buf += sep;
				}
			}

			name = GetEntityName(world, entity);
			if (!name || strlen(name) == 0)
			{
				sprintf_s(tmp, 32, "%u", (U32)entity);
				name = tmp;
			}
		}
		else
		{
			sprintf_s(tmp, 32, "%u", (U32)entity);
			name = tmp;
		}

		buf += name;
	}

	String GetEntityPath(WorldImpl* world, EntityID entity)
	{
		String path;
		GetEntityPath(world, INVALID_ENTITYID, entity, ".", path);
		return path;
	}

	EntityID SetEntityPath(WorldImpl* world, EntityID entity, EntityID parent, const char* sep, const char* path)
	{
		ECS_ASSERT(world != nullptr);
		parent = GetParentFromPath(world, parent, &path, sep, entity == INVALID_ENTITYID);

		char buff[ECS_NAME_BUFFER_LENGTH];
		char* elem = buff;
		int32_t len, size = ECS_NAME_BUFFER_LENGTH;
		EntityID cur = parent;
		const char* ptr = path;
		const char* ptrStart = ptr;

		char* name = NULL;
		while ((ptr = GetPathElement(ptr, sep, &len)))
		{
			if (len < size)
			{
				memcpy(elem, ptrStart, len);
			}
			else
			{
				if (size == ECS_NAME_BUFFER_LENGTH)
					elem = NULL;

				elem = (char*)ECS_REALLOC(elem, len + 1);
				memcpy(elem, ptrStart, len);
				size = len + 1;
			}

			elem[len] = '\0';
			ptrStart = ptr;

			EntityID e = LookupChild(world, cur, elem);
			if (e == INVALID_ENTITYID)
			{
				if (name)
					ECS_FREE(name);
				name = ECS_STRDUP(elem);

				bool lastElem = false;
				if (!GetPathElement(ptr, sep, NULL))
				{
					e = entity;
					lastElem = true;
				}

				if (!e)
				{
					if (lastElem)
					{
						EntityID prev = SetScope(world, 0);
						e = CreateNewEntityID(world);
						SetScope(world, prev);
					}
					else
					{
						e = CreateNewEntityID(world);
					}
				}

				if (cur)
					AddComponent(world, e, ECS_MAKE_PAIR(EcsRelationChildOf, cur));

				SetEntityName(world, e, name);
			}

			cur = e;
		}

		if (name)
			ECS_FREE(name);

		if (elem != buff)
			ECS_FREE(elem);

		return cur;
	}

	void DeleteEntity(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(entity != INVALID_ENTITYID);

		auto stage = GetStageFromWorld(&world);
		if (DeferDelete(world, stage, entity))
			return;

		EntityInfo* entityInfo = world->entityPool.Get(entity);
		if (entityInfo != nullptr)
		{
			U64 tableID = 0;
			if (entityInfo->table)
				tableID = entityInfo->table->tableID;

			if (tableID > 0 && world->tablePool.CheckExsist(tableID))
				entityInfo->table->DeleteEntity(entityInfo->row, true);

			entityInfo->row = 0;
			entityInfo->table = nullptr;
			world->entityPool.Remove(entity);
		}

		EndDefer(world);
	}

	const EntityType& GetEntityType(WorldImpl* world, EntityID entity)
	{
		world = GetWorld(world);
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

	void ClearEntity(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(IsEntityValid(world, entity));

		Stage* stage = GetStageFromWorld(&world);
		if (DeferClear(world, stage, entity))
			return;

		EntityInfo* entityInfo = world->entityPool.Get(entity);
		if (entityInfo == nullptr)
			return;

		EntityTable* table = entityInfo->table;
		if (table)
		{
			table->DeleteEntity(entityInfo->row, true);
			entityInfo->table = nullptr;
			entityInfo->row = 0;
		}

		EndDefer(world);
	}

	void EnsureEntity(WorldImpl* world, EntityID entity)
	{
		world = GetWorld(world);
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
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(entity != INVALID_ENTITYID);

		world = GetWorld(world);
		return world->entityPool.Get(entity) != nullptr;
	}

	bool EntityExists(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(entity != INVALID_ENTITYID);
		world = GetWorld(world);
		return world->entityPool.CheckExsist(entity);
	}

	bool IsEntityValid(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(world != nullptr);

		if (entity == INVALID_ENTITYID)
			return false;

		world = GetWorld(world);

		// Entity identifiers should not contain flag bits
		if (entity & ECS_ROLE_MASK)
			return false;

		if (!EntityExists(world, entity))
			return ECS_GENERATION(entity) == 0;

		return IsEntityAlive(world, entity);
	}
}