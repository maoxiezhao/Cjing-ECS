#pragma once

#include "ecs_def.h"

namespace ECS
{
	struct Stage;

	extern EntityID ECS_ENTITY_ID(SystemComponent);

	struct SystemComponent
	{
		EntityID entity;
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
		bool multiThreaded = false;
		QueryImpl* query;
	};

	EntityID InitNewSystem(WorldImpl* world, const SystemCreateDesc& desc);
	void InitSystemComponent(WorldImpl* world);
	void RunSystem(WorldImpl* world, EntityID entity);
	void RunSystemInternal(WorldImpl* world, Stage* stage, EntityID entity, SystemComponent* system, I32 stageIndex, I32 stageCount);
}