#include "ecs.h"

#include <string>
#include <array>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

struct PositionComponent
{
    COMPONENT(PositionComponent);
    float x = 0.0f;
    float y = 0.0f;
};

struct VelocityComponent
{
    COMPONENT(VelocityComponent);
    float x = 0.0f;
    float y = 0.0f;
};

static int clearTimes = 0;
struct TestComponent
{
    COMPONENT(TestComponent);
    std::vector<int> vec;

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

TEST_CASE("Reflect", "ECS")
{
    ECS::Reflect::ReflectInfo info = {};
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
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    for (int i = 0; i < 500; i++)
    {
        world->CreateEntity((std::string("A") + std::to_string(i)).c_str())
            .With<PositionComponent>()
            .With<VelocityComponent>();
    }

    ECS::EntityID system = world->CreateSystem<PositionComponent, VelocityComponent>()
        .ForEach([&updateTimes](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel)
        {
            updateTimes++;
        }
    );
    world->RunSystem(system);
    CHECK(updateTimes == 500);
}

TEST_CASE("SingletonComponent", "ECS")
{
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    PositionComponent* a = world->GetSingletonComponent<PositionComponent>();
    a->x = 1.0f;
    
    PositionComponent* b = world->GetSingletonComponent<PositionComponent>();
    b->x = 2.0f;

    PositionComponent* c = world->GetSingletonComponent<PositionComponent>();
    CHECK(c->x == 2.0f);
}

TEST_CASE("Prefab", "ECS")
{
    // Default
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    ECS::EntityID prefab = world->CreatePrefab("TestPrefab")
        .With<PositionComponent>()
        .With<VelocityComponent>()
        .entity;

    ECS::EntityID test1 = world->CreateEntity("Test1")
        .Instantiate(prefab)
        .With<TestComponent>().entity;

    ECS::EntityID test2 = world->CreateEntity("Test2")
        .Instantiate(prefab)
        .With<TestComponent>().entity;

    PositionComponent* pos1 = world->GetComponent<PositionComponent>(test1);
    PositionComponent* pos2 = world->GetComponent<PositionComponent>(test2);
    CHECK(pos1 != pos2);

    pos1->x = 3.0f;
    CHECK(pos1->x == world->GetComponent<PositionComponent>(test1)->x);
    pos2->x = 2.0f;
    CHECK(pos2->x == world->GetComponent<PositionComponent>(test2)->x);

    // SharedComponent

}

TEST_CASE("Query", "ECS")
{
    U32 aTimes = 0;
    U32 bTimes = 0;
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    for (int i = 0; i < 5; i++)
    {
        world->CreateEntity((std::string("A") + std::to_string(i)).c_str())
            .With<PositionComponent>()
            .With<VelocityComponent>();
    }

    auto& query = world->CreateQuery<PositionComponent, VelocityComponent>().Cached().Build();
    query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
        aTimes++;
    });
    query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
        bTimes++;
    });

    CHECK(aTimes == bTimes);
}

TEST_CASE("ChildOf", "ECS")
{
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    ECS::EntityID parent = world->CreateEntity("Parent")
        .With<PositionComponent>()
        .entity;
    ECS::EntityID child = world->CreateEntity("Child")
        .ChildOf(parent)
        .entity;

    ECS::EntityID target = world->GetParent(child);
    CHECK(target == parent);
}