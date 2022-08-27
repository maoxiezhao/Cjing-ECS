#ifdef TEST_ECS

#include "ecs.hpp"
#include "test/jobsystem/jobsystem.h"
#include "test/jobsystem/atomic.h"

#include <string>
#include <array>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace VulkanTest;
using namespace ECS;

struct PositionComponent
{
    float x = 0.0f;
    float y = 0.0f;
};

struct VelocityComponent
{
    float x = 0.0f;
    float y = 0.0f;
};

static int clearTimes = 0;
struct TestComponent
{
    std::vector<int> vec;

    TestComponent() {}

    TestComponent(TestComponent&& rhs) noexcept
    {
        vec = std::move(rhs.vec);
    }

    ~TestComponent()
    {
        clearTimes++;
    }

    void operator=(TestComponent&& rhs) noexcept
    {
        vec = std::move(rhs.vec);
    }

    void operator=(const TestComponent& rhs)
    {
        vec = rhs.vec;
    }
};

TEST_CASE("Basic", "ECS")
{
    ECS::World world;
    ECS::Entity a = world.Entity("A")
        .Add<VelocityComponent>()
        .Add<PositionComponent>();
    VelocityComponent* comp = a.GetMut<VelocityComponent>();
    comp->x = 1.0f;
    const VelocityComponent* compConst = a.Get<VelocityComponent>();
    CHECK(compConst->x == 1.0f);

    a.Clear();
    const VelocityComponent* compConst1 = a.Get<VelocityComponent>();
    const PositionComponent* posConst1 = a.Get<PositionComponent>();
    CHECK(compConst1 == nullptr);
    CHECK(posConst1 == nullptr);

    a.Add<VelocityComponent>();
    comp = a.GetMut<VelocityComponent>();
    comp->x = 1.0f;
    const VelocityComponent* compConst2 = a.Get<VelocityComponent>();
    CHECK(compConst->x == 1.0f);
}

TEST_CASE("Reflect", "ECS")
{
    ECS::ComponentTypeHooks info = {};
    info.ctor = ECS::Reflect::Ctor<TestComponent>();
    info.dtor = ECS::Reflect::Dtor<TestComponent>();
    info.copy = ECS::Reflect::Copy<TestComponent>();
    info.move = ECS::Reflect::Move<TestComponent>();
    info.copyCtor = ECS::Reflect::CopyCtor<TestComponent>();
    info.moveCtor = ECS::Reflect::MoveCtor<TestComponent>();

    TestComponent a[2];
    a[0].vec.push_back(1);
    a[0].vec.push_back(2);
    a[0].vec.push_back(3);
    
    TestComponent* b = static_cast<TestComponent*>(malloc(sizeof(TestComponent) * 2));
    info.moveCtor(nullptr, nullptr, nullptr, sizeof(TestComponent), 2, a, b);
    CHECK(b[0].vec.size() == 3);
    CHECK(a[0].vec.size() == 0);

    info.copy(nullptr, nullptr, nullptr, sizeof(TestComponent), 2, b, a);
    CHECK(b[0].vec.size() == 3);
    CHECK(a[0].vec.size() == 3);

    info.dtor(nullptr, nullptr, sizeof(TestComponent), 2, b);
    CHECK(clearTimes == 2);
}

TEST_CASE("System", "ECS")
{
    I32 updateTimes = 0;
    ECS::World world;
    world.SetThreads(10);
    for (int i = 0; i < 500; i++)
    {
        world.Entity((std::string("A") + std::to_string(i)).c_str())
            .Add<PositionComponent>()
            .Add<VelocityComponent>();
    }

    // ForEach
    ECS::System system = world.CreateSystem<PositionComponent, VelocityComponent>()
        .ForEach([&updateTimes](ECS::Entity entity, PositionComponent& pos, VelocityComponent& vel)
        {
            updateTimes++;
        }
    );
    system.Run();
    CHECK(updateTimes == 500);

    // Iter
    ECS::System system1 = world.CreateSystem<PositionComponent, VelocityComponent>()
        .Iter([&updateTimes](ECS::EntityIterator iter, PositionComponent* pos, VelocityComponent* vel)
        {
            for(int i = 0; i < iter.Count(); i++)
                updateTimes++;
        }
    );
    system1.Run();
    CHECK(updateTimes == 1000);
}

