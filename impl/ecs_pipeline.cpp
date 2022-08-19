#include "ecs_pipeline.h"
#include "ecs_priv_types.h"
#include "ecs_world.h"
#include "ecs_table.h"
#include "ecs_query.h"
#include "ecs_system.h"

namespace ECS
{
	int EntityCompare(
		EntityID e1,
		const void* ptr1,
		EntityID e2,
		const void* ptr2)
	{
		return (e1 > e2) - (e1 < e2);
	}

	inline void DefaultCtor(WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr)
	{
		memset(ptr, 0, size * count);
	}

	EntityID InitPipeline(WorldImpl* world, const PipelineCreateDesc& desc)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(!world->isFini);

		EntityCreateDesc entityDesc = {};
		EntityID entity = CreateEntityID(world, entityDesc);
		if (entity == INVALID_ENTITY)
			return INVALID_ENTITY;

		bool newAdded = false;
		PipelineComponent* pipeline = static_cast<PipelineComponent*>(GetOrCreateMutableByID(world, entity, ECS_ENTITY_ID(PipelineComponent), &newAdded));
		if (newAdded)
		{
			QueryCreateDesc queryDesc = desc.query;
			if (queryDesc.orderBy == nullptr)
				queryDesc.orderBy = EntityCompare;	// Keep order by system adding sequence

			QueryImpl* query = CreateQuery(world, queryDesc);
			if (query == nullptr)
			{
				DeleteEntity(world, entity);
				return INVALID_ENTITY;
			}

			pipeline->entity = entity;
			pipeline->query = query;
		}

