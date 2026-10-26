#include <unity.h>

void setUp() {}
void tearDown() {}

void runCaptiveProbeTests();
void runPortalApiTests();
void runSetupScreenTests();

int main() {
  UNITY_BEGIN();
  runCaptiveProbeTests();
  runPortalApiTests();
  runSetupScreenTests();
  return UNITY_END();
}
