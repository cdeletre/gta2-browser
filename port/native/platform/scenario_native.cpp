// Test scenarios for the parts of the game a random soak does not reach (native build).
//
//   GTA2_TEST=save         save at the church: gives the player the 50,000 points the
//                          save costs, then moves them into the script's save zone
//                          (SCRCMD_DO_SAVE_GAME), as walking into the church does
//   GTA2_TEST=phone        answers a mission phone, as touching it does
//                          (Char_B4::PhoneTouched_5535B0 -> AnswerPhone_5129F0), then
//                          lets the soak (GTA2_SOAK=1) play while the mission runs
//   GTA2_TEST_PHONE=n      which of the district's mission phones (default 0)
//   GTA2_TEST_DELAY=s      seconds in the city before the save or the phone (default 3).
//                          Answering a phone while another mission is starting (the
//                          soak often walks into the phone by the church at once) loads
//                          the second mission over the first before the first has set
//                          its mission flag, which crashes; a player cannot reach two
//                          phones that fast, so the phone scenario waits for the soak's
//                          mission flag to clear (see docs/native-status.md)
//   GTA2_TEST=watch        only the log below (after loading a save, for example)
//   GTA2_TEST=damage       hurts the player through the game's Ped::TakeDamage, as a hit
//                          does, and logs the health and the HUD's half hearts (the sum
//                          Hud_Health_4::DrawHealth_5D0260 draws) around each hit
//   GTA2_TEST_DAMAGE=n     points per hit (default 10, a bullet; a punch is 2)
//   GTA2_TEST_DAMAGE_EVERY=s  seconds between hits (default 5); the first after the delay
//   GTA2_TEST_UNLOCK=1     unlocks the three areas for the current player in the menus (as the
//                          UKGAMER name cheat; gta2_dev_unlock_areas), so GTA2_MENU_KEYS
//                          can start area 2 or 3 with a fresh profile; works with any test
//   GTA2_TEST_TRACE=1      also logs each script thread's next command when it changes
//                          (checked once per loop iteration, so EXEC blocks show their end)
//
// Every step is logged ("gta2: test: ..."), with the score and the mission flag each
// second, so a run shows whether the save was paid for and whether the mission started.
// It uses only the game's own functions and data, like the web developer probe
// (port/platform/dev_hooks.cpp).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_0x40.hpp"
#include "Player.hpp"
#include "ScriptManager_C1EA8.hpp"
#include "Object_2C_Pool.hpp"
#include "miss2_0x11C.hpp"

extern "C" const s32* gta2_dev_state();
extern "C" int gta2_dev_add_cash(int cash);
extern "C" int gta2_dev_teleport(int x, int y);
extern "C" int gta2_dev_unlock_areas();
extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
EXTERN_GLOBAL(SCR_POINTER*, gBasePtr_6F8070); // miss2_0x11C.cpp: the command being run
EXTERN_GLOBAL(s32, gPrevCmdIdx_6F806C);
extern "C" void __sanitizer_set_death_callback(void (*callback)(void)) __attribute__((weak));

