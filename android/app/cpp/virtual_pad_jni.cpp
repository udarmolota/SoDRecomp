// The on-screen gamepad's state from the app's VirtualPadView (com.udarmolota.xdroid360.VirtualPad),
// handed to the input unit, which merges it with a physical pad.
#include "stdafx.h"

#include <jni.h>

#include "xdroid360_abi.h"
#include "input/input.h"
#include "xenia/gpu/d3d12/dao_frame_trace.h"

extern "C" JNIEXPORT void JNICALL Java_com_udarmolota_xdroid360_VirtualPad_nativeSetState(JNIEnv*, jclass, jint buttons,
    jfloat leftX, jfloat leftY, jfloat rightX, jfloat rightY, jfloat leftTrigger, jfloat rightTrigger)
{
    sod::input::SetVirtualPad(uint16_t(buttons), leftX, leftY, rightX, rightY, leftTrigger, rightTrigger);
}

// Frames the game presented so far (Xenia swaps), for the performance overlay.
extern "C" JNIEXPORT jlong JNICALL Java_com_udarmolota_xdroid360_GameActivity_nativeGetPresentedFrames(JNIEnv*, jclass)
{
    return jlong(xe::gpu::dao_trace::SwapCount());
}

// The interface version this library was built for (the launcher's GameActivity must match).
extern "C" JNIEXPORT jint JNICALL Java_com_udarmolota_xdroid360_GameActivity_nativeAbiVersion(JNIEnv*, jclass)
{
    return XDROID360_ABI;
}
