#include "ecs.h"

#include <string>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

struct Position 
{
    float  m1 = 0.0f;
    float  m2 = 0.0f;
};

struct TestComponent
{
    COMPONENT(TestComponent);
    std::vector<int> vec;

    TestComponent(TestComponent&& rhs) noexcept
    {
        vec = std::move(rhs.vec);
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

TEST_CASE("ECS", "Reflection")
{
    ECS::Reflect::ReflectInfo info = {};
    info.ctor = ECS::Reflect::Ctor<TestComponent>();
    info.dtor = ECS::Reflect::Dtor<TestComponent>();
    info.copy = ECS::Reflect::Copy<TestComponent>();
    info.move = ECS::Reflect::Move<TestComponent>();

    TestComponent a = {};
    a.vec.push_back(1);
    a.vec.push_back(2);
    a.vec.push_back(3);
    TestComponent b = std::move(a);
    CHECK(a.vec.size() == 0);
}

//TEST_CASE("ECS", "Simple flow")
//{
//    //ECS::World world;
//    //ECS::EntityID entity = world.CreateEntity("Test").with(TestComponent()).entity;
//    //ECS::NameComponent* nameComp = world.GetComponent<ECS::NameComponent>(entity);
//    //if (nameComp)
//    //    std::cout << nameComp->name << std::endl;
//
//    //TestComponent* testComp = world.GetComponent<TestComponent>(entity);
//    //if (testComp)
//    //    testComp->vec.push_back(1);
//
//    //testComp = world.GetComponent<TestComponent>(entity);
//}