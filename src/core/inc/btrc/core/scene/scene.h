#pragma once

#include <btrc/core/camera/camera.h>
#include <btrc/core/light/light.h>
#include <btrc/core/light_sampler/light_sampler.h>
#include <btrc/core/geometry/geometry.h>
#include <btrc/core/material/material.h>
#include <btrc/core/utils/optix/as.h>

BTRC_CORE_BEGIN

struct InstanceInfo
{
    int32_t   geometry_id = 0;
    int32_t   material_id = 0;
    int32_t   light_id = 0;
    Transform transform;
};

CUJ_PROXY_CLASS(
    CInstanceInfo,
    InstanceInfo,
    geometry_id,
    material_id,
    light_id,
    transform);

class Scene
{
public:

    struct Instance
    {
        RC<const Geometry>  geometry;
        RC<const Material>  material;
        RC<const AreaLight> light;
        Transform           transform;
    };

    Scene() = default;

    Scene(const Scene &other) noexcept = delete;

    Scene &operator=(const Scene &other) noexcept = delete;

    Scene(Scene &&other) noexcept = default;

    Scene &operator=(Scene &&other) noexcept = default;

    void add_instance(const Instance &inst);

    void set_envir_light(RC<const EnvirLight> env);

    void set_camera(RC<const Camera> camera);

    void preprocess(optix::Context &optix_ctx);

    OptixTraversableHandle get_tlas() const;

    const GeometryInfo *get_device_geometry_info() const;

    const InstanceInfo *get_device_instance_info() const;

    const int32_t *get_device_instance_to_material() const;

    const LightSampler *get_light_sampler() const;

    int get_material_count() const;

    const Material *get_material(int id) const;

    const Camera *get_camera() const;

private:

    std::vector<Instance>  instances_;
    RC<const EnvirLight>   env_light_;
    RC<const Camera>       camera_;

    optix::InstanceAS               tlas_;
    std::vector<RC<const Material>> materials_;
    RC<const LightSampler>          light_sampler_;
    CUDABuffer<InstanceInfo>        instance_info_;
    CUDABuffer<GeometryInfo>        geometry_info_;
    CUDABuffer<int32_t>             instance_to_material_;
};

BTRC_CORE_END
