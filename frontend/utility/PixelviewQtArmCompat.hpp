#pragma once

// Qt 6.10.3's qyieldcpu.h uses __yield without including its declaration.
// Xcode 26 diagnoses that as an implicit builtin declaration under -Werror.
// Keep the real ACLE declaration visible; do not suppress the diagnostic.
#if defined(__APPLE__) && defined(__aarch64__)
#include <arm_acle.h>
#endif
