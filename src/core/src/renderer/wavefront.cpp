#include <btrc/core/renderer/wavefront/generate.h>
#include <btrc/core/renderer/wavefront/path_state.h>
#include <btrc/core/renderer/wavefront/shade.h>
#include <btrc/core/renderer/wavefront/shadow.h>
#include <btrc/core/renderer/wavefront/sort.h>
#include <btrc/core/renderer/wavefront/trace.h>
#include <btrc/core/renderer/wavefront.h>

BTRC_CORE_BEGIN

struct WavefrontPathTracer::Impl
{
    optix::Context *optix_ctx = nullptr;

    Params          params;
    RC<const Scene> scene;

    bool is_dirty = true;

    Film                 film;
    wf::PathState        path_state;
    wf::GeneratePipeline generate;
    wf::TracePipeline    trace;
    wf::SortPipeline     sort;
    wf::ShadePipeline    shade;
    wf::ShadowPipeline   shadow;
};

WavefrontPathTracer::WavefrontPathTracer(optix::Context &optix_ctx)
{
    impl_ = newBox<Impl>();
    impl_->optix_ctx = &optix_ctx;
}

WavefrontPathTracer::~WavefrontPathTracer()
{
    
}

void WavefrontPathTracer::set_params(const Params &params)
{
    impl_->params = params;
    impl_->is_dirty = true;
}

void WavefrontPathTracer::set_scene(RC<const Scene> scene)
{
    impl_->scene = scene;
    impl_->is_dirty = true;
}

Renderer::RenderResult WavefrontPathTracer::render() const
{
    if(impl_->is_dirty)
        build_pipeline();

    impl_->generate.clear();
    impl_->path_state.clear();

    auto &scene = *impl_->scene;
    auto &soa = impl_->path_state;
    auto &params = impl_->params;

    int active_state_count = 0;
    while(!impl_->generate.is_done() || active_state_count > 0)
    {
        const int new_state_count = impl_->generate.generate(
            active_state_count,
            wf::GeneratePipeline::SOAParams{
                .rng                  = soa.rng,
                .output_pixel_coord   = soa.pixel_coord,
                .output_ray_o_t0      = soa.o_t0,
                .output_ray_d_t1      = soa.d_t1,
                .output_ray_time_mask = soa.time_mask,
                .output_beta          = soa.beta,
                .output_beta_le       = soa.beta_le,
                .output_bsdf_pdf      = soa.bsdf_pdf,
                .output_depth         = soa.depth,
                .output_path_radiance = soa.path_radiance
            });
        active_state_count += new_state_count;

        impl_->trace.trace(
            scene.get_tlas(),
            active_state_count,
            wf::TracePipeline::SOAParams{
                .ray_o_t0      = soa.o_t0,
                .ray_d_t1      = soa.d_t1,
                .ray_time_mask = soa.time_mask,
                .inct_t        = soa.inct_t,
                .inct_uv_id    = soa.inct_uv_id,
                .state_index   = soa.active_state_indices
            });

        // sort

        const auto shade_counters = impl_->shade.shade(
            active_state_count,
            wf::ShadePipeline::SOAParams{
                .rng                         = soa.rng,
                .active_state_indices        = soa.active_state_indices,
                .path_radiance               = soa.path_radiance,
                .pixel_coord                 = soa.pixel_coord,
                .depth                       = soa.depth,
                .beta                        = soa.beta,
                .beta_le                     = soa.beta_le,
                .bsdf_pdf                    = soa.bsdf_pdf,
                .inct_t                      = soa.inct_t,
                .inct_uv_id                  = soa.inct_uv_id,
                .ray_o_t0                    = soa.o_t0,
                .ray_d_t1                    = soa.d_t1,
                .ray_time_mask               = soa.time_mask,
                .output_rng                  = soa.next_rng,
                .output_path_radiance        = soa.next_path_radiance,
                .output_pixel_coord          = soa.next_pixel_coord,
                .output_depth                = soa.next_depth,
                .output_beta                 = soa.next_beta,
                .output_shadow_pixel_coord   = soa.shadow_pixel_coord,
                .output_shadow_ray_o_t0      = soa.shadow_o_t0,
                .output_shadow_ray_d_t1      = soa.shadow_d_t1,
                .output_shadow_ray_time_mask = soa.shadow_time_mask,
                .output_shadow_beta_li       = soa.shadow_beta_li,
                .output_new_ray_o_t0         = soa.next_o_t0,
                .output_new_ray_d_t1         = soa.next_d_t1,
                .output_new_ray_time_mask    = soa.next_time_mask,
                .output_beta_le              = soa.next_beta_le,
                .output_bsdf_pdf             = soa.next_bsdf_pdf
            });

        active_state_count = shade_counters.active_state_counter;
        
        if(shade_counters.shadow_ray_counter)
        {
            impl_->shadow.test(
                scene.get_tlas(),
                shade_counters.shadow_ray_counter,
                wf::ShadowPipeline::SOAParams{
                    .pixel_coord   = soa.shadow_pixel_coord,
                    .ray_o_t0      = soa.shadow_o_t0,
                    .ray_d_t1      = soa.shadow_d_t1,
                    .ray_time_mask = soa.shadow_time_mask,
                    .beta_li       = soa.shadow_beta_li
                });
        }

        soa.next_iteration();
    }

    throw_on_error(cudaStreamSynchronize(nullptr));

    auto value = Image<Vec4f>(params.width, params.height);
    auto weight = Image<float>(params.width, params.height);
    auto albedo = params.albedo ? Image<Vec4f>(params.width, params.height) : Image<Vec4f>();
    auto normal = params.normal ? Image<Vec4f>(params.width, params.height) : Image<Vec4f>();

    impl_->film.get_float3_output(Film::OUTPUT_RADIANCE).to_cpu(&value(0, 0).x);
    impl_->film.get_float_output(Film::OUTPUT_WEIGHT).to_cpu(&weight(0, 0));
    if(params.albedo)
        impl_->film.get_float3_output(Film::OUTPUT_ALBEDO).to_cpu(&albedo(0, 0).x);
    if(params.normal)
        impl_->film.get_float3_output(Film::OUTPUT_NORMAL).to_cpu(&normal(0, 0).x);

    for(int i = 0; i < params.width * params.height; ++i)
    {
        float &f = weight.data()[i];
        if(f > 0)
            f = 1.0f / f;
    }

    RenderResult result;
    result.value = Image<Vec3f>(params.width, params.height);
    for(int i = 0; i < params.width * params.height; ++i)
    {
        const Vec4f &sum = value.data()[i];
        const float ratio = weight.data()[i];
        result.value.data()[i] = ratio * sum.xyz();
    }

    if(params.albedo)
    {
        result.albedo = Image<Vec3f>(params.width, params.height);
        for(int i = 0; i < params.width * params.height; ++i)
        {
            const Vec4f &sum = albedo.data()[i];
            const float ratio = weight.data()[i];
            result.albedo.data()[i] = ratio * sum.xyz();
        }
    }
    if(params.normal)
    {
        result.normal = Image<Vec3f>(params.width, params.height);
        for(int i = 0; i < params.width * params.height; ++i)
        {
            const Vec4f &sum = normal.data()[i];
            const float ratio = weight.data()[i];
            result.normal.data()[i] = 0.5f + 0.5f * ratio * sum.xyz();
        }
    }

    return result;
}

