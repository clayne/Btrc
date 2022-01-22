#pragma once

#include <btrc/core/light_sampler/light_sampler.h>
#include <btrc/core/material/material.h>
#include <btrc/core/spectrum/spectrum.h>

BTRC_WAVEFRONT_BEGIN

struct GeometryInfo
{
    Vec4f *geometry_ex_tex_coord_u_a;
    Vec4f *geometry_ey_tex_coord_u_ba;
    Vec4f *geometry_ez_tex_coord_u_ca;

    Vec4f *shading_normals_tex_coord_v_a;
    Vec4f *shading_normals_tex_coord_v_ba;
    Vec4f *shading_normals_tex_coord_v_ca;
};

CUJ_PROXY_CLASS(
    CGeometryInfo,
    GeometryInfo,
    geometry_ex_tex_coord_u_a,
    geometry_ey_tex_coord_u_ba,
    geometry_ez_tex_coord_u_ca,
    shading_normals_tex_coord_v_a,
    shading_normals_tex_coord_v_ba,
    shading_normals_tex_coord_v_ca);

struct InstanceInfo
{
    GeometryInfo *geometry;
    int32_t       material_id = 0;
    int32_t       light_id = 0;
    Transform     transform;
};

CUJ_PROXY_CLASS(
    CInstanceInfo,
    InstanceInfo,
    geometry,
    material_id,
    light_id,
    transform);

struct SceneData
{
    std::vector<InstanceInfo>       instances;
    std::vector<RC<const Material>> materials;
    RC<const LightSampler>          light_sampler;
};

constexpr uint8_t RAY_MASK_ALL = 0xff;

BTRC_WAVEFRONT_END
