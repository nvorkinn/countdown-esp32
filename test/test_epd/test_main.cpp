#include <unity.h>

void setUp() {}
void tearDown() {}

void runEpd7in5V2Tests();
void runEpdDisplayTests();
void runIt8951Tests();

int main() {
  UNITY_BEGIN();
  runEpd7in5V2Tests();
  runEpdDisplayTests();
  runIt8951Tests();
  return UNITY_END();
}
