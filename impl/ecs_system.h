#include "ecs_def.h"

namespace ECS
{
	extern EntityID ECS_ENTITY_ID(SystemComponent);

	struct SystemComponent
	{
		EntityID entity;
		SystemAction action;
		void* invoker;
		InvokerDeleter invokerDeleter;
		QueryImpl* query;
	};

	EntityID InitNewSystem(WorldImpl* world, const SystemCreateDesc& desc);
	void InitSystemComponent(WorldImpl* world);
	void RunSystem(WorldImpl* world, EntityID entity);
}