namespace
{
enum Test
{
    kNone,
    kSave,
    kPhone,
    kWatch,
    kDamage
};
Test gTest = kNone;
int gPhone = 0;
double gDelay = 3000;
double gPlayerSince = -1; // run clock (ms) when the player first existed
int gStep = 0;
double gNextLog = 0;
double gStepAt = 0;


int ToBlocks(const Fix16& f)
{
    return f.mValue >> 14; // Fix16: 14 fractional bits
}

Player* ThePlayer()
{
    return gGame_0x40_67E008 ? gGame_0x40_67E008->field_38_orf1 : nullptr;
}

// The half hearts the HUD draws for this health (Hud_Health_4::DrawHealth_5D0260).
int HalfHearts(int health)
{
    return health == 100 ? 10 : health / 10 + 1;
}

bool SaveZone(int* x, int* y, int* z)
{
    ScriptManager_C1EA8* sm = gScriptManager_6F8060;
    int found = 0;
    for (u16 idx = 0; sm && idx < GTA2_COUNTOF_S(sm->field_46C_base_pointers); ++idx)
    {
        SCR_CMD_HEADER* h = sm->GetBasePointer_512770(idx);
        if (h && h->field_2_type == SCRCMD_DO_SAVE_GAME)
        {
            const SCR_DO_SAVE_GAME* cmd = reinterpret_cast<const SCR_DO_SAVE_GAME*>(h);
            const int cx = ToBlocks(cmd->field_C_rect.field_0_pos.field_0_x);
            const int cy = ToBlocks(cmd->field_C_rect.field_0_pos.field_4_y);
            fprintf(stderr, "gta2: test: save zone %d at (%d, %d), half size (%.2f, %.2f), trigger %u\n", found, cx, cy,
                    cmd->field_C_rect.field_C_size.field_0_x.mValue / 16384.0,
                    cmd->field_C_rect.field_C_size.field_4_y.mValue / 16384.0, cmd->field_8_triggername);
            SCR_CMD_HEADER* trig = sm->GetBasePointer_512770(cmd->field_8_triggername);
            if (trig && (trig->field_2_type == SCRCMD_THREAD_DECLARE5 || trig->field_2_type == SCRCMD_CHAR_AREA_ANY_MEANS))
            {
                const SCR_CHAR_AREA_ANY* area = reinterpret_cast<const SCR_CHAR_AREA_ANY*>(trig);
                fprintf(stderr, "gta2: test: its trigger (type %u): (%.2f, %.2f, %.2f), size (%.2f, %.2f), touch point %d\n",
                        trig->field_2_type, area->field_10_x.mValue / 16384.0, area->field_14_y.mValue / 16384.0,
                        area->field_18_z.mValue / 16384.0, area->field_1C_w.mValue / 16384.0,
                        area->field_20_h.mValue / 16384.0, area->field_26_result);
            }
            else
            {
                fprintf(stderr, "gta2: test: its trigger is a type %u command\n", trig ? trig->field_2_type : 0);
            }
            if (found++ == 0)
            {
                *x = cx;
                *y = cy;
                *z = cmd->field_C_rect.field_0_pos.field_8_z.mValue;
            }
        }
    }
    return found > 0;
}

// The model of object id among the live objects, or -1. The pool is scanned in full:
// a test aid, run once.
int ObjectModel(s32 id)
{
    if (!gObject_2C_Pool_6F8F80)
    {
        return -1;
    }
    for (Object_2C& o : gObject_2C_Pool_6F8F80->field_8_pool)
    {
        if (o.field_14_id == id && o.field_18_model)
        {
            return o.field_18_model;
        }
    }
    return -1;
}

// What touching it needs (Char_B4's collision switch, char.cpp): one of the phone models.
bool IsPhoneModel(int model)
{
    return model == objects::blue_phone_164 || model == objects::red_phone_177 || model == objects::yellow_phone_179 ||
           model == objects::green_phone_181;
}

bool AnswerPhone()
{
    ScriptManager_C1EA8* sm = gScriptManager_6F8060;
    const int count = sm ? sm->field_184_phone_trigger_count : 0;
    fprintf(stderr, "gta2: test: %d mission phones\n", count);
    for (int i = 0; i < count; ++i)
    {
        const ThreadTrigger_C& t = sm->field_188_phone_triggers[i];
        SCR_CMD_HEADER* h = sm->GetBasePointer_512770(t.field_8_cmd_line);
        const SCR_THREAD* th = reinterpret_cast<const SCR_THREAD*>(h);
        const bool startOk = h && sm->GetBasePointer_512770(th->field_E_thread_start_cmd) != nullptr;
        fprintf(stderr, "gta2: test: phone %d: ped %d, object %d (model %d), command %u (type %u), thread start %u%s\n", i,
                t.field_0_ped_id, t.field_4_obj_f14, ObjectModel(t.field_4_obj_f14), t.field_8_cmd_line,
                h ? h->field_2_type : 0, h ? th->field_E_thread_start_cmd : 0, startOk ? "" : " (no such line)");
    }
    if (gPhone >= count)
    {
        return false;
    }
    const ThreadTrigger_C& t = sm->field_188_phone_triggers[gPhone];
    if (!IsPhoneModel(ObjectModel(t.field_4_obj_f14)) && !getenv("GTA2_TEST_ANY_PHONE"))
    {
        fprintf(stderr, "gta2: test: phone %d cannot be touched (no phone object with that id), not answered\n", gPhone);
        return false;
    }
    if (getenv("GTA2_TEST_TRACE"))
    {
        const SCR_THREAD* th = reinterpret_cast<const SCR_THREAD*>(sm->GetBasePointer_512770(t.field_8_cmd_line));
        for (int line = th->field_E_thread_start_cmd; line < th->field_E_thread_start_cmd + 40; ++line)
        {
            SCR_CMD_HEADER* h = sm->GetBasePointer_512770(static_cast<u16>(line));
            const u8* b = reinterpret_cast<const u8*>(h);
            fprintf(stderr, "gta2: trace: line %d: type %d, next %d, value %d, bytes", line, h ? h->field_2_type : -1,
                    h ? h->field_4_cmd_next : -1, h ? h->field_6_return_value : -1);
            for (int k = 8; h && k < 24; ++k)
            {
                fprintf(stderr, " %02x", b[k]);
            }
            fprintf(stderr, "\n");
        }
    }
    const bool ok = sm->AnswerPhone_5129F0(t.field_0_ped_id, t.field_4_obj_f14);
    fprintf(stderr, "gta2: test: answered phone %d: %s\n", gPhone, ok ? "yes" : "no");
    return ok;
}
// On a sanitizer error (GTA2_TEST_TRACE): the command being run and every thread's state.
void OnDeath()
{
    ScriptManager_C1EA8* sm = gScriptManager_6F8060;
    const SCR_CMD_HEADER* cur = reinterpret_cast<const SCR_CMD_HEADER*>(gBasePtr_6F8070);
    fprintf(stderr, "gta2: trace: at the error, command line %d (type %d, next %d), previous line %d\n",
            cur ? cur->field_0_cmd_this : -1, cur ? cur->field_2_type : -1, cur ? cur->field_4_cmd_next : -1,
            gPrevCmdIdx_6F806C);
    if (!miss2_0x11C_Pool_6F8064 || !sm)
    {
        return;
    }
    Pool<miss2_0x11C, 8>& pool = miss2_0x11C_Pool_6F8064->field_0_pool;
    for (miss2_0x11C* t = pool.field_4_pPrev; t; t = t->mpNext)
    {
        fprintf(stderr, "gta2: trace: thread %d (id %d): next line %u%s, stopped %d, exec %d\n",
                static_cast<int>(t - pool.field_8_pool), t->field_11A_thread_id, t->field_4_next_cmd,
                sm->GetBasePointer_512770(t->field_4_next_cmd) ? "" : " (no such line)", t->field_10_stopped,
                t->field_12_exec_flag);
    }
}

void TraceThreads()
{
    static u16 last[8];
    static bool live[8];
    bool seen[8] = {};
    ScriptManager_C1EA8* sm = gScriptManager_6F8060;
    if (!miss2_0x11C_Pool_6F8064 || !sm)
    {
        return;
    }
    Pool<miss2_0x11C, 8>& pool = miss2_0x11C_Pool_6F8064->field_0_pool;
    for (miss2_0x11C* t = pool.field_4_pPrev; t; t = t->mpNext)
    {
        const int slot = static_cast<int>(t - pool.field_8_pool);
        if (slot < 0 || slot >= 8)
        {
            continue;
        }
        seen[slot] = true;
        if (!live[slot] || last[slot] != t->field_4_next_cmd)
        {
            SCR_CMD_HEADER* h = sm->GetBasePointer_512770(t->field_4_next_cmd);
            fprintf(stderr, "gta2: trace: thread %d (id %d): line %u, type %d, next %d\n", slot, t->field_11A_thread_id,
                    t->field_4_next_cmd, h ? h->field_2_type : -1, h ? h->field_4_cmd_next : -1);
            last[slot] = t->field_4_next_cmd;
            live[slot] = true;
        }
    }
    for (int i = 0; i < 8; ++i)
    {
        if (live[i] && !seen[i])
        {
            fprintf(stderr, "gta2: trace: thread %d ended\n", i);
            live[i] = false;
        }
    }
}
} // namespace

