// SpeedAddressFinder.asi
// Purpose: locate the static module-relative offset equivalent to
//   constexpr uintptr_t g_SpeedAddress = 0x2A856CC;
// by correlating position-based km/h with floats in the game module.
//
// No HUD / ImGui / time control.
// Results are written next to this DLL as SpeedAddress.txt
// (file is truncated/cleared once when the finder starts)
//
// Build: x64 DLL/ASI, link Psapi.lib
// Load via Ultimate ASI Loader (dinput8.dll) in SnowRunner\Sources\Bin

#include <Windows.h>
#include <Psapi.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>

#pragma comment(lib, "Psapi.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
HMODULE g_GameModule = nullptr;
HMODULE g_SelfModule = nullptr;
std::string g_DllDirectory;
std::atomic<bool> g_Running{ false };

std::atomic<uintptr_t> g_ChassisBody{ 0 };
uintptr_t g_PosOffset = 0xC0;
uintptr_t g_UsedStatic = 0;
int       g_UsedChain = -1;

float g_PrevPos[3] = {};
bool  g_HasPrevPos = false;
float g_SmoothedKmh = 0.f;
bool  g_Confirmed = false;
float g_ConfirmTravel = 0.f;

// Speed-float candidates
constexpr int kMaxCands = 48;
uintptr_t g_CandOff[kMaxCands] = {};
int       g_CandHits[kMaxCands] = {};
int       g_CandCount = 0;
uintptr_t g_BestOff = 0;
int       g_BestHits = 0;
bool      g_Written = false;

// Layout / chains (same as SpeedHud runtime scanner)
constexpr uintptr_t POS_CANDIDATES[] = { 0xC0, 0xB0, 0xD0, 0xA0, 0xE0 };
struct ChainVariant { uintptr_t tsn, inter, chassis; };
constexpr ChainVariant CHAINS[] = {
    { 0x08, 0x60, 0x68 },
    { 0x08, 0x60, 0x70 },
    { 0x08, 0x58, 0x68 },
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
template<typename T>
bool SafeRead(uintptr_t addr, T& out)
{
    if (!addr || addr < 0x10000) return false;
    __try {
        out = *reinterpret_cast<T*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ClearLogFile()
{
    if (g_DllDirectory.empty()) return;
    std::ofstream f(g_DllDirectory + "\\SpeedAddress.txt", std::ios::trunc);
}

void LogLine(const std::string& line)
{
    if (g_DllDirectory.empty()) return;
    std::ofstream f(g_DllDirectory + "\\SpeedAddress.txt", std::ios::app);
    if (f.is_open())
        f << line << "\n";
}

bool IsUnitLike(float x, float y, float z)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    float len = sqrtf(x * x + y * y + z * z);
    return len > 0.85f && len < 1.15f;
}

bool IsPlausibleWorldPos(float x, float y, float z)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    float horiz = sqrtf(x * x + z * z);
    if (horiz < 5.0f) return false;
    if (fabsf(x) > 40000.f || fabsf(z) > 40000.f) return false;
    if (y < -500.f || y > 5000.f) return false;
    return true;
}

bool ReadValidTransform(uintptr_t chassis, uintptr_t posOff, float* outPos)
{
    float x = 0, y = 0, z = 0;
    if (!SafeRead(chassis + posOff, x)) return false;
    if (!SafeRead(chassis + posOff + 4, y)) return false;
    if (!SafeRead(chassis + posOff + 8, z)) return false;
    if (!IsPlausibleWorldPos(x, y, z)) return false;

    const uintptr_t axisDeltas[] = { 0x10, 0x20, 0x30, 0x40 };
    bool foundAxis = false;
    for (uintptr_t d : axisDeltas)
    {
        if (posOff < d) continue;
        float ax = 0, ay = 0, az = 0;
        if (!SafeRead(chassis + posOff - d, ax)) continue;
        if (!SafeRead(chassis + posOff - d + 4, ay)) continue;
        if (!SafeRead(chassis + posOff - d + 8, az)) continue;
        if (IsUnitLike(ax, ay, az)) { foundAxis = true; break; }
    }
    if (!foundAxis) return false;

    outPos[0] = x; outPos[1] = y; outPos[2] = z;
    return true;
}

bool TryResolve(uintptr_t staticAddr, int chainIdx, uintptr_t& outChassis, uintptr_t& outPosOff)
{
    const ChainVariant& ch = CHAINS[chainIdx];
    uintptr_t truckControl = 0;
    if (!SafeRead(staticAddr, truckControl) || truckControl < 0x100000) return false;
    if (truckControl > 0x7FFFFFFFFFFF) return false;

    uintptr_t tsn = 0;
    if (!SafeRead(truckControl + ch.tsn, tsn) || tsn < 0x100000) return false;

    uintptr_t inter = 0;
    if (!SafeRead(tsn + ch.inter, inter) || inter < 0x100000) return false;

    uintptr_t chassis = 0;
    if (!SafeRead(inter + ch.chassis, chassis) || chassis < 0x100000) return false;

    float pos[3]{};
    for (uintptr_t po : POS_CANDIDATES)
    {
        if (ReadValidTransform(chassis, po, pos))
        {
            outChassis = chassis;
            outPosOff = po;
            return true;
        }
    }
    return false;
}

uintptr_t FindChassisBody()
{
    if (!g_GameModule) return 0;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), g_GameModule, &mi, sizeof(mi)))
        return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    for (uintptr_t off = 0x2A50000; off < 0x2AA0000; off += 8)
    {
        if (off >= mi.SizeOfImage) break;
        for (int ci = 0; ci < (int)(sizeof(CHAINS) / sizeof(CHAINS[0])); ++ci)
        {
            uintptr_t chassis = 0, posOff = 0;
            if (TryResolve(base + off, ci, chassis, posOff))
            {
                g_UsedStatic = off;
                g_UsedChain = ci;
                g_PosOffset = posOff;
                return chassis;
            }
        }
    }
    return 0;
}

