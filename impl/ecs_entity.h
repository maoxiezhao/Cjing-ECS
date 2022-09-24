#pragma once

#include "ecs_def.h"

namespace ECS
{
	struct WorldImpl;

	EntityID CreateEntityID(WorldImpl* world, const EntityCreateDesc& desc);
	void EnsureEntity(WorldImpl* world, EntityID entity);
	EntityID FindEntityIDByName(WorldImpl* world, const char* name);
	bool EntityExists(WorldImpl* world, EntityID entity);
	bool IsEntityValid(WorldImpl* world, EntityID entity);
	bool IsEntityAlive(WorldImpl* world, EntityID entity);
	bool CheckIDHasPropertyNone(EntityID id);
	void DeleteEntity(WorldImpl* world, EntityID entity);
	EntityID GetAliveEntity(WorldImpl* world, EntityID entity);
	const EntityType& GetEntityType(WorldImpl* world, EntityID entity);
	EntityID GetRealTypeID(WorldImpl* world, EntityID compID);
	bool MergeEntityType(EntityType& entityType, EntityID compID);
	void RemoveFromEntityType(EntityType& entityType, EntityID compID);
	void Instantiate(WorldImpl* world, EntityID entity, EntityID prefab);
	void ChildOf(WorldImpl* world, EntityID entity, EntityID parent);
	void SetEntityName(WorldImpl* world, EntityID entity, const char* name);
	const char* GetEntityName(WorldImpl* world, EntityID entity);
	String GetEntityPath(WorldImpl* world, EntityID entity);
	EntityID GetParent(WorldImpl* world, EntityID entity);
	void EnableEntity(WorldImpl* world, EntityID entity, bool enabled);
	void ClearEntity(WorldImpl* world, EntityID entity);
	EntityID FindEntityByPath(WorldImpl* world, EntityID parent, const char* sep, const char* path);
	EntityID SetEntityPath(WorldImpl* world, EntityID entity, EntityID parent, const char* sep, const char* path);
}