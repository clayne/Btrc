#include <btrc/core/utils/cmath/ctransform.h>

BTRC_CORE_BEGIN

CTransform::CTransform(
    f32               _scale,
    const CQuaterion &_rotate,
    const CVec3f     &_translate)
{
    scale = _scale;
    rotate = _rotate;
    translate = _translate;
}

CVec3f CTransform::apply_to_point(const CVec3f &p) const
{
    return rotate.apply_to_vector(scale * p) + translate;
}

CVec3f CTransform::apply_to_vector(const CVec3f &v) const
{
    return rotate.apply_to_vector(scale * v);
}

BTRC_CORE_END
