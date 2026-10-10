package org.minecraftc;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.SystemClock;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

// NSD owns multicast and permission handling. Callback data is copied under one
// lock; the native main thread receives bounded immutable snapshots.
public final class LanDiscoveryBridge {
    private static NsdManager manager;
    private static NsdManager.DiscoveryListener discovery;
    private static NsdManager.RegistrationListener registration;
    private static final LinkedHashMap<String, NsdServiceInfo> found = new LinkedHashMap<>();
    private static final LinkedHashMap<String, String[]> rooms = new LinkedHashMap<>();
    private static final LinkedHashMap<String, Long> updated = new LinkedHashMap<>();
    private static final ArrayDeque<NsdServiceInfo> pending = new ArrayDeque<>();
    private static String[] advertisement;
    private static String error = "";
    private static boolean resolving, registered, unregistering;
    private static int epoch;
    private static long lastRefresh;
    private LanDiscoveryBridge() {}
    public static synchronized void initialize(Context context) {
        manager = (NsdManager) context.getApplicationContext().getSystemService(Context.NSD_SERVICE);
    }
    public static synchronized String error() { return error; }
    private static void failure(String operation, int code) { error = operation + " (" + code + ")"; }
    public static synchronized boolean browse() {
        if (manager == null) { error = "Android NSD unavailable"; return false; }
        stopBrowsing(); error = ""; final int generation = epoch;
        NsdManager.DiscoveryListener listener = new NsdManager.DiscoveryListener() {
            public void onDiscoveryStarted(String type) {}
            public void onDiscoveryStopped(String type) {}
            public void onStartDiscoveryFailed(String type, int code) { synchronized (LanDiscoveryBridge.class) { if (generation == epoch) { failure("NSD browse", code); discovery = null; } } }
            public void onStopDiscoveryFailed(String type, int code) { synchronized (LanDiscoveryBridge.class) { failure("NSD stop browse", code); } }
            public void onServiceFound(NsdServiceInfo service) {
                synchronized (LanDiscoveryBridge.class) {
                    if (generation != epoch || !service.getServiceType().equals("_minecraftc._tcp.") || found.size() >= 64) return;
                    found.put(service.getServiceName(), service);
                    if (pending.size() < 64 && pending.stream().noneMatch(value -> value.getServiceName().equals(service.getServiceName()))) pending.add(service);
                    resolveNext();
                }
            }
            public void onServiceLost(NsdServiceInfo service) {
                synchronized (LanDiscoveryBridge.class) {
                    if (generation != epoch) return;
                    found.remove(service.getServiceName()); rooms.remove(service.getServiceName()); updated.remove(service.getServiceName());
                    pending.removeIf(value -> value.getServiceName().equals(service.getServiceName()));
                }
            }
        };
        try { manager.discoverServices("_minecraftc._tcp.", NsdManager.PROTOCOL_DNS_SD, listener); discovery = listener; return true; }
        catch (RuntimeException exception) { error = "NSD browse: " + exception.getClass().getSimpleName(); return false; }
    }
    public static synchronized void stopBrowsing() {
        ++epoch; pending.clear(); found.clear(); rooms.clear(); updated.clear();
        if (discovery != null) { try { manager.stopServiceDiscovery(discovery); } catch (RuntimeException exception) { error = "NSD stop browse failed"; } discovery = null; }
    }
    @SuppressWarnings("deprecation")
    private static void resolveNext() {
        if (resolving || pending.isEmpty() || discovery == null) return;
        final NsdServiceInfo service = pending.remove(); final int generation = epoch; resolving = true;
        try { manager.resolveService(service, new NsdManager.ResolveListener() {
            public void onResolveFailed(NsdServiceInfo ignored, int code) {
                synchronized (LanDiscoveryBridge.class) { resolving = false; if (generation == epoch) failure("NSD resolve", code); resolveNext(); }
            }
            public void onServiceResolved(NsdServiceInfo resolved) {
                synchronized (LanDiscoveryBridge.class) {
                    resolving = false;
                    if (generation == epoch && found.containsKey(service.getServiceName())) {
                        InetAddress address = resolved.getHost();
                        if (address != null) {
                            String numeric = address.getHostAddress();
                            if (address instanceof Inet6Address && numeric.contains("%")) numeric = numeric.substring(0, numeric.indexOf('%')) + "%" + ((Inet6Address) address).getScopeId();
                            Map<String, byte[]> attributes = resolved.getAttributes();
                            rooms.put(service.getServiceName(), new String[]{service.getServiceName(), field(attributes,"name"), field(attributes,"version"), numeric,
                                Integer.toString(resolved.getPort()), field(attributes,"protocol"), field(attributes,"generation"), field(attributes,"players"), field(attributes,"capacity"), field(attributes,"pvp"), field(attributes,"content")});
                            updated.put(service.getServiceName(), SystemClock.elapsedRealtime());
                        }
                    }
                    resolveNext();
                }
            }
        }); } catch (RuntimeException exception) { resolving = false; error = "NSD resolve failed"; }
    }
    private static String field(Map<String, byte[]> fields, String key) {
        byte[] value = fields.get(key); return value != null && value.length <= 128 ? new String(value, StandardCharsets.UTF_8) : "";
    }
    public static synchronized String[][] rooms() {
        long now = SystemClock.elapsedRealtime();
        if (discovery != null && now - lastRefresh >= 5000) {
            lastRefresh = now;
            if (pending.isEmpty()) for (NsdServiceInfo service : found.values()) pending.add(service);
            resolveNext();
        }
        rooms.entrySet().removeIf(entry -> now - updated.getOrDefault(entry.getKey(), 0L) > 20000);
        return rooms.values().toArray(new String[0][]);
    }
    public static synchronized boolean advertise(String[] values) {
        if (manager == null || values.length != 10) return false;
        if (Arrays.equals(advertisement, values)) return true;
        advertisement = values.clone();
        if (registration != null) { unregister(); return true; }
        return register();
    }
    private static boolean register() {
        if (advertisement == null) return true;
        NsdServiceInfo service = new NsdServiceInfo(); service.setServiceName("mc-" + advertisement[0]);
        service.setServiceType("_minecraftc._tcp."); service.setPort(Integer.parseInt(advertisement[3]));
        String[] keys = {"name", "version", "protocol", "generation", "players", "capacity", "pvp", "content"};
        int[] indices = {1,2,4,5,6,7,8,9};
        for (int i = 0; i < keys.length; ++i) service.setAttribute(keys[i], advertisement[indices[i]]);
        final String[] submitted = advertisement.clone();
        NsdManager.RegistrationListener listener = new NsdManager.RegistrationListener() {
            public void onServiceRegistered(NsdServiceInfo info) { synchronized (LanDiscoveryBridge.class) { registered = true; if (!Arrays.equals(advertisement, submitted)) unregister(); } }
            public void onRegistrationFailed(NsdServiceInfo info, int code) { synchronized (LanDiscoveryBridge.class) { failure("NSD register", code); registration = null; registered = false; advertisement = null; } }
            public void onServiceUnregistered(NsdServiceInfo info) { synchronized (LanDiscoveryBridge.class) { registration = null; registered = unregistering = false; register(); } }
            public void onUnregistrationFailed(NsdServiceInfo info, int code) { synchronized (LanDiscoveryBridge.class) { failure("NSD unregister", code); unregistering = false; } }
        };
        try { manager.registerService(service, NsdManager.PROTOCOL_DNS_SD, listener); registration = listener; return true; }
        catch (RuntimeException exception) { error = "NSD register: " + exception.getClass().getSimpleName(); advertisement = null; return false; }
    }
    private static void unregister() {
        if (registration == null || !registered || unregistering) return;
        unregistering = true;
        try { manager.unregisterService(registration); }
        catch (RuntimeException exception) { unregistering = false; error = "NSD unregister failed"; }
    }
    public static synchronized void stopAdvertising() { advertisement = null; unregister(); }
}
