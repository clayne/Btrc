#pragma once

#include <btrc/core/spectrum/spectrum.h>

BTRC_CORE_BEGIN

enum class TransportMode
{
    Radiance, Importance
};

class Shader
{
public:

    CUJ_CLASS_BEGIN(SampleResult)
        CUJ_MEMBER_VARIABLE(CSpectrum, bsdf)
        CUJ_MEMBER_VARIABLE(CVec3f,    dir)
        CUJ_MEMBER_VARIABLE(f32,       pdf)
    CUJ_CLASS_END

    virtual ~Shader() = default;

    virtual SampleResult sample(
        ref<CVec3f>   wo,
        ref<CVec3f>   sam,
        TransportMode mode) const = 0;

    virtual CSpectrum eval(
        ref<CVec3f>   wi,
        ref<CVec3f>   wo,
        TransportMode mode) const = 0;

    virtual f32 pdf(
        ref<CVec3f>   wi,
        ref<CVec3f>   wo,
        TransportMode mode) const = 0;

    virtual CSpectrum albedo() const = 0;

    virtual CVec3f normal() const = 0;

    virtual boolean is_delta() const = 0;
};

BTRC_CORE_END
