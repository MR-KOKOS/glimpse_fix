// Split/Second PC fix - boost (X held) and full power bar (LS+RS), player 1 only
//
// Both work on game data reached from the in-game resources global, no code is touched:
//   [0xD66B28] cInGameResources  +0xE4 cGameLogic (vtable 0xBA9A74): +0x18 player count, +0x1C players
//   player 0 cPlayerLogic (0xBA63D4): +0xE8 cPowerBar (0xBA8644) +0x2C output (0xBA8680), +8 power 0..100
//                                     +0xAC cVehicleState (0xBA7378) +0x40 vehicle list, +4 + i*4 vehicle i
//   vehicle +0x584 chassis hkpRigidBody (0xBE9C90): +0x100 forward axis, +0x110 position, +0x1A0 velocity
// The power bar update (0x43D0F0) adds its gain to the output value and clamps it to 100, so setting the
// value is enough. The boost adds speed along the chassis' forward axis while the car is moving and
// the game is not paused (the chassis moved since the last frame).
#include "common.h"
#include <math.h>

static void* const* const kInGameResources = (void* const*)0x00D66B28;

static bool IsA(void* p, DWORD vtable)
{
    return p && !IsBadReadPtr(p, 4) && *(DWORD*)p == vtable;
}

static BYTE* Field(void* p, DWORD offset)
{
    BYTE* q = p ? *(BYTE**)((BYTE*)p + offset) : NULL;
    return q && !IsBadReadPtr(q, 0x200) ? q : NULL;
}

static BYTE* PlayerLogic()
{
    BYTE* res = (BYTE*)*kInGameResources;
    if (!res || IsBadReadPtr(res, 0x100)) return NULL;
    BYTE* logic = Field(res, 0xE4);
    if (!IsA(logic, 0x00BA9A74) || *(int*)(logic + 0x18) < 1) return NULL;
    BYTE* players = Field(logic, 0x1C);
    BYTE* p0 = players ? Field(players, 0) : NULL;
    return IsA(p0, 0x00BA63D4) ? p0 : NULL;
}

static float* PowerValue(BYTE* player)
{
    BYTE* bar = Field(player, 0xE8);
    if (!IsA(bar, 0x00BA8644)) return NULL;
    BYTE* out = Field(bar, 0x2C);
    return IsA(out, 0x00BA8680) ? (float*)(out + 8) : NULL;
}

static BYTE* Chassis(BYTE* player)
{
    BYTE* state = Field(player, 0xAC);
    if (!IsA(state, 0x00BA7378)) return NULL;
    BYTE* vehicles = Field(state, 0x40);
    BYTE* vehicle = vehicles ? Field(vehicles, 4) : NULL;
    BYTE* body = vehicle && !IsBadReadPtr(vehicle, 0x600) ? Field(vehicle, 0x584) : NULL;
    return IsA(body, 0x00BE9C90) && !IsBadWritePtr(body + 0x1A0, 12) ? body : NULL;
}

static WORD PadButtons()
{
    WORD buttons = 0;
    XINPUT_STATE st;
    // polling empty slots is slow, so only the ones the pad wrapper has seen connected
    for (DWORD i = 0; i < XUSER_MAX_COUNT && pXInputGetState; i++)
        if (PadSlotConnected(i) && pXInputGetState(i, &st) == ERROR_SUCCESS) buttons |= st.Gamepad.wButtons;
    return buttons;
}

static float s_boostBlend;  // 0..1, eases the motion blur in and out

float BoostBlurScale()
{
    return 1.0f + (g_cfg.boostMotionBlur - 1.0f) * s_boostBlend;
}

void BoostTick()
{
    static DWORD lastTick;
    static WORD lastButtons;
    static float lastPos[3];
    DWORD now = GetTickCount();
    float dt = lastTick ? (now - lastTick) / 1000.0f : 0.0f;
    lastTick = now;
    if (dt > 0.1f) dt = 0.1f;

    WORD buttons = PadButtons();
    WORD pressed = buttons & ~lastButtons;
    lastButtons = buttons;
    bool boosting = false;

    __try {
        BYTE* player = PlayerLogic();
        if (player) {
            const WORD fill = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
            if (g_cfg.powerFill && (buttons & fill) == fill && (pressed & fill)) {
                if (float* power = PowerValue(player)) {
                    *power = 100.0f;
                    Log("power bar filled");
                }
            }
            BYTE* body = g_cfg.boost ? Chassis(player) : NULL;
            if (body) {
                float* fwd = (float*)(body + 0x100);
                float* pos = (float*)(body + 0x110);
                float* vel = (float*)(body + 0x1A0);
                bool moved = pos[0] != lastPos[0] || pos[1] != lastPos[1] || pos[2] != lastPos[2];
                lastPos[0] = pos[0];
                lastPos[1] = pos[1];
                lastPos[2] = pos[2];
                float speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
                float along = vel[0] * fwd[0] + vel[1] * fwd[1] + vel[2] * fwd[2];
                if ((buttons & XINPUT_GAMEPAD_X) && moved && along > 5.0f && speed < g_cfg.boostMaxSpeed) {
                    float dv = g_cfg.boostAcceleration * dt;
                    if (speed + dv > g_cfg.boostMaxSpeed) dv = g_cfg.boostMaxSpeed - speed;
                    vel[0] += fwd[0] * dv;
                    vel[1] += fwd[1] * dv;
                    vel[2] += fwd[2] * dv;
                    boosting = true;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        boosting = false;
    }

    // blur reaches full strength in 0.25 s and fades out over 0.5 s
    if (boosting) s_boostBlend = s_boostBlend + dt * 4.0f > 1.0f ? 1.0f : s_boostBlend + dt * 4.0f;
    else s_boostBlend = s_boostBlend - dt * 2.0f < 0.0f ? 0.0f : s_boostBlend - dt * 2.0f;
}
