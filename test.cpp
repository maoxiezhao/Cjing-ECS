#include "ecs.h"

#include <string>
#include <array>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

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
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    for (int i = 0; i < 500; i++)
    {
        world->CreateEntity((std::string("A") + std::to_string(i)).c_str())
            .With<PositionComponent>()
            .With<VelocityComponent>();
    }

    // ForEach
    ECS::EntityID system = world->CreateSystem<PositionComponent, VelocityComponent>()
        .ForEach([&updateTimes](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel)
        {
            updateTimes++;
        }
    );
    world->RunSystem(system);
    CHECK(updateTimes == 500);

    // Iter
    ECS::EntityID system1 = world->CreateSystem<PositionComponent, VelocityComponent>()
        .Iter([&updateTimes](ECS::EntityIterator iter, PositionComponent* pos, VelocityComponent* vel)
        {
            for(int i = 0; i < iter.Count(); i++)
                updateTimes++;
        }
    );
    world->RunSystem(system1);
    CHECK(updateTimes == 1000);


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
    // auto query = world->CreateQuery<PositionComponent, VelocityComponent>().Build();
    auto query1 = world->CreateQuery<PositionComponent>().Build();
    for (int i = 0; i < 5; i++)
    {
        world->CreateEntity((std::string("A") + std::to_string(i)).c_str())
            .With<PositionComponent>()
            .With<VelocityComponent>();
    }

    for (int i = 0; i < 5; i++)
    {
        world->CreateEntity((std::string("C") + std::to_string(i)).c_str())
            .With<PositionComponent>()
            .With<VelocityComponent>()
            .With<TestComponent>();
    }

    //query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
    //    aTimes++;
    //});
    //query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
    //    bTimes++;
    //});

    // CHECK(aTimes == bTimes);

    U32 cTimes = 0;
    query1.ForEach([&](ECS::EntityID entity, PositionComponent& pos) {
        cTimes++;
    });
    CHECK(cTimes == 10);
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
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    ECS::EntityID parent = world->CreateEntity("Parent")
        .With<PositionComponent>()
        .entity;
    ECS::EntityID child = world->CreateEntity("Child")
        .ChildOf(parent)
        .entity;

    ECS::EntityID target = world->GetParent(child);
    CHECK(target == parent);

    // Query
    auto query = world->CreateQuery<Position, Position, Position>()
        .TermIndex(0).Obj<Local>()
        .TermIndex(1).Obj<Global>()
        .TermIndex(2).Obj<Global>()
        .TermIndex(2)
        .Set(ECS::TermFlagParent | ECS::TermFlagCascade)
        .Build();

    std::vector<ECS::EntityID> queue;
    auto e1 = world->CreateEntity("e1")
        .With<Position, Local>({ 1.0f, 1.0f })
        .With<Position, Global>({ 11.0f, 11.0f });

    auto e2 = world->CreateEntity("e2")
        .ChildOf(e1.entity)
        .With<Position, Local>({ 2.0f, 2.0f })
        .With<Position, Global>({ 22.0f, 22.0f });

    auto e3 = world->CreateEntity("e3")
        .ChildOf(e2.entity)
        .With<Position, Local>({ 3.0f, 3.0f })
        .With<Position, Global>({ 33.0f, 33.0f });

    auto e4 = world->CreateEntity("e4")
        .ChildOf(e1.entity)
        .With<Position, Local>({ 4.0f, 4.0f })
        .With<Position, Global>({ 44.0f, 44.0f })
        .With<Health>();

    query.ForEach([&](ECS::EntityID entity, Position& p1, Position& p2, Position& pOut) {
        queue.push_back(entity);
    });

    CHECK(queue[0] == e1.entity);
    CHECK(queue[1] == e2.entity);
    CHECK(queue[2] == e4.entity);
    CHECK(queue[3] == e3.entity);
    queue.clear();

    std::vector<ECS::EntityID> children;
    world->EachChildren(e1.entity, [&](ECS::EntityID child) {
        children.push_back(child);
    });
    CHECK(children[0] == e2.entity);
    CHECK(children[1] == e4.entity);

    // System
    auto system = world->CreateSystem<Position, Position, Position>()
        .TermIndex(0).Obj<Local>()
        .TermIndex(1).Obj<Global>()
        .TermIndex(2).Obj<Global>()
        .TermIndex(2)
        .Set(ECS::TermFlagParent | ECS::TermFlagCascade)
        .ForEach([&](ECS::EntityID entity, Position& p1, Position& p2, Position& pOut) {
            queue.push_back(entity);
        });
    world->RunSystem(system);
    CHECK(queue[0] == e1.entity);
    CHECK(queue[1] == e2.entity);
    CHECK(queue[2] == e4.entity);
    CHECK(queue[3] == e3.entity);

    const char* name = world->GetEntityName(e1.entity);
    CHECK(std::string(name) == "e1");
}

//int main()
//{
//    U32 aTimes = 0;
//    U32 bTimes = 0;
//    std::unique_ptr<ECS::World> world = ECS::World::Create();
//    for (int i = 0; i < 5; i++)
//    {
//        world->CreateEntity((std::string("A") + std::to_string(i)).c_str())
//            .With<PositionComponent>()
//            .With<VelocityComponent>();
//    }
//
//    auto query = world->CreateQuery<PositionComponent, VelocityComponent>().Build();
//    query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
//        aTimes++;
//        });
//    query.ForEach([&](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel) {
//        bTimes++;
//        });
//
//    assert(aTimes == bTimes);
//}