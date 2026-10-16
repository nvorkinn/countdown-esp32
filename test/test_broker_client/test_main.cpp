#include <unity.h>

void setUp() {}
void tearDown() {}

void runTokenTests();
void runRetryTests();
void runDisplayRegistrarTests();
void runFrameClientTests();
void runLogBufferTests();
void runLineProtocolTests();
void runResponseBodyTests();

int main() {
  UNITY_BEGIN();
  runTokenTests();
  runRetryTests();
  runDisplayRegistrarTests();
  runFrameClientTests();
  runLogBufferTests();
  runLineProtocolTests();
  runResponseBodyTests();
  return UNITY_END();
}
