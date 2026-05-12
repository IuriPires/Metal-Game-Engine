#pragma once

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
