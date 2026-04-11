#include "config/parameter.h"

bool operator==(const Parameter& a, const Parameter& b) {
  return a.name == b.name && a.curveIndex == b.curveIndex && a.range == b.range;
}
