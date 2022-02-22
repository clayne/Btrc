#pragma once

#include <btrc/core/light.h>
#include <btrc/factory/context.h>

BTRC_BUILTIN_BEGIN

class GradientSky : public EnvirLight
{
    PropertySlot<Spectrum> lower_ = new_property(Spectrum::from_rgb(0, 0, 0));
    PropertySlot<Spectrum> upper_ = new_property(Spectrum::from_rgb(1, 1, 1));
    PropertySlot<Vec3f> up_ = new_property(Vec3f(0, 0, 1));

public:

    using EnvirLight::EnvirLight;

    void set_lower(const Spectrum &lower);

    void set_upper(const Spectrum &upper);

    void set_up(const Vec3f &up);

    CSpectrum eval_le_inline(CompileContext &cc, ref<CVec3f> to_light) const override;

    SampleLiResult sample_li_inline(CompileContext &cc, ref<CVec3f> sam) const override;

    f32 pdf_li_inline(CompileContext &cc, ref<CVec3f> to_light) const override;
};

class GradientSkyCreator : public factory::Creator<Light>
{
public:

    std::string get_name() const override { return "gradient_sky"; }

    RC<Light> create(RC<const factory::Node> node, factory::Context &context) override;
};

BTRC_BUILTIN_END
