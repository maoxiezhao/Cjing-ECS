#include "ecs.h"

#include <string>

struct Position 
{
    float  m1 = 0.0f;
    float  m2 = 0.0f;
};

struct TestComponent
{
    COMPONENT(TestComponent);
    std::vector<int> vec;
};

int main()
{
    ECS::World world;
    ECS::EntityID entity = world.CreateEntity("Test").with(TestComponent()).entity;
    ECS::NameComponent* nameComp = world.GetComponent<ECS::NameComponent>(entity);
    if (nameComp)
        std::cout << nameComp->name << std::endl;

    TestComponent* testComp = world.GetComponent<TestComponent>(entity);
    if (testComp)
        testComp->vec.push_back(1);

    testComp = world.GetComponent<TestComponent>(entity);
    return 0;
}

