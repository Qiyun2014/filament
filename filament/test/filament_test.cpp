/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>

#include <gtest/gtest.h>

#include <math/vec3.h>
#include <math/vec4.h>
#include <math/mat4.h>

#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Frustum.h>
#include <filament/Material.h>
#include <filament/Engine.h>

#include "driver/UniformBuffer.h"
#include <filament/UniformInterfaceBlock.h>

#include "details/Allocators.h"
#include "details/Material.h"
#include "details/Camera.h"
#include "details/Froxelizer.h"
#include "details/Engine.h"
#include "components/TransformManager.h"
#include "utils/RangeSet.h"

using namespace filament;
using namespace math;
using namespace utils;

static bool isGray(math::float3 v) {
    return v.r == v.g && v.g == v.b;
}

static bool almostEqualUlps(float a, float b, int maxUlps) {
    if (a == b) return true;
    int intDiff = abs(*reinterpret_cast<int32_t*>(&a) - *reinterpret_cast<int32_t*>(&b));
    return intDiff <= maxUlps;
}

static bool vec3eq(math::float3 a, math::float3 b) {
    return  almostEqualUlps(a.x, b.x, 1) &&
            almostEqualUlps(a.y, b.y, 1) &&
            almostEqualUlps(a.z, b.z, 1);
}

TEST(FilamentTest, TransformManager) {
    filament::details::FTransformManager tcm;
    EntityManager& em = EntityManager::get();
    std::array<Entity, 3> entities;
    em.create(entities.size(), entities.data());

    // test component creation
    tcm.create(entities[0]);
    EXPECT_TRUE(tcm.hasComponent(entities[0]));
    TransformManager::Instance parent = tcm.getInstance(entities[0]);
    EXPECT_TRUE(bool(parent));

    // test component creation with parent
    tcm.create(entities[1], parent, mat4f{});
    EXPECT_TRUE(tcm.hasComponent(entities[1]));
    TransformManager::Instance child = tcm.getInstance(entities[1]);
    EXPECT_TRUE(bool(child));

    // test default values
    EXPECT_EQ(tcm.getTransform(parent), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getWorldTransform(parent), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getTransform(child), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getWorldTransform(child), mat4f{ float4{ 1 }});

    // test setting a transform
    tcm.setTransform(parent, mat4f{ float4{ 2 }});

    // test local and world transform propagation
    EXPECT_EQ(tcm.getTransform(parent), mat4f{ float4{ 2 }});
    EXPECT_EQ(tcm.getWorldTransform(parent), mat4f{ float4{ 2 }});
    EXPECT_EQ(tcm.getTransform(child), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getWorldTransform(child), mat4f{ float4{ 2 }});

    // test local transaction
    tcm.openLocalTransformTransaction();
    tcm.setTransform(parent, mat4f{ float4{ 4 }});

    // check the transfroms ARE NOT propagated
    EXPECT_EQ(tcm.getTransform(parent), mat4f{ float4{ 4 }});
    EXPECT_EQ(tcm.getWorldTransform(parent), mat4f{ float4{ 2 }});
    EXPECT_EQ(tcm.getTransform(child), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getWorldTransform(child), mat4f{ float4{ 2 }});

    tcm.commitLocalTransformTransaction();
    // test propagation after closing the transaction
    EXPECT_EQ(tcm.getTransform(parent), mat4f{ float4{ 4 }});
    EXPECT_EQ(tcm.getWorldTransform(parent), mat4f{ float4{ 4 }});
    EXPECT_EQ(tcm.getTransform(child), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getWorldTransform(child), mat4f{ float4{ 4 }});

    //
    // test out-of-order parent/child
    //

    tcm.create(entities[2]);
    EXPECT_TRUE(tcm.hasComponent(entities[2]));
    TransformManager::Instance newParent = tcm.getInstance(entities[2]);
    EXPECT_TRUE(bool(newParent));

    // test reparenting
    tcm.setParent(child, newParent);

    // make sure child/parent are out of order
    ASSERT_LT(child, newParent);

    // local transaction reprders parent/child
    tcm.openLocalTransformTransaction();
    tcm.setTransform(newParent, mat4f{ float4{ 8 }});
    tcm.commitLocalTransformTransaction();

    // local transaction invalidates Instances
    parent = tcm.getInstance(entities[0]);
    child = tcm.getInstance(entities[1]);
    newParent = tcm.getInstance(entities[2]);

    // check parent / child order is correct
    EXPECT_GT(child, newParent);

    // check transform propagation
    EXPECT_EQ(tcm.getTransform(newParent), mat4f{ float4{ 8 }});
    EXPECT_EQ(tcm.getWorldTransform(newParent), mat4f{ float4{ 8 }});
    EXPECT_EQ(tcm.getTransform(child), mat4f{ float4{ 1 }});
    EXPECT_EQ(tcm.getWorldTransform(child), mat4f{ float4{ 8 }});
}

