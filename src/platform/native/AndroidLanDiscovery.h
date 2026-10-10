#pragma once
namespace Platform {
// Called by the SDL Android adapter with its JNI environment and local activity
// reference. JNI and NSD stay inside the native adapter.
void initializeAndroidLanDiscovery(void* environment, void* activity);
}