extern "C" void Gta2Native_ScenarioInit()
{
    const char* test = getenv("GTA2_TEST");
    if (!test)
    {
        return;
    }
    gTest = !strcmp(test, "save")    ? kSave
        : !strcmp(test, "phone")     ? kPhone
        : !strcmp(test, "watch")     ? kWatch
        : !strcmp(test, "damage")    ? kDamage
                                     : kNone;
    if (const char* delay = getenv("GTA2_TEST_DELAY"))
    {
        gDelay = atof(delay) * 1000.0;
    }
    if (const char* phone = getenv("GTA2_TEST_PHONE"))
    {
        gPhone = atoi(phone);
    }
    fprintf(stderr, "gta2: test: scenario %s\n", gTest == kNone ? "unknown, ignored" : test);
    if (getenv("GTA2_TEST_TRACE") && __sanitizer_set_death_callback)
    {
        __sanitizer_set_death_callback(OnDeath);
    }
}

// Called once per loop iteration with the run's clock (ms).
extern "C" void Gta2Native_ScenarioStep(double now)
{
    static bool unlock = getenv("GTA2_TEST_UNLOCK") && atoi(getenv("GTA2_TEST_UNLOCK")) != 0;
    if (unlock && gta2_dev_unlock_areas())
    {
        fprintf(stderr, "gta2: test: all areas unlocked for this player\n");
        unlock = false;
    }
    if (gTest == kNone)
    {
        return;
    }
    const s32* s = gta2_dev_state();
    Player* player = ThePlayer();
    if (!s[0] || !player)
    {
        gPlayerSince = -1;
        return;
    }
    if (gPlayerSince < 0)
    {
        gPlayerSince = now;
    }
    const double inGame = now - gPlayerSince;
    static const bool trace = getenv("GTA2_TEST_TRACE") && atoi(getenv("GTA2_TEST_TRACE")) != 0;
    if (trace)
    {
        TraceThreads();
    }

    if (gTest == kSave)
    {
        // 0: pay; 1: leave the car (Residential starts in one); 2-3: teleport to the zone's
        // centre or a block next to it, keeping the first spot at the zone's height (a
        // teleport lands on top of the column, the roof at Downtown's church); 10-17:
        // turns and walks until the save is paid for (below).
        static const int kAround[][2] = {{0, 0},  {0, 1},  {0, -1}, {1, 0},  {-1, 0}, {1, 1},  {-1, 1}, {1, -1},
                                         {-1, -1}, {0, 2},  {0, -2}, {2, 0},  {-2, 0}, {2, 2},  {-2, 2}, {2, -2},
                                         {-2, -2}, {1, 2},  {-1, 2}, {1, -2}, {-1, -2}, {2, 1}, {-2, 1}, {2, -1},
                                         {-2, -1}};
        const int kSpots = static_cast<int>(sizeof(kAround) / sizeof(kAround[0]));
        static int zoneX = 0;
        static int zoneY = 0;
        static int zoneZ = 0;
        static int spot = 0;
        static int exits = 0;
        static double enterUpAt = -1;
        if (enterUpAt >= 0 && now >= enterUpAt)
        {
            gta2_port_key(0x1C, 0x0D, 0, 0);
            enterUpAt = -1;
        }
        if (gStep == 0 && inGame > gDelay)
        {
            fprintf(stderr, "gta2: test: score %d after adding 60000\n", gta2_dev_add_cash(60000));
            if (!SaveZone(&zoneX, &zoneY, &zoneZ))
            {
                fprintf(stderr, "gta2: test: no save zone in this district's script\n");
                gStep = 99;
                return;
            }
            gStep = 1;
            gStepAt = now;
        }
        else if (gStep == 1 && now >= gStepAt)
        {
            if (s[1] && exits < 5)
            {
                fprintf(stderr, "gta2: test: leaving the car\n");
                gta2_port_key(0x1C, 0x0D, 1, 0); // Enter: get out
                enterUpAt = now + 150;
                gStepAt = now + 2500;
                ++exits;
            }
            else
            {
                gStep = 2;
            }
        }
        else if (gStep == 2)
        {
            const int moved = gta2_dev_teleport(zoneX + kAround[spot][0], zoneY + kAround[spot][1]);
            if (!moved)
            {
                fprintf(stderr, "gta2: test: cannot move the player (still in a car?)\n");
                gStep = 99;
                return;
            }
            gStep = 3;
            gStepAt = now + 300;
        }
        else if (gStep == 3 && now >= gStepAt)
        {
            const int dz = s[4] - zoneZ;
            fprintf(stderr, "gta2: test: at (%.2f, %.2f, %.2f), the zone is at height %.2f\n", s[2] / 16384.0,
                    s[3] / 16384.0, s[4] / 16384.0, zoneZ / 16384.0);
            if (dz > -10000 && dz < 10000)
            {
                gStep = 10;
                gStepAt = now;
            }
            else if (++spot < kSpots)
            {
                gStep = 2;
            }
            else
            {
                fprintf(stderr, "gta2: test: no spot at the zone's height\n");
                gStep = 99;
            }
        }
        else if (gStep == 10 + 6 * 8)
        {
            // Not paid from this spot (a wall between it and the zone): the next one.
            gStep = ++spot < kSpots ? 2 : 99;
            if (gStep == 99)
            {
                fprintf(stderr, "gta2: test: could not walk into the save zone\n");
            }
        }
        else if (gStep >= 10 && gStep < 10 + 6 * 8 && now >= gStepAt)
        {
            // Eight rounds of: turn left a little, forward 0.5 s, back 0.5 s. The spot is
            // next to the zone, so one of the headings walks into it. Stops once paid.
            if (player->GetScore_421980() < 20000)
            {
                fprintf(stderr, "gta2: test: the save was paid for\n");
                gStep = 98;
                return;
            }
            const int phase = (gStep - 10) % 6;
            static const struct
            {
                int scancode;
                int vk;
                int down;
                int ms;
            } kWiggle[6] = {{0xCB, 0x25, 1, 250}, {0xCB, 0x25, 0, 0}, {0xC8, 0x26, 1, 500},
                            {0xC8, 0x26, 0, 0},   {0xD0, 0x28, 1, 500}, {0xD0, 0x28, 0, 0}};
            gta2_port_key(kWiggle[phase].scancode, kWiggle[phase].vk, kWiggle[phase].down, 0);
            gStepAt = now + kWiggle[phase].ms;
            ++gStep;
        }
    }
    else if (gTest == kWatch)
    {
        gStep = gStep ? gStep : 1;
    }
    else if (gTest == kPhone)
    {
        if (gStep == 0 && inGame > gDelay)
        {
            AnswerPhone();
            gStep = 1;
        }
    }
    else if (gTest == kDamage)
    {
        static const int points = getenv("GTA2_TEST_DAMAGE") ? atoi(getenv("GTA2_TEST_DAMAGE")) : 10;
        static const double every = (getenv("GTA2_TEST_DAMAGE_EVERY") ? atof(getenv("GTA2_TEST_DAMAGE_EVERY")) : 5.0) * 1000.0;
        static double nextHit = -1;
        static int lastHealth = -1;
        Ped* ped = player->GetPlayerPed_41D020();
        if (!ped)
        {
            return;
        }
        gStep = gStep ? gStep : 1;
        const int health = ped->get_health_433B70();
        if (health != lastHealth)
        {
            // Every change, the test's hits and anything else (a respawn, a power-up).
            fprintf(stderr, "gta2: test: %.1f s, health %d -> %d, %d half hearts, invulnerability %u, armour %u\n",
                    inGame / 1000.0, lastHealth, health, HalfHearts(health), ped->field_208_invulnerability,
                    player->field_6F4_power_up_timers[3]);
            lastHealth = health;
        }
        if (nextHit < 0)
        {
            nextHit = gPlayerSince + gDelay;
        }
        if (now >= nextHit)
        {
            nextHit = now + every;
            fprintf(stderr, "gta2: test: hit for %d (health %d, invulnerability %u)\n", points, health,
                    ped->field_208_invulnerability);
            ped->TakeDamage(static_cast<s16>(points));
        }
    }

    if (gStep > 0 && gStep < 98 && now >= gNextLog)
    {
        gNextLog = now + 1000;
        ScriptManager_C1EA8* sm = gScriptManager_6F8060;
        fprintf(stderr, "gta2: test: %.0f s in game, score %d, on a mission %d, loaded from a save %d\n",
                inGame / 1000.0, player->GetScore_421980(), sm && sm->IsOnMission_4C7350() ? 1 : 0,
                sm ? sm->get_field_C1E2C_475A20() : 0);
    }
}
