#include "ecs_priv_types.h"
#include "ecs_stage.h"
#include "ecs_world.h"
#include "ecs_table.h"

namespace ECS
{
	void InitStage(WorldImpl* world, Stage* stage)
	{
		ECS_NEW_PLACEMENT(stage, Stage);
		ECS_INIT_OBJECT(stage, Stage);
		stage->world = world;
		stage->threadCtx = &world->base;
		stage->deferStack.Init();
	}

	void FiniStage(WorldImpl* world, Stage* stage)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(stage != nullptr);
		ECS_ASSERT(stage->deferQueue.empty());

		stage->~Stage();
	}

	Stage* GetStage(WorldImpl* world, I32 stageID)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(world->stageCount > stageID);
		return &world->stages[stageID];
	}

	Stage* GetStageFromWorld(ObjectBase* threadCtx)
	{
		ECS_ASSERT(threadCtx != nullptr);
		if (ECS_CHECK_OBJECT(threadCtx, WorldImpl))
		{
			WorldImpl* world = (WorldImpl*)threadCtx;
			ECS_ASSERT(world->stageCount <= 1 || !world->isReadonly);
			return &world->stages[0];
		}
		else if(ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			Stage* stage = (Stage*)threadCtx;
			return stage;
		}

		ECS_ASSERT(false);
		return nullptr;
	}

	Stage* GetStageFromWorld(WorldImpl* threadCtx)
	{
		return GetStageFromWorld(&threadCtx->base);
	}

	WorldImpl* GetWorld(ObjectBase* threadCtx)
	{
		ECS_ASSERT(threadCtx != nullptr);
		if (ECS_CHECK_OBJECT(threadCtx, WorldImpl))
		{
			return (WorldImpl*)threadCtx;
		}
		else if (ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			Stage* stage = (Stage*)threadCtx;
			return stage->world;
		}

		ECS_ASSERT(false);
		return nullptr;
	}

	I32 GetStageID(ObjectBase* threadCtx)
	{
		ECS_ASSERT(threadCtx != nullptr);
		if (ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			Stage* stage = (Stage*)threadCtx;
			return stage->id;
		}
		else if (ECS_CHECK_OBJECT(threadCtx, WorldImpl))
		{
			return 0;
		}
		ECS_ASSERT(false);
		return 0;
	}

	I32 GetStageCount(WorldImpl* world)
	{
		return world->stageCount;
	}

	void SetStageCount(WorldImpl* world, I32 stageCount)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT((stageCount > 1 && !world->isFini) || stageCount <= 1);

		if (stageCount == world->stageCount)
			return;

		// Fini existing stages
		if (world->stageCount > 0 && stageCount != world->stageCount)
		{
			for (int i = 0; i < world->stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				ECS_ASSERT(stage->thread == 0);
				FiniStage(world, stage);
			}

			ECS_FREE(world->stages);
		}

		world->stages = nullptr;
		world->stageCount = stageCount;

		// Init new stages
		if (stageCount > 0)
		{
			world->stages = ECS_MALLOC_T_N(Stage, stageCount);
			for (int i = 0; i < stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				InitStage(world, stage);
				stage->id = i;
				stage->threadCtx = &stage->base;
			}
		}
	}

	void BeginDefer(ObjectBase* threadCtx)
	{
		Stage* stage = GetStageFromWorld(threadCtx);
		if (stage->deferSuspend)
			return;
		stage->defer++;
	}

	void EndDefer(ObjectBase* threadCtx)
	{
		Stage* stage = GetStageFromWorld(threadCtx);
		if (stage->deferSuspend)
			return;

		// Only stage defer is end, we do deferred operations
		if (!(--stage->defer))
		{
			Vector<DeferOperation> deferQueue = stage->deferQueue;
			stage->deferQueue.clear();

			if (deferQueue.empty())
				return;

			Util::Stack deferStack = stage->deferStack;
			stage->deferStack = Util::Stack();
			stage->deferStack.Init();

			WorldImpl* world = GetWorld(threadCtx);
			for (const auto& op : deferQueue)
			{
				EntityID entity = op.entity;

				// Entity is no longer alive, discard current operation
				if (entity && !IsEntityAlive(world, entity))
					continue;

				switch (op.kind)
				{
				case ECS::EcsOpNew:
				case ECS::EcsOpAdd:
					ECS_ASSERT(op.id != INVALID_ENTITY);
					AddComponent(world, entity, op.id);
					break;
				case ECS::EcsOpRemove:
					RemoveComponent(world, entity, op.id);
					break;
				case ECS::EcsOpSet:
				case ECS::EcsOpMut:
					SetComponent(world, entity, op.id, op.size, op.value, true);
					break;
				case ECS::EcsOpModified:
					ECS_ASSERT(false);
					break;
				case ECS::EcsOpDelete:
					DeleteEntity(world, entity);
					break;
				case ECS::EcsOpClear:
				case ECS::EcsOpOnDeleteAction:
					ECS_ASSERT(false);
					break;
				case ECS::EcsOpEnable:
					EnableEntity(world, entity, true);
					break;
				case ECS::EcsOpDisable:
					EnableEntity(world, entity, false);
					break;
				default:
					break;
				}
			}

			stage->deferStack.Uninit();
			stage->deferStack = deferStack;
			stage->deferStack.Reset();
		}
	}

	void BeginReadonly(WorldImpl* world)
	{
		FlushPendingTables(world);

		I32 stageCount = GetStageCount(world);
		for (int i = 0; i < stageCount; i++)
		{
			Stage* stage = &world->stages[i];
			ECS_ASSERT(stage->defer == 0);
			BeginDefer(&stage->base);
		}

		world->isReadonly = true;

		if (stageCount > 1)
			world->isMultiThreaded = true;
	}

	void MergeStages(ObjectBase* threadCtx)
	{
		if (ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			EndDefer(threadCtx);
		}
		else
		{
			WorldImpl* world = GetWorld(threadCtx);
			I32 stageCount = GetStageCount(world);
			for (int i = 0; i < stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				EndDefer(&stage->base);
			}
		}
	}

	void EndReadonly(WorldImpl* world)
	{
		ECS_ASSERT(world->isReadonly);
		world->isReadonly = false;
		world->isMultiThreaded = false;
		MergeStages(&world->base);
	}

	void PurgeDefer(WorldImpl* world)
	{
		// Discard and clear all defer operations
		ECS_ASSERT(world);
		Stage* stage = &world->stages[0];
		if (stage->defer > 0)
		{
			stage->deferQueue.clear();
			stage->deferStack.Uninit();
		}
	}

	void SuspendReadonly(WorldImpl* world, SuspendReadonlyState* state)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(state != nullptr);

		Stage* stage = GetStageFromWorld(world);
		bool isReadonly = world->isReadonly;
		bool isDeferred = stage->defer > 0;
		if (!isReadonly && !isDeferred)
		{
			state->isReadonly = false;
			state->isDeferred = false;
			return;
		}

		// Can't suspend when running in multithreads
		ECS_ASSERT(GetStageCount(world) <= 1);
		world->isReadonly = false;

		state->isReadonly = isReadonly;
		state->isDeferred = isDeferred;
		state->defer = stage->defer;
		state->deferQueue = stage->deferQueue;	// TODO
		state->deferStack = stage->deferStack;

		stage->defer = 0;
		stage->deferQueue.clear();
		stage->deferStack.Init();
	}

	void ResumeReadonly(WorldImpl* world, SuspendReadonlyState* state)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(state != nullptr);

		Stage* stage = GetStageFromWorld(world);

		if (!state->isReadonly && !state->isDeferred)
			return;

		world->isReadonly = state->isReadonly;

		stage->defer = state->defer;
		stage->deferQueue = state->deferQueue;
		state->deferStack.Uninit();
		stage->deferStack = state->deferStack;
	}

	bool DoDeferOperation(WorldImpl* world, Stage* stage)
	{
		if (stage->deferSuspend)
			return false;

		if (stage->defer > 0)
			return true;

		stage->defer++;
		return false;
	}

	DeferOperation* NewDeferOperator(Stage* stage)
	{
		stage->deferQueue.emplace_back();
		auto* op = &stage->deferQueue.front();
		memset(op, 0, sizeof(DeferOperation));
		return op;
	}

	bool DeferAddRemoveID(WorldImpl* world, Stage* stage, EntityID entity, DeferOperationKind kind, EntityID compID)
	{
		if (!DoDeferOperation(world, stage))
			return false;

		auto op = NewDeferOperator(stage);
		op->entity = entity;
		op->kind = kind;
		op->id = compID;
		return true;
	}

	bool DeferDelete(WorldImpl* world, Stage* stage, EntityID entity)
	{
		if (!DoDeferOperation(world, stage))
			return false;

		auto op = NewDeferOperator(stage);
		op->entity = entity;
		op->kind = EcsOpDelete;
		return true;
	}

	bool DeferSet(WorldImpl* world, Stage* stage, EntityID entity, DeferOperationKind kind, EntityID compID, size_t size, const void* value, void** valueOut)
	{
		if (!DoDeferOperation(world, stage))
			return false;

		auto typeInfo = GetComponentTypeInfo(world, compID);
		ECS_ASSERT(typeInfo != nullptr);
		ECS_ASSERT(size == 0 || size == typeInfo->size);
		size = typeInfo->size;

		auto op = NewDeferOperator(stage);
		op->entity = entity;
		op->kind = kind;
		op->id = compID;
		op->size = size;
		op->value = stage->deferStack.Alloc(size, typeInfo->alignment);

		if (value == nullptr)
			value = GetComponent(world, entity, compID);

		if (value)
		{
			if (typeInfo->hooks.copyCtor != nullptr)
				typeInfo->hooks.copyCtor(world, nullptr, nullptr, size, 1, value, op->value);
			else
				memcpy(op->value, value, size);
		}

		if (valueOut)
			*valueOut = op->value;

		return true;
	}
}