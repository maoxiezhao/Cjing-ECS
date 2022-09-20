#pragma once

#include "ecs_def.h"
#include "ecs_priv_types.h"

namespace ECS
{
	struct WorldImpl;
	struct ComponentRecord;
	struct Stage;

	////////////////////////////////////////////////////////////////////////////////
	//// Coomponent
	////////////////////////////////////////////////////////////////////////////////

	// Component
	EntityID InitNewComponent(WorldImpl* world, const ComponentCreateDesc& desc);
	void* GetMutableComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void AddComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void RemoveComponent(WorldImpl* world, EntityID entity, EntityID compID);
	const void* GetComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void SetComponent(WorldImpl* world, EntityID entity, EntityID compID, size_t size, const void* ptr, bool isMove);
	bool HasComponent(WorldImpl* world, EntityID entity, EntityID compID);
	void ModifiedComponent(WorldImpl* world, EntityID entity, EntityID compID);
	I32 CountComponent(WorldImpl* world, EntityID compID);
	InfoComponent* GetComponentInfo(WorldImpl* world, EntityID compID);

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