#include <gtest/gtest.h>
#include "Core/Timestep.h"

using Engine::Timestep;

TEST(Timestep, DefaultConstructorIsZero)
{
    Timestep ts;
    EXPECT_FLOAT_EQ(static_cast<float>(ts), 0.0f);
}

TEST(Timestep, ConstructWithValue)
{
    Timestep ts(0.016f);
    EXPECT_FLOAT_EQ(static_cast<float>(ts), 0.016f);
}

TEST(Timestep, ImplicitConversionToFloat)
{
    Timestep ts(1.5f);
    float    value = ts;
    EXPECT_FLOAT_EQ(value, 1.5f);
}

TEST(Timestep, GetSeconds)
{
    Timestep ts(2.5f);
    EXPECT_FLOAT_EQ(ts.GetSeconds(), 2.5f);
}

TEST(Timestep, GetMilliseconds)
{
    Timestep ts(0.016f);
    EXPECT_FLOAT_EQ(ts.GetMilliseconds(), 16.0f);
}

TEST(Timestep, NegativeTimestep)
{
    Timestep ts(-1.0f);
    EXPECT_FLOAT_EQ(ts.GetSeconds(), -1.0f);
    EXPECT_FLOAT_EQ(ts.GetMilliseconds(), -1000.0f);
}