void WavefrontPathTracer::build_pipeline() const
{
    assert(impl_->is_dirty);
    BTRC_SCOPE_SUCCESS{ impl_->is_dirty = false; };

    auto &params = impl_->params;

    // film

    impl_->film = Film(params.width, params.height);
    impl_->film.add_output(Film::OUTPUT_RADIANCE, Film::Float3);
    impl_->film.add_output(Film::OUTPUT_WEIGHT, Film::Float);
    if(params.albedo)
        impl_->film.add_output(Film::OUTPUT_ALBEDO, Film::Float3);
    if(params.normal)
        impl_->film.add_output(Film::OUTPUT_NORMAL, Film::Float3);

    // pipelines

    impl_->generate = wf::GeneratePipeline(
        *impl_->scene->get_camera(),
        { params.width, params.height },
        params.spp, params.state_count);

    impl_->trace = wf::TracePipeline(
        *impl_->optix_ctx,
        impl_->scene->has_motion_blur(),
        impl_->scene->is_triangle_only(),
        2);

    impl_->sort = wf::SortPipeline();

    impl_->shade = wf::ShadePipeline(
        impl_->film, *impl_->scene, wf::ShadePipeline::ShadeParams{
            .min_depth = params.min_depth,
            .max_depth = params.max_depth,
            .rr_threshold = params.rr_threshold,
            .rr_cont_prob = params.rr_cont_prob
        });

    impl_->shadow = wf::ShadowPipeline(
        impl_->film, *impl_->optix_ctx,
        impl_->scene->has_motion_blur(),
        impl_->scene->is_triangle_only(),
        2);

    // path state

    impl_->path_state.initialize(params.state_count);
}

BTRC_CORE_END