bool RefreshFromKnownStatic(uintptr_t& outChassis)
{
    if (!g_GameModule || g_UsedStatic == 0 || g_UsedChain < 0) return false;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), g_GameModule, &mi, sizeof(mi)))
        return false;
    uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    uintptr_t chassis = 0, posOff = 0;
    if (TryResolve(base + g_UsedStatic, g_UsedChain, chassis, posOff))
    {
        outChassis = chassis;
        g_PosOffset = posOff;
        return true;
    }
    for (int ci = 0; ci < (int)(sizeof(CHAINS) / sizeof(CHAINS[0])); ++ci)
    {
        if (ci == g_UsedChain) continue;
        if (TryResolve(base + g_UsedStatic, ci, chassis, posOff))
        {
            g_UsedChain = ci;
            outChassis = chassis;
            g_PosOffset = posOff;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Speed float search
// ---------------------------------------------------------------------------
void SeedCandidates(float targetKmh)
{
    if (!g_GameModule || targetKmh < 8.f || targetKmh > 120.f) return;

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), g_GameModule, &mi, sizeof(mi)))
        return;
    uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);

    uintptr_t regions[][2] = {
        { 0x2A00000, 0x2B00000 },
        { 0x2800000, 0x2A00000 },
        { 0x2B00000, 0x2C00000 },
    };
    float tol = 2.5f + targetKmh * 0.08f;

    for (auto& reg : regions)
    {
        uintptr_t start = reg[0];
        uintptr_t end = reg[1];
        if (start >= mi.SizeOfImage) continue;
        if (end > mi.SizeOfImage) end = mi.SizeOfImage;

        for (uintptr_t off = start; off + 4 < end; off += 4)
        {
            float v = 0.f;
            if (!SafeRead(base + off, v)) continue;
            if (!std::isfinite(v) || v < 0.f || v > 200.f) continue;
            if (fabsf(v - targetKmh) > tol) continue;

            bool exists = false;
            for (int i = 0; i < g_CandCount; ++i)
                if (g_CandOff[i] == off) { exists = true; break; }
            if (exists) continue;
            if (g_CandCount >= kMaxCands) return;
            if (off == 0) continue;

            g_CandOff[g_CandCount] = off;
            g_CandHits[g_CandCount] = 1;
            g_CandCount++;
        }
    }
}

