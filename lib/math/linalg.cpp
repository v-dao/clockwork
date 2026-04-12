#include "cw/camera_basis.hpp"
#include "cw/quat.hpp"
#include "cw/rot_align.hpp"

#include <algorithm>
#include <cmath>

namespace cw::math {

void quat_normalize(Quat& q) {
  const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (n < 1e-24) {
    q = Quat{};
    return;
  }
  const double inv = 1.0 / n;
  q.w *= inv;
  q.x *= inv;
  q.y *= inv;
  q.z *= inv;
  if (q.w < 0.0) {
    q.w = -q.w;
    q.x = -q.x;
    q.y = -q.y;
    q.z = -q.z;
  }
}

void mat3_col_from_quat(const Quat& q, double m[9]) {
  const double xx = q.x * q.x;
  const double yy = q.y * q.y;
  const double zz = q.z * q.z;
  const double xy = q.x * q.y;
  const double xz = q.x * q.z;
  const double yz = q.y * q.z;
  const double wx = q.w * q.x;
  const double wy = q.w * q.y;
  const double wz = q.w * q.z;
  m[0] = 1.0 - 2.0 * (yy + zz);
  m[1] = 2.0 * (xy + wz);
  m[2] = 2.0 * (xz - wy);
  m[3] = 2.0 * (xy - wz);
  m[4] = 1.0 - 2.0 * (xx + zz);
  m[5] = 2.0 * (yz + wx);
  m[6] = 2.0 * (xz + wy);
  m[7] = 2.0 * (yz - wx);
  m[8] = 1.0 - 2.0 * (xx + yy);
}

void glu_lookat_camera_side_world(double ex, double ey, double ez, double out_s[3]) {
  double fx = -ex;
  double fy = -ey;
  double fz = -ez;
  const double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (fl < 1e-20) {
    out_s[0] = 1.;
    out_s[1] = 0.;
    out_s[2] = 0.;
    return;
  }
  fx /= fl;
  fy /= fl;
  fz /= fl;
  const double upx = 0.;
  const double upy = 1.;
  const double upz = 0.;
  double sx = fy * upz - fz * upy;
  double sy = fz * upx - fx * upz;
  double sz = fx * upy - fy * upx;
  const double sl = std::sqrt(sx * sx + sy * sy + sz * sz);
  if (sl < 1e-20) {
    out_s[0] = 1.;
    out_s[1] = 0.;
    out_s[2] = 0.;
    return;
  }
  out_s[0] = sx / sl;
  out_s[1] = sy / sl;
  out_s[2] = sz / sl;
}

void glu_lookat_camera_up_world(double ex, double ey, double ez, double out_u[3]) {
  double fx = -ex;
  double fy = -ey;
  double fz = -ez;
  const double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (fl < 1e-20) {
    out_u[0] = 0.;
    out_u[1] = 1.;
    out_u[2] = 0.;
    return;
  }
  fx /= fl;
  fy /= fl;
  fz /= fl;
  const double upx = 0.;
  const double upy = 1.;
  const double upz = 0.;
  double sx = fy * upz - fz * upy;
  double sy = fz * upx - fx * upz;
  double sz = fx * upy - fy * upx;
  const double sl = std::sqrt(sx * sx + sy * sy + sz * sz);
  if (sl < 1e-20) {
    out_u[0] = 0.;
    out_u[1] = 1.;
    out_u[2] = 0.;
    return;
  }
  sx /= sl;
  sy /= sl;
  sz /= sl;
  double ux = sy * fz - sz * fy;
  double uy = sz * fx - sx * fz;
  double uz = sx * fy - sy * fx;
  const double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
  if (ul < 1e-20) {
    out_u[0] = 0.;
    out_u[1] = 1.;
    out_u[2] = 0.;
    return;
  }
  out_u[0] = ux / ul;
  out_u[1] = uy / ul;
  out_u[2] = uz / ul;
}

void quat_from_axis_angle_unit(const double axis[3], double angle, Quat& q) {
  const double ha = 0.5 * angle;
  const double c = std::cos(ha);
  const double s = std::sin(ha);
  q.w = c;
  q.x = axis[0] * s;
  q.y = axis[1] * s;
  q.z = axis[2] * s;
  quat_normalize(q);
}

void quat_from_two_unit_vectors(const double u[3], const double v[3], Quat& q) {
  const double dotp = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
  const double cx = u[1] * v[2] - u[2] * v[1];
  const double cy = u[2] * v[0] - u[0] * v[2];
  const double cz = u[0] * v[1] - u[1] * v[0];
  q.w = 1.0 + dotp;
  q.x = cx;
  q.y = cy;
  q.z = cz;
  if (q.w < 1e-8 && (cx * cx + cy * cy + cz * cz) < 1e-12) {
    const double ax = (std::fabs(u[0]) < 0.9) ? 1.0 : 0.0;
    const double ay = (std::fabs(u[0]) < 0.9) ? 0.0 : 1.0;
    const double az = 0.0;
    double tx = u[1] * az - u[2] * ay;
    double ty = u[2] * ax - u[0] * az;
    double tz = u[0] * ay - u[1] * ax;
    const double tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (tlen > 1e-12) {
      q.w = 0.;
      q.x = tx / tlen;
      q.y = ty / tlen;
      q.z = tz / tlen;
    } else {
      q = Quat{};
    }
  } else {
    quat_normalize(q);
  }
}

void quat_to_mat4_col(const Quat& q, double out_R[16]) {
  double m[9]{};
  mat3_col_from_quat(q, m);
  out_R[0] = m[0];
  out_R[1] = m[1];
  out_R[2] = m[2];
  out_R[3] = 0.;
  out_R[4] = m[3];
  out_R[5] = m[4];
  out_R[6] = m[5];
  out_R[7] = 0.;
  out_R[8] = m[6];
  out_R[9] = m[7];
  out_R[10] = m[8];
  out_R[11] = 0.;
  out_R[12] = out_R[13] = out_R[14] = 0.;
  out_R[15] = 1.;
}

void rot_align_unit_vectors_to_mat4_col(const double u[3], const double v[3], double out_R[16]) {
  double cx = u[1] * v[2] - u[2] * v[1];
  double cy = u[2] * v[0] - u[0] * v[2];
  double cz = u[0] * v[1] - u[1] * v[0];
  double sint = std::sqrt(cx * cx + cy * cy + cz * cz);
  double c = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
  c = std::max(-1.0, std::min(1.0, c));
  double kx = cx;
  double ky = cy;
  double kz = cz;
  if (sint > 1e-12) {
    kx /= sint;
    ky /= sint;
    kz /= sint;
  } else if (c > 0.0) {
    for (int i = 0; i < 16; ++i) {
      out_R[i] = 0.;
    }
    out_R[0] = out_R[5] = out_R[10] = out_R[15] = 1.;
    return;
  } else {
    const double tx = (std::fabs(u[0]) < 0.9) ? 1.0 : 0.0;
    const double ty = (std::fabs(u[0]) < 0.9) ? 0.0 : 1.0;
    const double tz = 0.0;
    kx = u[1] * tz - u[2] * ty;
    ky = u[2] * tx - u[0] * tz;
    kz = u[0] * ty - u[1] * tx;
    sint = std::sqrt(kx * kx + ky * ky + kz * kz);
    if (sint < 1e-12) {
      for (int i = 0; i < 16; ++i) {
        out_R[i] = 0.;
      }
      out_R[0] = out_R[5] = out_R[10] = out_R[15] = 1.;
      return;
    }
    kx /= sint;
    ky /= sint;
    kz /= sint;
    sint = 0.;
    c = -1.;
  }
  const double invC = 1.0 - c;
  out_R[0] = c + kx * kx * invC;
  out_R[1] = ky * kx * invC + kz * sint;
  out_R[2] = kz * kx * invC - ky * sint;
  out_R[3] = 0.;
  out_R[4] = kx * ky * invC - kz * sint;
  out_R[5] = c + ky * ky * invC;
  out_R[6] = kz * ky * invC + kx * sint;
  out_R[7] = 0.;
  out_R[8] = kx * kz * invC + ky * sint;
  out_R[9] = ky * kz * invC - kx * sint;
  out_R[10] = c + kz * kz * invC;
  out_R[11] = 0.;
  out_R[12] = out_R[13] = out_R[14] = 0.;
  out_R[15] = 1.;
}

}  // namespace cw::math
