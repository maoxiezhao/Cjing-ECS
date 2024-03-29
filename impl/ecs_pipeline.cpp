#include "ecs_pipeline.h"
#include "ecs_priv_types.h"
#include "ecs_world.h"
#include "ecs_table.h"
#include "ecs_query.h"
#include "ecs_system.h"
#include "ecs_stage.h"
#include "ecs_entity.h"

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

	inline void DefaultCtor(void* ptr, size_t count, const ComponentTypeInfo* info)
	{
		memset(ptr, 0, info->size * count);
	}

	EntityID InitPipeline(WorldImpl* world, const PipelineCreateDesc& desc)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(!world->isFini);

		EntityCreateDesc entityDesc = {};
		EntityID entity = CreateEntityID(world, entityDesc);
		if (entity == INVALID_ENTITYID)
			return INVALID_ENTITYID;

		if (HasComponent(world, entity, ECS_ENTITY_ID(PipelineComponent)))
			return entity;

		PipelineComponent* pipeline = static_cast<PipelineComponent*>(GetMutableComponent(world, entity, ECS_ENTITY_ID(PipelineComponent)));
		ECS_ASSERT(pipeline != nullptr);

		QueryCreateDesc queryDesc = desc.query;
		if (queryDesc.orderBy == nullptr)
			queryDesc.orderBy = EntityCompare;	// Keep order by system adding sequence

		QueryImpl* query = CreateQuery(world, queryDesc);
		if (query == nullptr)
		{
			DeleteEntity(world, entity);
			return INVALID_ENTITYID;
		}

		pipeline->entity = entity;
		pipeline->query = query;
	
		return entity;
	}

	SystemComponent* GetSystemsFromIter(Iterator* iter)
	{
		I32 columnIndex = TableSearchType(iter->table, EcsCompSystem);
		ECS_ASSERT(columnIndex != -1);
		SystemComponent* systems = (SystemComponent*)iter->table->GetColumnData(columnIndex);
		return &systems[iter->offset];
	}

	enum ComponentWriteState {
		NotWritten = 0,
		WriteToStage,
	};

	struct WriteComponentState
	{
		Map<ComponentWriteState> components;
	};

	ComponentWriteState GetComponentWriteState(WriteComponentState& state, EntityID compID)
	{
		auto it = state.components.find(compID);
		if (it != state.components.end())
			return it->second;
		return NotWritten;
	}

	void SetComponentWriteState(WriteComponentState& state, EntityID compID, ComponentWriteState value)
	{
		state.components[compID] = value;
	}

	bool CheckTermMerged(Term* term, WriteComponentState& writeState)
	{
		// Check whether there is a read-write conflict between components
		ComponentWriteState state = GetComponentWriteState(writeState, term->compID);
		switch (term->inout)
		{
		case InOutDefault:
		case InOut:
		case In:
			if (state == WriteToStage)	// Write conflict
				return true;
			break;
		default:
			break;
		}

		switch (term->inout)
		{
		case InOutDefault:
		case InOut:
		case Out:
			SetComponentWriteState(writeState, term->compID, WriteToStage);
			break;
		default:
			break;
		}
		return false;
	}

	bool CheckTermsMerged(Filter* filter, WriteComponentState& writeState)
	{
		bool mergeEnable = false;
		for (int t = 0; t < filter->termCount; t++)
			mergeEnable |= CheckTermMerged(&filter->terms[t], writeState);
	
		return mergeEnable;
	}

	bool BuildPipeline(WorldImpl* world, PipelineComponent* pipeline)
	{
		GetQueryIterator(world, pipeline->query);

		if (pipeline->query->matchingCount == pipeline->matchCount)
			return false;

		SystemComponent* lastSys = nullptr;
		Vector<PipelineOperation> ops;
		PipelineOperation* op = nullptr;

		bool multiThreaded = false;
		bool first = true;

		WriteComponentState writeState = {};

		// Traverse all systems in order, check all components from system
		// If they dont have a conflict of writing, merge these systems into the same PipelineOperation
		auto it = GetQueryIterator(world, pipeline->query);
		while (NextQueryIter(&it))
		{
			SystemComponent* systems = GetSystemsFromIter(&it);
			for (int i = 0; i < it.count; i++)
			{
				SystemComponent* system = &systems[i];
				if (system->query == nullptr)
					continue;

				// Check these terms from current system is enable to merge
				bool needMerge = CheckTermsMerged(&system->query->filter, writeState);
				if (first)
				{
					multiThreaded = system->multiThreaded;
					first = false;
				}
				else if (system->multiThreaded != multiThreaded)
				{
					multiThreaded = system->multiThreaded;
					needMerge = true;
				}

				if (needMerge)
				{
					needMerge = false;
					op = nullptr;

					// Reset write state
					writeState.components.clear();

					// Re-evaluate columns to set write flags if system is active
					needMerge = CheckTermsMerged(&system->query->filter, writeState);
					ECS_ASSERT(needMerge == false);
				}

				if (op == nullptr)
				{
					ops.emplace_back();
					op = &ops.back();
					op->count = 0;
					op->multiThreaded = multiThreaded;
				}

				lastSys = system;
				op->systems.push_back(system);
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
				pipeline->iters[i] = GetQueryIterator(world, pipeline->query);

			pipeline->curOp = !pipeline->ops.empty() ? &pipeline->ops.front() : nullptr;
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

		return false;
	}

	void WaitForWorkerSync(WorldImpl* world)
	{
		if (ecsSystemAPI.thread_sync_ != nullptr)
			ecsSystemAPI.thread_sync_(&world->threadCtx);
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

		// Run pipeline in main thread
		if (stageCount == 1)
		{
			Stage* stage = GetStage(world, 0);
			RunPipelineThread(stage, pipeline);
		}

		// All threads run the pipeline in each targe stage
		else
		{
			for (auto& op : pipelineComp->ops)
			{
				BeginReadonly(world);

				if (op.multiThreaded)
				{
					for (int i = 0; i < stageCount; i++)
					{
						if (ecsSystemAPI.thread_run_ != nullptr)
							ecsSystemAPI.thread_run_(&world->threadCtx, &world->stages[i], pipeline);
						else
							RunPipelineThread(&world->stages[i], pipeline);
					}

					WaitForWorkerSync(world);
				}
				else
				{
					RunPipelineThread(&world->stages[0], pipeline);
				}

				EndReadonly(world);

				// System may be chagned, update pipeline
				bool rebuild = UpdatePipeline(world, pipelineComp, false);
				if (rebuild)
				{
					ECS_ASSERT(false);
					pipelineComp = (PipelineComponent*)GetComponent(world, pipeline, ECS_ENTITY_ID(PipelineComponent));
					// TODO
					return;
				}
			}
		}
	}

	void RunPipeline(WorldImpl* world, EntityID pipeline)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(pipeline != INVALID_ENTITYID);
		ECS_ASSERT(!world->isReadonly);
		WorkerProgress(world, pipeline);
	}

	void SyncPipelineWorker(WorldImpl* world)
	{
		world = GetWorld(world);
		I32 stageCount = GetStageCount(world);
	}

	void PipelineWorkerBegin(WorldImpl* world)
	{
		world = GetWorld(world);
		ECS_ASSERT(world != nullptr);

		I32 stageCount = GetStageCount(world);
		if (stageCount == 1)
		{
			BeginReadonly(world);
		}
	}

	void PipelineWorkerEnd(WorldImpl* world)
	{
		world = GetWorld(world);
		ECS_ASSERT(world != nullptr);

		I32 stageCount = GetStageCount(world);
		if (stageCount == 1)
		{
			if (world->isReadonly)
				EndReadonly(world);
		}
		else
		{
			SyncPipelineWorker(world);
		}
	}

	bool SyncPipelineWorkers(WorldImpl* world, PipelineComponent* pipeline)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(pipeline != nullptr);

		// Only main thread, do merge
		I32 stageCount = GetStageCount(world);
		if (stageCount == 1)
		{
			EndReadonly(world);
			UpdatePipeline(world, pipeline, false);
		}
		// Sync all workers
		else
		{
			SyncPipelineWorker(world);
		}

		// Readonly begin again
		if (stageCount == 1)
			BeginReadonly(world);

		return false;
	}

	void RunPipelineThread(Stage* stage, EntityID pipeline)
	{
		ECS_ASSERT(stage != nullptr);
		ECS_ASSERT(stage->world != nullptr);

		PipelineComponent* pipelineComp = (PipelineComponent*)GetComponent(stage->world, pipeline, ECS_ENTITY_ID(PipelineComponent));
		ECS_ASSERT(pipelineComp != nullptr);
		ECS_ASSERT(pipelineComp->query != nullptr);
		ECS_ASSERT(pipelineComp->curOp != nullptr);

		I32 stageIndex = GetStageID(stage->threadCtx);
		I32 stageCount = GetStageCount(stage->world);

		// Workder begin
		PipelineWorkerBegin((WorldImpl*)stage);

		PipelineOperation* curOp = pipelineComp->curOp;
		for (int i = 0; i < curOp->systems.size(); i++)
		{
			SystemComponent* system = curOp->systems[i];
			ECS_ASSERT(system != nullptr);

			if (stageIndex == 0 || curOp->multiThreaded)
			{
				RunSystemInternal(
					stage->world,
					stage,
					system->entity,
					system,
					stageIndex,
					stage->world->stageCount
				);
			}
		}

		// Worker end
		PipelineWorkerEnd((WorldImpl*)stage);
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
		info.dtor = [](void* ptr, size_t count, const ComponentTypeInfo* info) {
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