		return entity;
	}

	SystemComponent* GetSystemsFromIter(Iterator* iter)
	{
		I32 columnIndex = TableSearchType(iter->table, EcsCompSystem);
		ECS_ASSERT(columnIndex != -1);
		SystemComponent* systems = (SystemComponent*)iter->table->GetColumnData(columnIndex);
		return &systems[iter->offset];
	}

	bool CheckTermsMerged(Filter* filter)
	{
		return false;
	}

	bool BuildPipeline(WorldImpl* world, PipelineComponent* pipeline)
	{
		GetQueryIterator(pipeline->query);

		if (pipeline->query->matchingCount == pipeline->matchCount)
			return false;

		bool multiThreaded = false;
		bool first = true;

		SystemComponent* lastSys = nullptr;
		Vector<PipelineOperation> ops;
		PipelineOperation* op = nullptr;

		// Traverse all systems in order, check all components from system
		// If they dont have a conflict of writing, merge these systems into the same PipelineOperation

		auto it = GetQueryIterator(pipeline->query);
		while (NextQueryIter(&it))
		{
			SystemComponent* systems = GetSystemsFromIter(&it);
			for (int i = 0; i < it.count; i++)
			{
				SystemComponent* system = &systems[i];
				if (system->query == nullptr)
					continue;

				// Check these terms from current system is enable to merge
				bool needMerge = CheckTermsMerged(&system->query->filter);
				if (first)
				{
					multiThreaded = system->multiThreaded;
					first = false;
				}

				if (needMerge)
				{
					// TODO
					ECS_ASSERT(false);
				}

				if (op == nullptr)
				{
					ops.emplace_back();
					op = &ops.back();
					op->count = 0;
					op->multiThreaded = multiThreaded;
				}

				lastSys = system;
				op->count++;
			}
		}

		ECS_ASSERT(!ops.empty());
		pipeline->matchCount = pipeline->query->matchingCount;
		pipeline->ops = ops;
		pipeline->lastSystem = lastSys;
		return true;
	}

	bool UpdatePipeline(WorldImpl* world, PipelineComponent* pipeline, bool startOfFrame)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(!world->isReadonly);

		I32 stageCount = world->stageCount;
		if (stageCount != pipeline->iterCount)
		{
			pipeline->iters = ECS_MALLOC_T_N(Iterator, stageCount);
			pipeline->iterCount = stageCount;
		}

		bool rebuild = BuildPipeline(world, pipeline);
		if (startOfFrame)
		{
			for (int i = 0; i < pipeline->iterCount; i++)
				pipeline->iters[i] = GetQueryIterator(pipeline->query);

			pipeline->curOp = &pipeline->ops.front();
		}
		else if (rebuild)
		{
			ECS_ASSERT(false);
			return true;
		}
		else
		{
			pipeline->curOp++;
		}

		return true;
	}

	void WorkerProgress(WorldImpl* world, EntityID pipeline)
	{
		I32 stageCount = world->stageCount;

		PipelineComponent* pipelineComp = (PipelineComponent*)GetComponent(world, pipeline, ECS_ENTITY_ID(PipelineComponent));
		ECS_ASSERT(pipelineComp != nullptr);
		ECS_ASSERT(pipelineComp->query != nullptr);

		// Update pipeline before run
		UpdatePipeline(world, pipelineComp, true);
	
		// Check pipeline operations are steup
		if (pipelineComp->ops.empty())
			return;

		// Run pipeline
		if (stageCount == 1)
		{
			Stage* stage = GetStage(world, 0);
			RunPipeline(stage, pipeline);
		}
	}

	void RunPipeline(WorldImpl* world, EntityID pipeline)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(pipeline != INVALID_ENTITY);
		ECS_ASSERT(!world->isReadonly);
		WorkerProgress(world, pipeline);
	}

	void RunPipeline(Stage* stage, EntityID pipeline)
	{
		ECS_ASSERT(stage != nullptr);
		ECS_ASSERT(stage->world != nullptr);

		PipelineComponent* pipelineComp = (PipelineComponent*)GetComponent(stage->world, pipeline, ECS_ENTITY_ID(PipelineComponent));
		ECS_ASSERT(pipelineComp != nullptr);
		ECS_ASSERT(pipelineComp->query != nullptr);

		I32 countSinceMerge = 0;
		I32 stageIndex = stage->id;

		PipelineOperation* op = &pipelineComp->ops.front();
		PipelineOperation* lastop = &pipelineComp->ops.back();
		auto it = pipelineComp->iters[stageIndex];
		while (NextQueryIter(&it))
		{
			SystemComponent* systems = GetSystemsFromIter(&it);
			for (int i = 0; i < it.count; i++)
			{
				// Run system in main thread (stageIndex == 0)
				if (stageIndex == 0)
				{
					RunSystemInternal(
						stage->world,
						stage,
						it.entities[i],
						&systems[i],
						stageIndex,
						stage->world->stageCount
					);
				}

				// Op changed, need to sync
				if (op != lastop && countSinceMerge == op->count)
				{
					ECS_ASSERT(false);
					// TODO
				}
			}
		}
	}

	void InitPipelineComponent(WorldImpl* world)
	{
		// System is a special builtin component, it build in a independent table.
		ComponentCreateDesc desc = {};
		desc.entity.entity = ECS_ENTITY_ID(PipelineComponent);
		desc.entity.name = Util::Typename<PipelineComponent>();
		desc.entity.useComponentID = true;
		desc.size = sizeof(PipelineComponent);
		desc.alignment = alignof(PipelineComponent);
		ECS_ENTITY_ID(PipelineComponent) = InitNewComponent(world, desc);

		// Set system component action
		ComponentTypeHooks info = {};
		info.ctor = DefaultCtor;
		info.dtor = [](WorldImpl* world, EntityID* entities, size_t size, size_t count, void* ptr) {
			PipelineComponent* pipelines = static_cast<PipelineComponent*>(ptr);
			for (size_t i = 0; i < count; i++)
			{
				PipelineComponent& pipeline = pipelines[i];
				if (pipeline.query != nullptr)
					FiniQuery(pipeline.query);
				if (pipeline.iters != nullptr)
					ECS_FREE(pipeline.iters);
			}
		};
		SetComponentTypeInfo(world, ECS_ENTITY_ID(PipelineComponent), info);
	}

}