TEST_CASE("SingletonComponent", "ECS")
{
    ECS::World world;
    PositionComponent* a = world.GetSingletonComponent<PositionComponent>();
    a->x = 1.0f;
    
    PositionComponent* b = world.GetSingletonComponent<PositionComponent>();
    b->x = 2.0f;

    PositionComponent* c = world.GetSingletonComponent<PositionComponent>();
    CHECK(c->x == 2.0f);
}

TEST_CASE("Prefab", "ECS")
{
    // Default
    ECS::World world;
    ECS::Entity prefab = world.Prefab("TestPrefab")
        .Add<PositionComponent>()
        .Add<VelocityComponent>();

    ECS::Entity test1 = world.Entity("Test1")
        .Instantiate(prefab)
        .Add<TestComponent>();

    ECS::Entity test2 = world.Entity("Test2")
        .Instantiate(prefab)
        .Add<TestComponent>();

    PositionComponent* pos1 = test1.GetMut<PositionComponent>();
    PositionComponent* pos2 = test2.GetMut<PositionComponent>();
    CHECK(pos1 != pos2);

    pos1->x = 3.0f;
    CHECK(pos1->x == test1.Get<PositionComponent>()->x);
    pos2->x = 2.0f;
    CHECK(pos2->x == test2.Get<PositionComponent>()->x);

    TestComponent* t1 = test1.GetMut<TestComponent>();
    TestComponent* t2 = test2.GetMut<TestComponent>();
    CHECK(t1 != nullptr);
    CHECK(t2 != nullptr);
}

TEST_CASE("Query", "ECS")
{
    U32 aTimes = 0;
    U32 bTimes = 0;
    ECS::World world;
    auto query = world.CreateQuery<PositionComponent, VelocityComponent>().Build();
    auto query1 = world.CreateQuery<PositionComponent>().Build();
    auto query2 = world.CreateQuery<PositionComponent, TestComponent>().Build();
    for (int i = 0; i < 5; i++)
    {
        world.Entity((std::string("A") + std::to_string(i)).c_str())
            .Add<PositionComponent>()
            .Add<VelocityComponent>();
    }

    for (int i = 0; i < 5; i++)
    {
        world.Entity((std::string("C") + std::to_string(i)).c_str())
            .Add<PositionComponent>()
            .Add<VelocityComponent>()
            .Add<TestComponent>();
    }

    query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
        aTimes++;
    });
    query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
        bTimes++;
    });

    CHECK(aTimes == bTimes);

    U32 cTimes = 0;
    query1.ForEach([&](ECS::EntityID entity, PositionComponent& pos) {
        cTimes++;
    });
    CHECK(cTimes == 10);

    U32 dTimes = 0;
    query2.ForEach([&](ECS::EntityID entity, PositionComponent& pos, TestComponent& test) {
        dTimes++;
    });
    CHECK(dTimes == 5);
}


struct Health
{
    float hp = 0.0f;
};

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Local { };
struct Global { };

TEST_CASE("ChildOf", "ECS")
{
    ECS::World world;
    ECS::Entity parent = world.Entity("Parent")
        .Add<PositionComponent>();

    ECS::Entity child = world.Entity("Child")
        .ChildOf(parent);

    ECS::EntityID target = child.GetParent();
    CHECK(target == parent);

    // Query
    auto query = world.CreateQuery<Position, Position, Position>()
        .Arg(0).Second<Local>()
        .Arg(1).Second<Global>()
        .Arg(2).Second<Global>()
        .Arg(2).Parent().Cascade()
        .Build();

    std::vector<ECS::EntityID> queue;
    auto e1 = world.Entity("e1")
        .Set<Position, Local>({ 1.0f, 1.0f })
        .Set<Position, Global>({ 11.0f, 11.0f });

    auto e2 = world.Entity("e2")
        .ChildOf(e1)
        .Set<Position, Local>({ 2.0f, 2.0f })
        .Set<Position, Global>({ 22.0f, 22.0f });

    auto e3 = world.Entity("e3")
        .ChildOf(e2)
        .Set<Position, Local>({ 3.0f, 3.0f })
        .Set<Position, Global>({ 33.0f, 33.0f });

    auto e4 = world.Entity("e4")
        .ChildOf(e1)
        .Set<Position, Local>({ 4.0f, 4.0f })
        .Set<Position, Global>({ 44.0f, 44.0f })
        .Add<Health>();

    query.ForEach([&](ECS::EntityID entity, Position& p1, Position& p2, Position& pOut) {
        queue.push_back(entity);
    });

    CHECK(queue[0] == e1);
    CHECK(queue[1] == e2);
    CHECK(queue[2] == e4);
    CHECK(queue[3] == e3);
    queue.clear();

    std::vector<ECS::EntityID> children;
    world.EachChildren(e1, [&](ECS::EntityID child) {
        children.push_back(child);
    });
    CHECK(children[0] == e2);
    CHECK(children[1] == e4);

    // System
    auto system = world.CreateSystem<Position, Position, Position>()
        .Arg(0).Second<Local>()
        .Arg(1).Second<Global>()
        .Arg(2).Second<Global>()
        .Arg(2).Parent().Cascade()
        .ForEach([&](ECS::EntityID entity, Position& p1, Position& p2, Position& pOut) {
            queue.push_back(entity);
        });
    system.Run();
    CHECK(queue[0] == e1);
    CHECK(queue[1] == e2);
    CHECK(queue[2] == e4);
    CHECK(queue[3] == e3);

    const char* name = e1.GetName();
    CHECK(std::string(name) == "e1");

    auto t1 = e2.GetParent();
    CHECK(t1 == e1);
    e2.RemoveParent();
    auto t2 = e2.GetParent();
    CHECK(t2 == ECS::INVALID_ENTITY);
}

