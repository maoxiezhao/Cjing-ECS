#include "ecs_system.h"
#include "ecs_priv_types.h"
#include "ecs_world.h"
#include "ecs_query.h"
#include "ecs_stage.h"
#include "ecs_iter.h"

namespace ECS
{
	inline void DefaultCtor(void* ptr, size_t count, const ComponentTypeInfo* info)
	{
		memset(ptr, 0, info->size * count);
	}

	EntityID InitNewSystem(WorldImpl* world, const SystemCreateDesc& desc)
	{
		EntityID entity = desc.entity;  
		if (entity == INVALID_ENTITYID)
		{
			EntityCreateDesc entityDesc = {};
			entity = CreateEntityID(world, entityDesc);
		}
	
		if (HasComponent(world, entity, ECS_ENTITY_ID(SystemComponent)))
			return entity;

		SystemComponent* sysComponent = static_cast<SystemComponent*>(GetMutableComponent(world, entity, ECS_ENTITY_ID(SystemComponent)));
		memset(sysComponent, 0, sizeof(SystemComponent));
		sysComponent->entity = entity;
		sysComponent->action = desc.action;
		sysComponent->invoker = desc.invoker;
		sysComponent->invokerDeleter = desc.invokerDeleter;
		sysComponent->multiThreaded = desc.multiThreaded;

		QueryImpl* queryInfo = CreateQuery(world, desc.query);
		if (queryInfo == nullptr)
			return INVALID_ENTITYID;

		sysComponent->query = queryInfo;

		return entity;
	}

	void RunSystem(WorldImpl* world, EntityID entity)
	{
		ECS_ASSERT(entity != INVALID_ENTITYID);
		SystemComponent* sysComponent = (SystemComponent*)(GetComponent(world, entity, ECS_ENTITY_ID(SystemComponent)));
		if (sysComponent == nullptr)
			return;

		return RunSystemInternal(world, GetStageFromWorld(&world), entity, sysComponent, 0, 0);
	}

	void RunSystemInternal(WorldImpl* world, Stage* stage, EntityID entity, SystemComponent* system, I32 stageIndex, I32 stageCount)
	{
		SystemAction action = system->action;
		ECS_ASSERT(action != nullptr);
		ECS_ASSERT(system->query != nullptr);
		ECS_ASSERT(system->invoker != nullptr);

		WorldImpl* threadCtx = world;
		if (stage)
			threadCtx = (WorldImpl*)stage->threadCtx;

		BeginDefer(threadCtx);

		Iterator workerIter = {};
		Iterator queryIter = GetQueryIterator(threadCtx, system->query);
		Iterator* iter = &queryIter;

		// If current system support multithread
		if (stageCount > 1 && system->multiThreaded)
		{
			workerIter = GetSplitWorkerInterator(queryIter, stageIndex, stageCount);
			iter = &workerIter;
		}

		iter->invoker = system->invoker;
		if (iter == &queryIter)
		{
			while (NextQueryIter(iter))
				action(iter);
		}
		else
		{
			while (NextIterator(iter))
				action(iter);
		}

		EndDefer(threadCtx);
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
		info.dtor = [](void* ptr, size_t count, const ComponentTypeInfo* info) {
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