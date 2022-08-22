#include "ecs_def.h"

namespace ECS
{
	struct ObjectBase;
	struct Stage;
	struct DeferOperation;

	void InitStage(WorldImpl* world, Stage* stage);
	void FiniStage(WorldImpl* world, Stage* stage);
	void SetStageCount(WorldImpl* world, I32 stageCount);
	I32 GetStageCount(WorldImpl* world);
	I32 GetStageID(ObjectBase* threadCtx);
	Stage* GetStage(WorldImpl* world, I32 stageID);
	Stage* GetStageFromWorld(ObjectBase* threadCtx);
	Stage* GetStageFromWorld(WorldImpl* threadCtx);
	WorldImpl* GetWorld(ObjectBase* threadCtx);

	void BeginDefer(ObjectBase* world);
	void EndDefer(ObjectBase* world);
	void BeginReadonly(WorldImpl* world);
	void EndReadonly(WorldImpl* world);
	void PurgeDefer(WorldImpl* world);
	void SuspendReadonly(WorldImpl* world, SuspendReadonlyState* state);
	void ResumeReadonly(WorldImpl* world, SuspendReadonlyState* state);

	DeferOperation* NewDeferOperator(Stage* stage);
	bool DeferAddRemoveID(WorldImpl* world, Stage* stage, EntityID entity, DeferOperationKind kind, EntityID compID);
	bool DeferDelete(WorldImpl* world, Stage* stage, EntityID entity);
	bool DeferSet(WorldImpl* world, Stage* stage, EntityID entity, DeferOperationKind kind, EntityID compID, size_t size, const void* value, void** valueOut);
}