﻿#include "ecs.h"

#include <string>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

struct Position 
{
    float  m1 = 0.0f;
    float  m2 = 0.0f;
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

TEST_CASE("SimpleFlow", "ECS")
{
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    world->CreateEntity("AABB").with(TestComponent());
    //ECS::EntityID entity = world.CreateEntity("Test").with(TestComponent()).entity;
    //ECS::NameComponent* nameComp = world.GetComponent<ECS::NameComponent>(entity);
    //if (nameComp)
    //    std::cout << nameComp->name << std::endl;

    //TestComponent* testComp = world.GetComponent<TestComponent>(entity);
    //if (testComp)
    //    testComp->vec.push_back(1);

    //testComp = world.GetComponent<TestComponent>(entity);
    int temp = 1;
    CHECK(temp == 1);
}