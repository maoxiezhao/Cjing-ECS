#include "ecs.h"

#include <string>

struct Position 
{
    float  m1 = 0.0f;
    float  m2 = 0.0f;
};

struct TestComponent
{
    std::vector<int> vec;
};

int main()
{
    ECS::World world;
    world.CreateEntity("Test");

    return 0;
}

