#include <unity.h>

void setUp() {}
void tearDown() {}

void runEpd7in5V2Tests();
void runEpdDisplayTests();
void runFrameDiffTests();
void runIt8951Tests();

int main() {
  UNITY_BEGIN();
  runEpd7in5V2Tests();
  runEpdDisplayTests();
  runFrameDiffTests();
  runIt8951Tests();
  return UNITY_END();
}
