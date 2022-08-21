#pragma once

#include "ecs_def.h"

namespace ECS
{
	struct Stage;
	struct SystemComponent;
	struct ObjectBase;

	extern EntityID ECS_ENTITY_ID(PipelineComponent);

	struct PipelineOperation
	{
		I32 count;
		bool multiThreaded;
	};

	struct PipelineComponent
	{
		EntityID entity;
		QueryImpl* query;
		Iterator* iters;
		I32 iterCount;
		I32 matchCount;
		Vector<PipelineOperation> ops;
		PipelineOperation* curOp;
		SystemComponent* lastSystem;
	};

	void InitPipelineComponent(WorldImpl* world);
	EntityID InitPipeline(WorldImpl* world, const PipelineCreateDesc& desc);
	void RunPipeline(WorldImpl* world, EntityID pipeline);
	void RunPipelineThread(Stage* stage, EntityID pipeline);
}