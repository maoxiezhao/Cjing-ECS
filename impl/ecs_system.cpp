#include "ecs_system.h"
#include "ecs_impl_types.h"
#include "ecs_world.h"
#include "ecs_query.h"

namespace ECS
{
	inline void DefaultCtor(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		memset(ptr, 0, size * count);
	}

	EntityID InitNewSystem(WorldImpl* world, const SystemCreateDesc& desc)
	{
		EntityID entity = CreateEntityID(world, desc.entity);
		if (entity == INVALID_ENTITY)
			return INVALID_ENTITY;

		bool newAdded = false;
		SystemComponent* sysComponent = static_cast<SystemComponent*>(GetOrCreateMutableByID(world, entity, ECS_ENTITY_ID(SystemComponent), &newAdded));
		if (newAdded)
		{
			memset(sysComponent, 0, sizeof(SystemComponent));
			sysComponent->entity = entity;
			sysComponent->action = desc.action;
			sysComponent->invoker = desc.invoker;
			sysComponent->invokerDeleter = desc.invokerDeleter;

			QueryImpl* queryInfo = CreateQuery(world, desc.query);
			if (queryInfo == nullptr)
				return INVALID_ENTITY;

			sysComponent->query = queryInfo;
		}
		return entity;
	}

	void RunSystem(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(entity != INVALID_ENTITY);
		SystemComponent* sysComponent = static_cast<SystemComponent*>(GetComponent(world, entity, ECS_ENTITY_ID(SystemComponent)));
		if (sysComponent == nullptr)
			return;

		SystemAction action = sysComponent->action;
		ECS_ASSERT(action != nullptr);
		ECS_ASSERT(sysComponent->query != nullptr);
		ECS_ASSERT(sysComponent->invoker != nullptr);

		Iterator iter = GetQueryIterator(sysComponent->query);
		iter.invoker = sysComponent->invoker;
		while (NextQueryIter(&iter))
			action(&iter);
	}

	void InitSystemComponent(WorldImpl* world)
	{
		// System is a special builtin component, it build in a independent table.
		ComponentCreateDesc desc = {};
		desc.entity.entity = ECS_ENTITY_ID(SystemComponent);
		desc.entity.name = Util::Typename<SystemComponent>();
		desc.entity.useComponentID = true;
		desc.size = sizeof(SystemComponent);
		desc.alignment = alignof(SystemComponent);
		ECS_ENTITY_ID(SystemComponent) = InitNewComponent(world, desc);

		// Set system component action
		ComponentTypeHooks info = {};
		info.ctor = DefaultCtor;
		info.dtor = [](WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr) {
			SystemComponent* sysArr = static_cast<SystemComponent*>(ptr);
			for (size_t i = 0; i < count; i++)
			{
				SystemComponent& sys = sysArr[i];
				if (sys.invoker != nullptr && sys.invokerDeleter != nullptr)
					sys.invokerDeleter(sys.invoker);

				if (sys.query != nullptr)
					FiniQuery(sys.query);
			}
		};
		SetComponentTypeInfo(world, ECS_ENTITY_ID(SystemComponent), info);
	}
}