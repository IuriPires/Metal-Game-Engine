#pragma once

#include "mge/math/aabb.h"
#include "mge/math/mat.h"
#include "mge/math/vec.h"

namespace mge::scene {

// Right-handed perspective camera. Produces a view matrix that takes world
// space into camera space (-Z forward) and a projection matrix that maps to
// Metal's NDC z range [0, 1]. Combined view-projection is what the vertex
// shader consumes for clip-space position.
class Camera {
public:
    Camera() = default;

    void set_perspective(float fovy_radians, float aspect, float znear, float zfar) noexcept {
        fovy_   = fovy_radians;
        aspect_ = aspect;
        znear_  = znear;
        zfar_   = zfar;
        proj_dirty_ = true;
    }

    void set_aspect(float aspect) noexcept {
        if (aspect_ != aspect) {
            aspect_     = aspect;
            proj_dirty_ = true;
        }
    }

    void look_at(math::Vec3 eye, math::Vec3 center, math::Vec3 up) noexcept {
        eye_         = eye;
        center_      = center;
        up_          = up;
        view_dirty_  = true;
    }

    [[nodiscard]] math::Vec3 eye()    const noexcept { return eye_; }
    [[nodiscard]] math::Vec3 center() const noexcept { return center_; }
    [[nodiscard]] math::Vec3 forward() const noexcept {
        return math::normalize(math::Vec3{center_.x - eye_.x, center_.y - eye_.y,
                                          center_.z - eye_.z});
    }

    [[nodiscard]] float fovy()    const noexcept { return fovy_; }
    [[nodiscard]] float aspect()  const noexcept { return aspect_; }
    [[nodiscard]] float znear()   const noexcept { return znear_; }
    [[nodiscard]] float zfar()    const noexcept { return zfar_; }

    [[nodiscard]] const math::Mat4& view() const noexcept {
        if (view_dirty_) {
            view_       = math::look_at_rh(eye_, center_, up_);
            view_dirty_ = false;
        }
        return view_;
    }

    [[nodiscard]] const math::Mat4& projection() const noexcept {
        if (proj_dirty_) {
            proj_       = math::perspective_rh_zo(fovy_, aspect_, znear_, zfar_);
            proj_dirty_ = false;
        }
        return proj_;
    }

    [[nodiscard]] math::Mat4 view_projection() const noexcept {
        return projection() * view();
    }

    // M27 — Build a world-space ray from a point in normalized device
    // coordinates. `ndc_x` / `ndc_y` are in [-1, 1] (Metal NDC, origin at
    // center, +Y up). Origin is the camera eye; direction is unit length.
    //
    // Inverting the view-projection lets us unproject the near and far
    // points of the NDC ray and take the difference — works for any
    // perspective projection without needing to know the fovy/aspect.
    [[nodiscard]] math::Ray ray_from_ndc(float ndc_x, float ndc_y) const noexcept {
        const math::Mat4 vp_inv = math::inverse(view_projection());
        // Metal NDC z range is [0, 1]; near plane is z=0, far is z=1.
        const math::Vec4 p_near_h = vp_inv * math::Vec4{ndc_x, ndc_y, 0.0f, 1.0f};
        const math::Vec4 p_far_h  = vp_inv * math::Vec4{ndc_x, ndc_y, 1.0f, 1.0f};
        const math::Vec3 p_near{p_near_h.x / p_near_h.w,
                                 p_near_h.y / p_near_h.w,
                                 p_near_h.z / p_near_h.w};
        const math::Vec3 p_far {p_far_h.x  / p_far_h.w,
                                 p_far_h.y  / p_far_h.w,
                                 p_far_h.z  / p_far_h.w};
        math::Ray r;
        r.origin = eye_;
        r.dir    = math::normalize(math::Vec3{p_far.x - p_near.x,
                                                p_far.y - p_near.y,
                                                p_far.z - p_near.z});
        return r;
    }

private:
    math::Vec3 eye_{0, 0, 3};
    math::Vec3 center_{0, 0, 0};
    math::Vec3 up_{0, 1, 0};

    float fovy_   = math::radians(60.0f);
    float aspect_ = 16.0f / 9.0f;
    float znear_  = 0.1f;
    float zfar_   = 100.0f;

    mutable math::Mat4 view_{};
    mutable math::Mat4 proj_{};
    mutable bool       view_dirty_ = true;
    mutable bool       proj_dirty_ = true;
};

}  // namespace mge::scene
