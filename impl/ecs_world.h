#pragma once

#include "ecs_def.h"

namespace ECS
{
	struct WorldImpl;
	struct ComponentRecord;
	struct Stage;

	////////////////////////////////////////////////////////////////////////////////
	//// Entity
	////////////////////////////////////////////////////////////////////////////////

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
	EntityID GetParent(WorldImpl* world, EntityID entity);
	void EnableEntity(WorldImpl* world, EntityID entity, bool enabled);

	////////////////////////////////////////////////////////////////////////////////
	//// Coomponent
	////////////////////////////////////////////////////////////////////////////////

	// Component
	EntityID InitNewComponent(WorldImpl* world, const ComponentCreateDesc& desc);
	void* GetOrCreateComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void* GetOrCreateMutableByID(WorldImpl* world, EntityID entity, EntityID compID, bool* added);
	void AddComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void RemoveComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void* GetComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove);
	bool HasComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void ModifiedComponent(WorldImpl* world, EntityID entity, EntityID compID);

	// Compoennt record
	ComponentRecord* EnsureComponentRecord(WorldImpl* world, EntityID compID);
	ComponentRecord* GetComponentRecord(WorldImpl* world, EntityID id);
	void RemoveComponentRecord(WorldImpl* world, EntityID id, ComponentRecord* compRecord);
	const ComponentTypeHooks* GetComponentTypeHooks(WorldImpl* world, EntityID compID);

	// Component type
	bool HasComponentTypeInfo(WorldImpl* world, EntityID compID);
	ComponentTypeInfo* EnsureComponentTypInfo(WorldImpl* world, EntityID compID);
	void SetComponentTypeInfo(WorldImpl* world, EntityID compID, const ComponentTypeHooks& info);
	ComponentTypeInfo* GetComponentTypeInfo(WorldImpl* world, EntityID compID);

	////////////////////////////////////////////////////////////////////////////////
	//// World
	////////////////////////////////////////////////////////////////////////////////

	// World
	void SetSystemAPI(const EcsSystemAPI& api);
	void DefaultSystemAPI(EcsSystemAPI& api);
	void SetThreads(WorldImpl* world, I32 threads, bool startThreads);

	WorldImpl* InitWorld();
	void FiniWorld(WorldImpl* world);
}