struct Rendering {};

void RunECSJob(void* ptr, void* stage, U64 pipeline)
{
    ECS::ThreadContext* ctx = (ECS::ThreadContext*)ptr;
    ECS_ASSERT(ctx != nullptr);

    if (ctx->payload == nullptr)
        ctx->payload = new(Jobsystem::JobHandle);

    Jobsystem::Run(nullptr, [stage, pipeline](void*) {
        ECS::RunPipelineThread((Stage*)stage, pipeline);
    }, (Jobsystem::JobHandle*)ctx->payload);
}

void WaitECSJob(void* ptr)
{
    ECS::ThreadContext* ctx = (ECS::ThreadContext*)ptr;
    ECS_ASSERT(ctx != nullptr);

    if (ctx->payload != nullptr)
        Jobsystem::Wait((Jobsystem::JobHandle*)ctx->payload);
}

struct TestIntComponent
{
    int a = 0;
};

TEST_CASE("Pipeline+JobSystem", "ECS")
{
    Jobsystem::Initialize(8);

    ECS::EcsSystemAPI api = {};
    ECS::DefaultSystemAPI(api);
    api.thread_run_ = RunECSJob;
    api.thread_sync_ = WaitECSJob;
    ECS::SetSystemAPI(api);

    World world;
    world.SetThreads(8);
    ECS::Entity entity;
    for (int i = 0; i < 12500; i++)
    {
        if (i == 0)
            entity = world.Entity(std::to_string(i).c_str()).Add<TestIntComponent>();
        else
            world.Entity(std::to_string(i).c_str()).Add<TestIntComponent>();
    }

    CHECK(entity != INVALID_ENTITY);

    volatile int valueMap[22500];
    for (int i = 0; i < ARRAYSIZE(valueMap); i++)
        valueMap[i] = 0;

    volatile int a = 0;
    volatile int b = 0;
    int c = 0;
    auto system1 = world.CreateSystem<TestIntComponent>()
        .Kind<Rendering>()
        .MultiThread(true)
        .ForEach([&](ECS::Entity entity, TestIntComponent& vel) {
            AtomicIncrement(&a);       
            entity.GetMut<TestIntComponent>()->a = 1;
            AtomicIncrement(&valueMap[entity]);
        });

    auto system2 = world.CreateSystem<TestIntComponent>()
        .Kind<Rendering>()
        .MultiThread(true)
        .ForEach([&](ECS::Entity entity, TestIntComponent& vel) {
            AtomicIncrement(&b);
        });

    auto system3 = world.CreateSystem<TestIntComponent>()
        .Kind<Rendering>()
        .ForEach([&](ECS::Entity entity, TestIntComponent& vel) {
            c = a + b;
        });

    auto pipeline = world.CreatePipeline()
        .Term(EcsCompSystem)
        .Term<Rendering>()
        .Build();

    world.RunPipeline(pipeline);

    auto query = world.CreateQuery<TestIntComponent>().Build();
    query.ForEach([&](ECS::Entity entity, TestIntComponent& vel) {
        if (vel.a != 1)
        {
            std::cout << entity << std::endl;
        }
     });

    Jobsystem::Uninitialize();

    for (int i = 0; i < ARRAYSIZE(valueMap); i++)
    {
        if (valueMap[i] > 1)
            std::cout << i << std::endl;
    }

    CHECK(a == 12500);
    CHECK(b == 12500);
    CHECK(c == 25000);
}

#endif