void FilterCandidates(float targetKmh, bool stopped)
{
    if (!g_GameModule || g_CandCount <= 0) return;

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), g_GameModule, &mi, sizeof(mi)))
        return;
    uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);

    int w = 0;
    for (int i = 0; i < g_CandCount; ++i)
    {
        if (g_CandOff[i] == 0) continue;

        float v = 0.f;
        if (!SafeRead(base + g_CandOff[i], v) || !std::isfinite(v))
            continue;

        if (stopped)
        {
            if (v > 3.5f) continue;
            g_CandHits[i]++;
        }
        else
        {
            float tol = 3.0f + targetKmh * 0.10f;
            if (fabsf(v - targetKmh) > tol) continue;
            g_CandHits[i]++;
        }

        g_CandOff[w] = g_CandOff[i];
        g_CandHits[w] = g_CandHits[i];
        w++;
    }
    g_CandCount = w;

    g_BestOff = 0;
    g_BestHits = 0;
    for (int i = 0; i < g_CandCount; ++i)
    {
        if (g_CandOff[i] == 0) continue;
        if (g_CandHits[i] > g_BestHits)
        {
            g_BestHits = g_CandHits[i];
            g_BestOff = g_CandOff[i];
        }
    }

    if (g_BestOff == 0)
        g_Written = false;
}

