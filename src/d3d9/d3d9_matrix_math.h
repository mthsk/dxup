#pragma once
// Portable D3D9 row-major 4x4 matrix operations (no Windows dependencies).
namespace dxup { namespace matrix_math {
  inline bool validTransformIndex(unsigned s) {
    return s == 2 || s == 3 || (s >= 16 && s <= 23) || (s >= 256 && s <= 511);
  }
  inline void multiply(const float left[4][4], const float right[4][4], float out[4][4]) {
    float product[4][4] = {};
    for (unsigned row = 0; row < 4; ++row)
      for (unsigned col = 0; col < 4; ++col)
        for (unsigned k = 0; k < 4; ++k)
          product[row][col] += left[row][k] * right[k][col];
    for (unsigned row = 0; row < 4; ++row)
      for (unsigned col = 0; col < 4; ++col)
        out[row][col] = product[row][col];
  }
  // For D3D9 row-vector camera-space lighting: N = normal * inverseTranspose(World * View).
  // The 3x3 upper-left linear transform is sufficient; translation is ignored.
  // A degenerate matrix cannot be inverted: retain the original linear transform.
  inline bool inverseTranspose3x3(const float in[4][4], float out[4][4]) {
    const float a=in[0][0], b=in[0][1], c=in[0][2];
    const float d=in[1][0], e=in[1][1], f=in[1][2];
    const float g=in[2][0], h=in[2][1], i=in[2][2];
    const float cof[3][3] = {
      {e*i-f*h, f*g-d*i, d*h-e*g},
      {c*h-b*i, a*i-c*g, b*g-a*h},
      {b*f-c*e, c*d-a*f, a*e-b*d}
    };
    const float det = a*cof[0][0] + b*cof[0][1] + c*cof[0][2];
    // Match the normal behavior for singular transforms without infinities.
    const bool invertible = det > 1.0e-20f || det < -1.0e-20f;
    for (unsigned row=0; row<4; ++row)
      for (unsigned col=0; col<4; ++col)
        out[row][col] = row==col ? 1.0f : 0.0f;
    for (unsigned row=0; row<3; ++row)
      for (unsigned col=0; col<3; ++col)
        out[row][col] = invertible ? (cof[row][col]/det) : in[row][col];
    return invertible;
  }
  inline void identity(float out[4][4]) {
    for (unsigned row = 0; row < 4; ++row)
      for (unsigned col = 0; col < 4; ++col)
        out[row][col] = (row == col) ? 1.0f : 0.0f;
  }
}}
