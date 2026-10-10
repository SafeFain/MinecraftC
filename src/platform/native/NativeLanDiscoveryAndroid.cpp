#include "core/LanDiscovery.h"
#if defined(__ANDROID__)
#include "platform/native/AndroidLanDiscovery.h"
#include "platform/native/LanDiscoveryValidation.h"
#include <jni.h>
#include <array>

namespace Platform {
namespace {
JavaVM* vm = nullptr;
jclass bridge = nullptr;
struct Environment {
    JNIEnv* env = nullptr;
    bool attached = false;
    Environment() {
        if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
            if (!attached) env = nullptr;
        }
    }
    ~Environment() { if (attached) vm->DetachCurrentThread(); }
};
bool exception(JNIEnv* env) { if (!env || !env->ExceptionCheck()) return false; env->ExceptionClear(); return true; }
jstring javaString(JNIEnv* env, const std::string& value) {
    const auto type = env->FindClass("java/lang/String");
    const auto constructor = env->GetMethodID(type, "<init>", "([BLjava/lang/String;)V");
    const auto bytes = env->NewByteArray(static_cast<jsize>(value.size()));
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(value.size()), reinterpret_cast<const jbyte*>(value.data()));
    const auto charset = env->NewStringUTF("UTF-8");
    const auto result = static_cast<jstring>(env->NewObject(type, constructor, bytes, charset));
    env->DeleteLocalRef(bytes); env->DeleteLocalRef(charset); env->DeleteLocalRef(type); return result;
}
std::string nativeString(JNIEnv* env, jstring value) {
    if (!value) return {};
    const auto type = env->FindClass("java/lang/String");
    const auto method = env->GetMethodID(type, "getBytes", "(Ljava/lang/String;)[B");
    const auto charset = env->NewStringUTF("UTF-8");
    const auto bytes = static_cast<jbyteArray>(env->CallObjectMethod(value, method, charset));
    std::string result;
    if (bytes) {
        const auto size = env->GetArrayLength(bytes);
        if (size <= 512) { result.resize(static_cast<size_t>(size)); env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(result.data())); }
        env->DeleteLocalRef(bytes);
    }
    env->DeleteLocalRef(charset); env->DeleteLocalRef(type); return result;
}
}
void initializeAndroidLanDiscovery(void* environment, void* activity) {
    auto* env = static_cast<JNIEnv*>(environment); const auto object = static_cast<jobject>(activity);
    if (!env || !object) return;
    if (bridge) { env->DeleteLocalRef(object); return; }
    env->GetJavaVM(&vm);
    const auto activityClass = env->GetObjectClass(object);
    const auto loaderMethod = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    const auto loader = env->CallObjectMethod(object, loaderMethod);
    const auto loaderClass = env->FindClass("java/lang/ClassLoader");
    const auto loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    const auto name = env->NewStringUTF("org.minecraftc.LanDiscoveryBridge");
    const auto localClass = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name));
    if (!exception(env) && localClass) {
        bridge = static_cast<jclass>(env->NewGlobalRef(localClass));
        const auto initialize = env->GetStaticMethodID(bridge, "initialize", "(Landroid/content/Context;)V");
        if (initialize) env->CallStaticVoidMethod(bridge, initialize, object);
        if (exception(env)) { env->DeleteGlobalRef(bridge); bridge = nullptr; }
    }
    if (localClass) env->DeleteLocalRef(localClass);
    env->DeleteLocalRef(name); env->DeleteLocalRef(loaderClass); env->DeleteLocalRef(loader); env->DeleteLocalRef(activityClass); env->DeleteLocalRef(object);
}
struct LanDiscovery::Impl {
    std::vector<LanDiscoveredRoom> rooms;
    std::string error;
    double lastPoll = -100;
    bool browsing = false, advertising = false;
    bool call(const char* name) {
        Environment environment; auto* env = environment.env;
        if (!env || !bridge) { error = "Android NSD unavailable"; return false; }
        const auto method = env->GetStaticMethodID(bridge, name, "()Z");
        const bool result = method && env->CallStaticBooleanMethod(bridge, method);
        if (exception(env)) { error = "Android NSD operation failed"; return false; }
        return result;
    }
    void stop(const char* name) {
        Environment environment; auto* env = environment.env;
        if (!env || !bridge) return;
        const auto method = env->GetStaticMethodID(bridge, name, "()V");
        if (method) env->CallStaticVoidMethod(bridge, method);
        if (exception(env)) error = "Android NSD shutdown failed";
    }
};
LanDiscovery::LanDiscovery() : m_impl(std::make_unique<Impl>()) {}
LanDiscovery::~LanDiscovery() { stopBrowsing(); stopAdvertising(); }
bool LanDiscovery::browse() { m_impl->browsing = m_impl->call("browse"); m_impl->lastPoll = -100; return m_impl->browsing; }
void LanDiscovery::stopBrowsing() { if (m_impl->browsing) m_impl->stop("stopBrowsing"); m_impl->browsing = false; m_impl->rooms.clear(); }
bool LanDiscovery::advertise(const LanAdvertisement& room) {
    if (!DiscoveryValidation::valid(room)) { m_impl->error = "Invalid LAN advertisement"; return false; }
    Environment environment; auto* env = environment.env;
    if (!env || !bridge) { m_impl->error = "Android NSD unavailable"; return false; }
    const std::array<std::string, 10> fields{room.instance, room.name, room.version, std::to_string(room.port),
        std::to_string(room.protocol), std::to_string(room.generation), std::to_string(room.players), std::to_string(room.capacity), room.pvp ? "1" : "0", room.contentSignature};
    const auto stringClass = env->FindClass("java/lang/String");
    const auto array = env->NewObjectArray(fields.size(), stringClass, nullptr);
    for (size_t i = 0; i < fields.size(); ++i) { const auto value = javaString(env, fields[i]); env->SetObjectArrayElement(array, static_cast<jsize>(i), value); env->DeleteLocalRef(value); }
    const auto method = env->GetStaticMethodID(bridge, "advertise", "([Ljava/lang/String;)Z");
    const bool result = method && env->CallStaticBooleanMethod(bridge, method, array);
    env->DeleteLocalRef(array); env->DeleteLocalRef(stringClass);
    if (exception(env)) { m_impl->error = "Android NSD advertisement failed"; return false; }
    m_impl->advertising = result; return result;
}
void LanDiscovery::stopAdvertising() { if (m_impl->advertising) m_impl->stop("stopAdvertising"); m_impl->advertising = false; }
void LanDiscovery::poll(double now) {
    if ((!m_impl->browsing && !m_impl->advertising) || now - m_impl->lastPoll < .1) return;
    m_impl->lastPoll = now;
    Environment environment; auto* env = environment.env;
    if (!env || !bridge) return;
    m_impl->rooms.clear();
    const auto errorMethod = env->GetStaticMethodID(bridge, "error", "()Ljava/lang/String;");
    const auto error = static_cast<jstring>(env->CallStaticObjectMethod(bridge, errorMethod));
    m_impl->error = nativeString(env, error); if (error) env->DeleteLocalRef(error);
    if (m_impl->browsing) {
        const auto method = env->GetStaticMethodID(bridge, "rooms", "()[[Ljava/lang/String;");
        const auto rows = static_cast<jobjectArray>(env->CallStaticObjectMethod(bridge, method));
        if (rows) {
            const auto count = std::min<jsize>(64, env->GetArrayLength(rows));
            for (jsize i = 0; i < count; ++i) {
                const auto row = static_cast<jobjectArray>(env->GetObjectArrayElement(rows, i));
                if (row && env->GetArrayLength(row) == 11) {
                    std::array<std::string, 11> fields;
                    for (jsize j = 0; j < 11; ++j) { const auto value = static_cast<jstring>(env->GetObjectArrayElement(row, j)); fields[j] = nativeString(env, value); if (value) env->DeleteLocalRef(value); }
                    LanDiscoveredRoom room; room.instance = fields[0]; room.name = fields[1]; room.version = fields[2]; room.address = fields[3];
                    room.port = static_cast<uint16_t>(DiscoveryValidation::number(fields[4], 65535)); room.protocol = static_cast<uint16_t>(DiscoveryValidation::number(fields[5], 65535));
                    room.generation = DiscoveryValidation::number(fields[6], UINT32_MAX); room.players = static_cast<uint8_t>(DiscoveryValidation::number(fields[7], 8));
                    room.capacity = static_cast<uint8_t>(DiscoveryValidation::number(fields[8], 8)); room.pvp = fields[9] == "1"; room.contentSignature = fields[10];
                    if (DiscoveryValidation::valid(room)) m_impl->rooms.push_back(std::move(room));
                }
                if (row) env->DeleteLocalRef(row);
            }
            env->DeleteLocalRef(rows);
        }
    }
    if (exception(env)) m_impl->error = "Android NSD result failed";
}
const std::vector<LanDiscoveredRoom>& LanDiscovery::rooms() const { return m_impl->rooms; }
const std::string& LanDiscovery::error() const { return m_impl->error; }
}
#endif
