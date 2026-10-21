#include <unity.h>

void setUp() {}
void tearDown() {}

void runCaptiveProbeTests();
void runSetupScreenTests();

int main() {
  UNITY_BEGIN();
  runCaptiveProbeTests();
  runSetupScreenTests();
  return UNITY_END();
}