void MaybeMarkDone()
{
    if (g_Written) return;
    if (g_BestOff == 0) return;
    if (g_BestHits < 6 || g_CandCount == 0 || g_CandCount > 8) return;
    g_Written = true;
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
DWORD WINAPI WorkerThread(LPVOID)
{
    // Wait for game to settle
    Sleep(5000);

    g_GameModule = GetModuleHandleW(nullptr);
    if (!g_GameModule)
    {
        ClearLogFile();
        LogLine("ERROR: GetModuleHandle failed");
        return 1;
    }

    ClearLogFile();  // empty SpeedAddress.txt at session start
    LogLine("SpeedAddressFinder started. Enter a map, exit garage, drive 10-90 km/h, then stop.");
    LogLine("Repeat drive/stop a few times. Watch for: DONE best=0x....");

    int emptyCycles = 0;
    int logCounter = 0;

    while (g_Running)
    {
        uintptr_t chassis = g_ChassisBody.load();

        // Refresh / find chassis
        if (g_UsedStatic != 0)
        {
            uintptr_t refreshed = 0;
            if (RefreshFromKnownStatic(refreshed))
            {
                if (refreshed != chassis)
                {
                    g_ChassisBody.store(refreshed);
                    chassis = refreshed;
                    g_Confirmed = false;
                    g_ConfirmTravel = 0.f;
                    g_HasPrevPos = false;
                    g_SmoothedKmh = 0.f;
                }
                else if (!chassis)
                {
                    g_ChassisBody.store(refreshed);
                    chassis = refreshed;
                }
                emptyCycles = 0;
            }
        }

        if (!chassis)
        {
            emptyCycles++;
            if (g_UsedStatic == 0 || emptyCycles >= 3)
            {
                uintptr_t found = FindChassisBody();
                if (found)
                {
                    g_ChassisBody.store(found);
                    chassis = found;
                    g_Confirmed = false;
                    g_ConfirmTravel = 0.f;
                    g_HasPrevPos = false;
                    emptyCycles = 0;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Chassis candidate: static=0x%llX chain=%d posOff=0x%llX",
                        (unsigned long long)g_UsedStatic, g_UsedChain,
                        (unsigned long long)g_PosOffset);
                    //LogLine(buf);
                }
            }
            Sleep(800);
            continue;
        }

        float pos[3]{};
        if (!ReadValidTransform(chassis, g_PosOffset, pos))
        {
            emptyCycles++;
            if (emptyCycles >= 3)
            {
                g_ChassisBody.store(0);
                g_Confirmed = false;
                g_HasPrevPos = false;
            }
            Sleep(500);
            continue;
        }
        emptyCycles = 0;

        // Sample interval ~120ms for speed
        const float dt = 0.12f;

        if (!g_Confirmed)
        {
            if (g_HasPrevPos)
            {
                float dx = pos[0] - g_PrevPos[0];
                float dy = pos[1] - g_PrevPos[1];
                float dz = pos[2] - g_PrevPos[2];
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d < 50.f && d > 0.001f)
                    g_ConfirmTravel += d;
                if (g_ConfirmTravel > 1.5f)
                {
                    g_Confirmed = true;
                    //LogLine("Chassis confirmed. Drive at 10-90 km/h to seed float search.");
                }
            }
            g_PrevPos[0] = pos[0];
            g_PrevPos[1] = pos[1];
            g_PrevPos[2] = pos[2];
            g_HasPrevPos = true;
            Sleep(120);
            continue;
        }

        // Speed from position delta
        float speedKmh = 0.f;
        if (g_HasPrevPos)
        {
            float dx = pos[0] - g_PrevPos[0];
            float dy = pos[1] - g_PrevPos[1];
            float dz = pos[2] - g_PrevPos[2];
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist < 80.f)
            {
                float mps = dist / dt;
                float raw = mps * 3.6f;
                if (raw < 2.5f) raw = 0.f;
                float alpha = (raw < g_SmoothedKmh) ? 0.45f : 0.25f;
                g_SmoothedKmh = g_SmoothedKmh * (1.f - alpha) + raw * alpha;
                if (g_SmoothedKmh < 1.5f) g_SmoothedKmh = 0.f;
            }
        }
        g_PrevPos[0] = pos[0];
        g_PrevPos[1] = pos[1];
        g_PrevPos[2] = pos[2];
        g_HasPrevPos = true;
        speedKmh = g_SmoothedKmh;

        // Float search
        bool stopped = (speedKmh < 1.5f);
        if (g_CandCount == 0 && !stopped && speedKmh >= 10.f && speedKmh <= 90.f)
        {
            SeedCandidates(speedKmh);
            char buf[128];
            //snprintf(buf, sizeof(buf),
            //    "Seeded %d candidates at ~%.1f km/h. Stop to filter, then drive again.",
            //    g_CandCount, speedKmh);
            //LogLine(buf);
        }
        else if (g_CandCount > 0)
        {
            FilterCandidates(speedKmh, stopped);
            MaybeMarkDone();
        }

        // Periodic log (~3s)
        if (++logCounter >= 25)
        {
            logCounter = 0;
            char buf[192];
            if (g_Written && g_BestOff != 0)
            {
                snprintf(buf, sizeof(buf),
                    "DONE best=0x%llX hits=%d speed=%.1f",
                    (unsigned long long)g_BestOff, g_BestHits, speedKmh);
                LogLine(buf);
            }
            else if (g_BestOff != 0)
            {
                //snprintf(buf, sizeof(buf),
                //    "progress speed=%.1f km/h cands=%d best=0x%llX hits=%d",
                //    speedKmh, g_CandCount,
                //    (unsigned long long)g_BestOff, g_BestHits);
                //LogLine(buf);
            }
            // best==0 のときはログしない
        }

        Sleep(120);
    }
    return 0;
}

// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        g_SelfModule = hModule;

        char path[MAX_PATH]{};
        if (GetModuleFileNameA(hModule, path, MAX_PATH))
        {
            std::string p(path);
            size_t pos = p.find_last_of("\\/");
            if (pos != std::string::npos)
                g_DllDirectory = p.substr(0, pos);
        }

        g_Running = true;
        CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_Running = false;
    }
    return TRUE;
}