TEST(FilamentTest, UniformInterfaceBlock) {

    UniformInterfaceBlock::Builder b;

    b.name("TestUniformInterfaceBlock");
    b.add("a_float_0", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_1", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_2", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_3", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_vec4_0",  1, UniformInterfaceBlock::Type::FLOAT4);
    b.add("a_float_4", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_5", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_6", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_vec3_0",  1, UniformInterfaceBlock::Type::FLOAT3);
    b.add("a_float_7", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float[3]",3, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_8", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_mat3_0",  1, UniformInterfaceBlock::Type::MAT3);
    b.add("a_mat4_0",  1, UniformInterfaceBlock::Type::MAT4);
    b.add("a_mat3[3]", 3, UniformInterfaceBlock::Type::MAT3);


    UniformInterfaceBlock ib(b.build());
    auto const& info = ib.getUniformInfoList();

    // test that 4 floats are packed together
    EXPECT_EQ(0, info[0].offset);
    EXPECT_EQ(1, info[1].offset);
    EXPECT_EQ(2, info[2].offset);
    EXPECT_EQ(3, info[3].offset);

    // test the double4 is where it should be
    EXPECT_EQ(4, info[4].offset);

    // check 3 following floats are packed right after the double4
    EXPECT_EQ(8, info[5].offset);
    EXPECT_EQ(9, info[6].offset);
    EXPECT_EQ(10, info[7].offset);

    // check that the following double3 is aligned to the next double4 boundary
    EXPECT_EQ(12, info[8].offset);

    // check that the following float is just behind the double3
    EXPECT_EQ(15, info[9].offset);

    // check that arrays are aligned on double4 and have a stride of double4
    EXPECT_EQ(16, info[10].offset);
    EXPECT_EQ(4, info[10].stride);
    EXPECT_EQ(3, info[10].size);

    // check the base offset of the member following the array is rounded up to the next multiple of the base alignment.
    EXPECT_EQ(28, info[11].offset);

    // check mat3 alignment is double4
    EXPECT_EQ(32, info[12].offset);
    EXPECT_EQ(12, info[12].stride);

    // check following mat4 is 3*double4 away
    EXPECT_EQ(44, info[13].offset);
    EXPECT_EQ(16, info[13].stride);

    // arrays of matrices
    EXPECT_EQ(60, info[14].offset);
    EXPECT_EQ(12, info[14].stride);
    EXPECT_EQ(3, info[14].size);
}

TEST(FilamentTest, UniformBuffer) {

    struct ubo {
                    float   f0;
                    float   f1;
                    float   f2;
                    float   f3;
        alignas(16) float4 v0;
                    float   f4;
                    float   f5;
                    float   f6;
        alignas(16) float3 v1;     // double3 are aligned to 4 floats
                    float   f7;
                    struct {
                        alignas(16) float v;    // arrays entries are always aligned to 4 floats
                    } u[3];
                    float   f8;
        alignas(16) float4 m0[3];  // mat3 are like vec4f[3]
        alignas(16) mat4f   m1;
    };

    auto CHECK = [](ubo const* data) {
        EXPECT_EQ(1.0f, data->f0);
        EXPECT_EQ(3.0f, data->f1);
        EXPECT_EQ(5.0f, data->f2);
        EXPECT_EQ(7.0f, data->f3);
        EXPECT_EQ((float4{ -1.1f, -1.2f, 3.14f, sqrtf(2)}), data->v0);
        EXPECT_EQ(11.0f, data->f4);
        EXPECT_EQ(13.0f, data->f5);
        EXPECT_EQ(17.0f, data->f6);
        EXPECT_EQ((float3{ 1, 2, 3}), data->v1);
        EXPECT_EQ(19.0f, data->f7);
        EXPECT_EQ(-3.0f, data->u[0].v);
        EXPECT_EQ(-5.0f, data->u[1].v);
        EXPECT_EQ(-7.0f, data->u[2].v);
        EXPECT_EQ(23.0f, data->f8);
        EXPECT_EQ((mat4f{100, 200, 300, 0, 400, 500, 600, 0, 700, 800, 900, 0, 0, 0, 0, 1}), data->m1);
    };

    auto CHECK2 = [](std::vector<UniformInterfaceBlock::UniformInfo> const& info) {
        EXPECT_EQ(offsetof(ubo, f0)/4, info[0].offset);
        EXPECT_EQ(offsetof(ubo, f1)/4, info[1].offset);
        EXPECT_EQ(offsetof(ubo, f2)/4, info[2].offset);
        EXPECT_EQ(offsetof(ubo, f3)/4, info[3].offset);
        EXPECT_EQ(offsetof(ubo, v0)/4, info[4].offset);
        EXPECT_EQ(offsetof(ubo, f4)/4, info[5].offset);
        EXPECT_EQ(offsetof(ubo, f5)/4, info[6].offset);
        EXPECT_EQ(offsetof(ubo, f6)/4, info[7].offset);
        EXPECT_EQ(offsetof(ubo, v1)/4, info[8].offset);
        EXPECT_EQ(offsetof(ubo, f7)/4, info[9].offset);
        EXPECT_EQ(offsetof(ubo,  u)/4, info[10].offset);
        EXPECT_EQ(offsetof(ubo, f8)/4, info[11].offset);
        EXPECT_EQ(offsetof(ubo, m0)/4, info[12].offset);
        EXPECT_EQ(offsetof(ubo, m1)/4, info[13].offset);
    };

    UniformInterfaceBlock::Builder b;
    b.name("TestUniformBuffer");
    b.add("a_float_0", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_1", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_2", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_3", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_vec4_0",  1, UniformInterfaceBlock::Type::FLOAT4);
    b.add("a_float_4", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_5", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_6", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_vec3_0",  1, UniformInterfaceBlock::Type::FLOAT3);
    b.add("a_float_7", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float[3]",3, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_float_8", 1, UniformInterfaceBlock::Type::FLOAT);
    b.add("a_mat3_0",  1, UniformInterfaceBlock::Type::MAT3);
    b.add("a_mat4_0",  1, UniformInterfaceBlock::Type::MAT4);
    UniformInterfaceBlock ib(b.build());

    CHECK2(ib.getUniformInfoList());

    EXPECT_EQ(sizeof(ubo), ib.getSize());

    UniformBuffer buffer(sizeof(ubo));
    ubo const* data = static_cast<ubo const*>(buffer.getBuffer());

    buffer.setUniform(offsetof(ubo, f0), 1.0f);
    buffer.setUniform(offsetof(ubo, f1), 3.0f);
    buffer.setUniform(offsetof(ubo, f2), 5.0f);
    buffer.setUniform(offsetof(ubo, f3), 7.0f);
    buffer.setUniform(offsetof(ubo, v0), float4{ -1.1f, -1.2f, 3.14f, sqrtf(2) });
    buffer.setUniform(offsetof(ubo, f4), 11.0f);
    buffer.setUniform(offsetof(ubo, f5), 13.0f);
    buffer.setUniform(offsetof(ubo, f6), 17.0f);
    buffer.setUniform(offsetof(ubo, v1), float3{ 1, 2, 3 });
    buffer.setUniform(offsetof(ubo, f7), 19.0f);
    buffer.setUniform(offsetof(ubo, u[0].v), -3.0f);
    buffer.setUniform(offsetof(ubo, u[1].v), -5.0f);
    buffer.setUniform(offsetof(ubo, u[2].v), -7.0f);
    buffer.setUniform(offsetof(ubo, f8), 23.0f);
    buffer.setUniform(offsetof(ubo, m0), mat3f{10,20,30, 40,50,60, 70,80,90 });
    buffer.setUniform(offsetof(ubo, m1), mat4f{100,200,300,0, 400,500,600,0, 700,800,900,0, 0,0,0,1 });

    CHECK(data);

    ubo copy(*data);
    CHECK(data);
    CHECK(&copy);

    ubo move(std::move(copy));
    CHECK(&move);

    copy = *data;
    CHECK(data);
    CHECK(&copy);

    move = std::move(copy);
    CHECK(&move);

    //buffer.log(std::cout, ib);
}

TEST(FilamentTest, BoxCulling) {
    Frustum frustum(mat4f::frustum(-1, 1, -1, 1, 1, 100));

    // a cube centered in 0 of size 1
    Box box = { 0, 0.5f };

    // box fully inside
    EXPECT_TRUE( frustum.intersects(box.translateTo({ 0,  0, -10})) );

    // box clipped by the near or far plane
    EXPECT_TRUE( frustum.intersects(box.translateTo({ 0,  0,   -1})) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({ 0,  0, -100})) );

    // box clipped by one or several planes of the frustum for any z, but still visible
    EXPECT_TRUE( frustum.intersects(box.translateTo({ -10,   0, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({  10,   0, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({   0, -10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({   0,  10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({ -10, -10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({  10,  10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({  10, -10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({ -10,  10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({ -10,  10, -10 })) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({  10, -10, -10 })) );

    // box outside frustum planes
    EXPECT_FALSE( frustum.intersects(box.translateTo({ 0,     0,    0})) );
    EXPECT_FALSE( frustum.intersects(box.translateTo({ 0,     0, -101})) );
    EXPECT_FALSE( frustum.intersects(box.translateTo({-1.51,  0, -0.5})) );

    // slightly inside the frustum
    EXPECT_TRUE( frustum.intersects(box.translateTo({-1.49,  0, -0.5})) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({-100,   0, -100})) );

    // expected false classification (the box is not visible, but its classified as visible)
    EXPECT_TRUE( frustum.intersects(box.translateTo({-100.51, 0, -100})) );
    EXPECT_TRUE( frustum.intersects(box.translateTo({-100.99, 0, -100})) );
    EXPECT_FALSE(frustum.intersects(box.translateTo({-101.01, 0, -100})) ); // good again

    // A box that entirely contain the frustum
    EXPECT_TRUE( frustum.intersects( { 0, 200 }) );
}

TEST(FilamentTest, SphereCulling) {
    Frustum frustum(mat4f::frustum(-1, 1, -1, 1, 1, 100));

    // a sphere centered in 0 of size 1
    float4 sphere = { 0, 0, 0, 0.5f };

    // sphere fully inside
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 0, 0, -10, 0}) );

    // sphere clipped by the near or far plane
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 0, 0, -1, 0}) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 0, 0, -100, 0}) );

    // sphere clipped by one or several planes of the frustum for any z, but still visible
    EXPECT_TRUE( frustum.intersects(sphere + float4{ -10, 0, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 10, 0, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 0, -10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 0, 10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ -10, -10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 10, 10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 10, -10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ -10, 10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ -10, 10, -10, 0 }) );
    EXPECT_TRUE( frustum.intersects(sphere + float4{ 10, -10, -10, 0 }) );

    // sphere outside frustum planes
    EXPECT_FALSE( frustum.intersects(sphere + float4{ 0, 0, 0, 0}) );
    EXPECT_FALSE( frustum.intersects(sphere + float4{ 0, 0, -101, 0}) );
    EXPECT_FALSE( frustum.intersects(sphere + float4{ -1.51, 0, -0.5, 0}) );

    // slightly inside the frustum
    EXPECT_TRUE( frustum.intersects(sphere + float4{ -100, 0, -100, 0}) );

    // A sphere that entirely contain the frustum
    EXPECT_TRUE(frustum.intersects({ 0, 200 }));
}

TEST(FilamentTest, ColorConversion) {
    // Linear to Gamma
    // 0.0 stays 0.0
    EXPECT_PRED2(vec3eq, (sRGBColor{0.0f, 0.0f, 0.0f}), Color::toSRGB<FAST>(LinearColor{0.0f}));
    // 1.0 stays 1.0
    EXPECT_PRED2(vec3eq, (sRGBColor{1.0f, 0.0f, 0.0f}), Color::toSRGB<FAST>({1.0f, 0.0f, 0.0f}));

    // 0.0 stays 0.0
    EXPECT_PRED2(vec3eq, (sRGBColor{0.0f, 0.0f, 0.0f}),
            Color::toSRGB<ACCURATE>(LinearColor{0.0f}));
    // 1.0 stays 1.0
    EXPECT_PRED2(vec3eq, (sRGBColor{1.0f, 0.0f, 0.0f}),
            Color::toSRGB<ACCURATE>({1.0f, 0.0f, 0.0f}));

    // 0.5 is > 0.5
    EXPECT_LT((sRGBColor{0.5f, 0.0f, 0.0f}), Color::toSRGB<FAST>({0.5f, 0.0f, 0.0f}));
    // 0.5 is > 0.5
    EXPECT_LT((sRGBColor{0.5f, 0.0f, 0.0f}), Color::toSRGB<ACCURATE>({0.5f, 0.0f, 0.0f}));

    EXPECT_PRED1(isGray, Color::toSRGB<FAST>(LinearColor{0.5f}));
    EXPECT_PRED1(isGray, Color::toSRGB<ACCURATE>(LinearColor{0.5f}));

    // Gamma to Linear
    // 0.0 stays 0.0
    EXPECT_PRED2(vec3eq, (LinearColor{0.0f, 0.0f, 0.0f}), Color::toLinear<FAST>(sRGBColor{0.0f}));
    // 1.0 stays 1.0
    EXPECT_PRED2(vec3eq, (LinearColor{1.0f, 0.0f, 0.0f}), Color::toLinear<FAST>({1.0f, 0.0f, 0.0f}));

    // 0.0 stays 0.0
    EXPECT_PRED2(vec3eq, (LinearColor{0.0f, 0.0f, 0.0f}), Color::toLinear<ACCURATE>(sRGBColor{0.0f}));
    // 1.0 stays 1.0
    EXPECT_PRED2(vec3eq, (LinearColor{1.0f, 0.0f, 0.0f}), Color::toLinear<ACCURATE>({1.0f, 0.0f, 0.0f}));

    // 0.5 is < 0.5
    EXPECT_GT((LinearColor{0.5f, 0.0f, 0.0f}), Color::toLinear<FAST>({0.5f, 0.0f, 0.0f}));
    // 0.5 is < 0.5
    EXPECT_GT((LinearColor{0.5f, 0.0f, 0.0f}), Color::toLinear<ACCURATE>({0.5f, 0.0f, 0.0f}));

    EXPECT_PRED1(isGray, Color::toLinear<FAST>(sRGBColor{0.5f}));
    EXPECT_PRED1(isGray, Color::toLinear<ACCURATE>(sRGBColor{0.5f}));
}


TEST(FilamentTest, FroxelData) {
    using namespace filament;
    using namespace filament::details;

    FEngine* engine = FEngine::create();

    LinearAllocatorArena arena("FRenderer: per-frame allocator", FEngine::CONFIG_PER_RENDER_PASS_ARENA_SIZE);
    utils::ArenaScope<LinearAllocatorArena> scope(arena);


    // view-port size is chosen so that we fit exactly a integer # of froxels horizontally
    // (unfortunately there is no way to guarantee it as it depends on the max # of froxel
    // used by the engine). We do this to infer the value of the left and right most planes
    // to check if they're computed correctly.
    Viewport vp(0, 0, 1280, 640);
    mat4f p = mat4f::perspective(90, 1.0f, 0.1, 100, mat4f::Fov::HORIZONTAL);

    Froxelizer froxelData(*engine);
    froxelData.setOptions(5, 100);
    froxelData.prepare(engine->getDriverApi(), scope, vp, p, 0.1, 100);

    Froxel f = froxelData.getFroxelAt(0,0,0);

    // 45-deg plane, with normal pointing outward to the left
    EXPECT_FLOAT_EQ(-M_SQRT2/2, f.planes[Froxel::LEFT].x);
    EXPECT_FLOAT_EQ(         0, f.planes[Froxel::LEFT].y);
    EXPECT_FLOAT_EQ( M_SQRT2/2, f.planes[Froxel::LEFT].z);

    // the right side of froxel 1 is near 45-deg plane pointing outward to the right
    EXPECT_TRUE(f.planes[Froxel::RIGHT].x > 0);
    EXPECT_FLOAT_EQ(0, f.planes[Froxel::RIGHT].y);
    EXPECT_TRUE(f.planes[Froxel::RIGHT].z < 0);

    // right side of last horizontal froxel is 45-deg plane pointing outward to the right
    Froxel g = froxelData.getFroxelAt(froxelData.getFroxelCountX()-1,0,0);
    EXPECT_FLOAT_EQ(M_SQRT2/2, g.planes[Froxel::RIGHT].x);
    EXPECT_FLOAT_EQ(        0, g.planes[Froxel::RIGHT].y);
    EXPECT_FLOAT_EQ(M_SQRT2/2, g.planes[Froxel::RIGHT].z);

    // first froxel near plane facing us
    EXPECT_FLOAT_EQ(        0, f.planes[Froxel::NEAR].x);
    EXPECT_FLOAT_EQ(        0, f.planes[Froxel::NEAR].y);
    EXPECT_FLOAT_EQ(        1, f.planes[Froxel::NEAR].z);

    // first froxel far plane away from us
    EXPECT_FLOAT_EQ(        0, f.planes[Froxel::FAR].x);
    EXPECT_FLOAT_EQ(        0, f.planes[Froxel::FAR].y);
    EXPECT_FLOAT_EQ(       -1, f.planes[Froxel::FAR].z);

    // first froxel near plane distance always 0
    EXPECT_FLOAT_EQ(        0, f.planes[Froxel::NEAR].w);

    // first froxel far plane distance always zLightNear
    EXPECT_FLOAT_EQ(        5,-f.planes[Froxel::FAR].w);

    Froxel l = froxelData.getFroxelAt(0, 0, froxelData.getFroxelCountZ()-1);

    // farthest froxel far plane distance always zLightFar
    EXPECT_FLOAT_EQ(        100,-l.planes[Froxel::FAR].w);

    // create a dummy point light that can be referenced in LightSoa
    Entity e = engine->getEntityManager().create();
    LightManager::Builder(LightManager::Type::POINT).build(*engine, e);
    LightManager::Instance instance = engine->getLightManager().getInstance(e);

    FScene::LightSoa lights;
    lights.push_back({}, {}, {}, {});   // first one is always skipped
    lights.push_back(float4{ 0, 0, -5, 1 }, {}, instance, 1);

    {
        froxelData.froxelizeLights(*engine, {}, lights);
        auto const& froxelBuffer = froxelData.getFroxelBufferUser();
        auto const& recordBuffer = froxelData.getRecordBufferUser();
        // light straddles the "light near" plane
        size_t pointCount = 0;
        for (const auto& entry : froxelBuffer) {
            EXPECT_LE(entry.pointLightCount, 1);
            EXPECT_EQ(entry.spotLightCount, 0);
            pointCount += entry.pointLightCount;
        }
        EXPECT_GT(pointCount, 0);
    }

    {
        // light doesn't cross any froxel near or far plane
        lights.elementAt<FScene::POSITION_RADIUS>(1) = float4{ 0, 0, -3, 1 };

        auto pos = lights.elementAt<FScene::POSITION_RADIUS>(1);
        EXPECT_TRUE(pos == float4( 0, 0, -3, 1 ));

        froxelData.froxelizeLights(*engine, {}, lights);
        auto const& froxelBuffer = froxelData.getFroxelBufferUser();
        auto const& recordBuffer = froxelData.getRecordBufferUser();
        size_t pointCount = 0;
        for (const auto& entry : froxelBuffer) {
            EXPECT_LE(entry.pointLightCount, 1);
            EXPECT_EQ(entry.spotLightCount, 0);
            pointCount += entry.pointLightCount;
        }
        EXPECT_GT(pointCount, 0);
    }

    froxelData.terminate(engine->getDriverApi());
    engine->shutdown();
    delete engine;
}

TEST(FilamentTest, RangeSet) {

    utils::RangeSet<4> rs;
    utils::BufferRange const* b = rs.cbegin();

    EXPECT_TRUE(rs.isEmpty());

    // add a range
    rs.set(10,20);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(10, b[0].start);
    EXPECT_EQ(30, b[0].end);

    // add range at the end w/o overlap
    rs.set(35, 5);
    EXPECT_EQ(2, rs.cend() - rs.cbegin());
    EXPECT_EQ(10, b[0].start);
    EXPECT_EQ(30, b[0].end);
    EXPECT_EQ(35, b[1].start);
    EXPECT_EQ(40, b[1].end);

    // add range at the end w/o overlap
    rs.set(60, 10);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(10, b[0].start);
    EXPECT_EQ(30, b[0].end);
    EXPECT_EQ(35, b[1].start);
    EXPECT_EQ(40, b[1].end);
    EXPECT_EQ(60, b[2].start);
    EXPECT_EQ(70, b[2].end);

    // add range at the begining w/o overlap
    rs.set(0, 5);
    EXPECT_EQ(4, rs.cend() - rs.cbegin());
    EXPECT_EQ( 0, b[0].start);
    EXPECT_EQ( 5, b[0].end);
    EXPECT_EQ(10, b[1].start);
    EXPECT_EQ(30, b[1].end);
    EXPECT_EQ(35, b[2].start);
    EXPECT_EQ(40, b[2].end);
    EXPECT_EQ(60, b[3].start);
    EXPECT_EQ(70, b[3].end);

    // test overflow
    // ... last range
    rs.set(80, 5);
    EXPECT_EQ(4, rs.cend() - rs.cbegin());
    EXPECT_EQ( 0, b[0].start);
    EXPECT_EQ( 5, b[0].end);
    EXPECT_EQ(10, b[1].start);
    EXPECT_EQ(30, b[1].end);
    EXPECT_EQ(35, b[2].start);
    EXPECT_EQ(40, b[2].end);
    EXPECT_EQ(60, b[3].start);
    EXPECT_EQ(85, b[3].end);

    // ... overlaping begining of a range
    rs.set(7, 5);
    EXPECT_EQ(4, rs.cend() - rs.cbegin());
    EXPECT_EQ( 0, b[0].start);
    EXPECT_EQ( 5, b[0].end);
    EXPECT_EQ( 7, b[1].start);
    EXPECT_EQ(30, b[1].end);
    EXPECT_EQ(35, b[2].start);
    EXPECT_EQ(40, b[2].end);
    EXPECT_EQ(60, b[3].start);
    EXPECT_EQ(85, b[3].end);

    // ... overlapping end of a range
    // (in that case, we merge with the following range)
    rs.set(27, 5);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ( 0, b[0].start);
    EXPECT_EQ( 5, b[0].end);
    EXPECT_EQ( 7, b[1].start);
    EXPECT_EQ(40, b[1].end);
    EXPECT_EQ(60, b[2].start);
    EXPECT_EQ(85, b[2].end);

    // test clear
    rs.clear();
    EXPECT_EQ(b, rs.cbegin());
    EXPECT_EQ(b, rs.cend());

    // test fully overlapping
    rs.set(0, 1000);
    rs.set(10, 10);
    rs.set(40, 10);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(0, b[0].start);
    EXPECT_EQ(1000, b[0].end);


    // test merging at the end
    rs.set(1000, 100);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(0, b[0].start);
    EXPECT_EQ(1100, b[0].end);

    // test merging at the end with overlap
    rs.set(1000, 200);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(0, b[0].start);
    EXPECT_EQ(1200, b[0].end);

    // test merge at the begining
    rs.clear();
    rs.set(100, 10);
    rs.set(50, 50);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(50, b[0].start);
    EXPECT_EQ(110, b[0].end);

    // test merge at the begining with overlap
    rs.set(40, 40);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(40, b[0].start);
    EXPECT_EQ(110, b[0].end);

    // test merging a larger range
    rs.set(0, 1000);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(0, b[0].start);
    EXPECT_EQ(1000, b[0].end);


    // test merging in the middle
    rs.clear();
    rs.set(  0, 50);
    rs.set(100, 50);
    rs.set(200, 50);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ(100, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... to the left w/ overlap
    rs.set(90, 20);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ( 90, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... to the left w/o overlap
    rs.set(80, 10);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ( 80, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... to the right w/ overlap
    rs.set(140, 20);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ( 80, b[1].start);
    EXPECT_EQ(160, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... to the right w/o overlap
    rs.set(160, 10);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ( 80, b[1].start);
    EXPECT_EQ(170, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // fill a gap w/o overlap
    rs.set(50, 30);
    EXPECT_EQ(2, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ(170, b[0].end);
    EXPECT_EQ(200, b[1].start);
    EXPECT_EQ(250, b[1].end);

    // fill a gap w/ overlap
    rs.set(150, 60);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ(250, b[0].end);

    // overlap 2 different range swallow the middle one
    rs.clear();
    rs.set(  0, 50);
    rs.set(100, 50);
    rs.set(200, 50);
    rs.set(25, 200);
    EXPECT_EQ(1, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ(250, b[0].end);


    // test matching start and/or ends
    rs.clear();
    rs.set(  0, 50);
    rs.set(100, 50);
    rs.set(200, 50);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ(100, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... match begin
    rs.set(100, 10);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ(100, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... match end
    rs.set(140, 10);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ(100, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);

    // ... match both
    rs.set(100, 50);
    EXPECT_EQ(3, rs.cend() - rs.cbegin());
    EXPECT_EQ(  0, b[0].start);
    EXPECT_EQ( 50, b[0].end);
    EXPECT_EQ(100, b[1].start);
    EXPECT_EQ(150, b[1].end);
    EXPECT_EQ(200, b[2].start);
    EXPECT_EQ(250, b[2].end);
}


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
