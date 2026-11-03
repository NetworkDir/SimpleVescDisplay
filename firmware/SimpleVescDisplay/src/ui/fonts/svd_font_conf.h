#pragma once
// Compile only the fonts the selected board uses (faster builds, smaller firmware).
#include "../../config/board_config.h"

#if defined(UI_LARGE)
  #define SVD_FONT_12         0
  #define SVD_FONT_14         1
  #define SVD_FONT_16         1
  #define SVD_FONT_20         1
  #define SVD_FONT_24         0
  #define SVD_FONT_28         1
  #define SVD_FONT_32         1
  #define SVD_FONT_SPEED_120  0
  #define SVD_FONT_SPEED_170  1
#else
  #define SVD_FONT_12         1
  #define SVD_FONT_14         1
  #define SVD_FONT_16         1
  #define SVD_FONT_20         1
  #define SVD_FONT_24         1
  #define SVD_FONT_28         0
  #define SVD_FONT_32         0
  #define SVD_FONT_SPEED_120  1
  #define SVD_FONT_SPEED_170  0
#endif
