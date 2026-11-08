// Test scenarios for the parts of the game a random soak does not reach (native build).
//
//   GTA2_TEST=save         save at the church: gives the player the 50,000 points the
//                          save costs, then moves them into the script's save zone
//                          (SCRCMD_DO_SAVE_GAME), as walking into the church does
//   GTA2_TEST=phone        answers a mission phone, as touching it does
//                          (Char_B4::PhoneTouched_5535B0 -> AnswerPhone_5129F0), then
//                          lets the soak (GTA2_SOAK=1) play while the mission runs
//   GTA2_TEST_PHONE=n      which of the district's mission phones (default 0)
//   GTA2_TEST=watch        only the log below (after loading a save, for example)
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
#include "miss2_0x11C.hpp"

extern "C" const s32* gta2_dev_state();
extern "C" int gta2_dev_add_cash(int cash);
extern "C" int gta2_dev_teleport(int x, int y);
extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);

namespace
{
enum Test
{
    kNone,
    kSave,
    kPhone,
    kWatch
};
Test gTest = kNone;
int gPhone = 0;
double gPlayerSince = -1; // run clock (ms) when the player first existed
int gStep = 0;
double gNextLog = 0;
double gStepAt = 0;

// Up and down arrows (DirectInput, VK): walk forward, then back, so the ped moves through
// the touch point the script put on the trigger (Car_214::AddThreadTrigger_5C86C0): touch
// points fire on contact while moving, not on a teleport alone.
void Walk(int step)
{
    const bool forward = step == 0 || step == 1;
    gta2_port_key(forward ? 0xC8 : 0xD0, forward ? 0x26 : 0x28, step == 0 || step == 2, 0);
}

int ToBlocks(const Fix16& f)
{
    return f.mValue >> 14; // Fix16: 14 fractional bits
}

Player* ThePlayer()
{
    return gGame_0x40_67E008 ? gGame_0x40_67E008->field_38_orf1 : nullptr;
}

bool SaveZone(int* x, int* y)
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
            }
        }
    }
    return found > 0;
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
        fprintf(stderr, "gta2: test: phone %d: ped %d, object %d, command %u (type %u)\n", i, t.field_0_ped_id,
                t.field_4_obj_f14, t.field_8_cmd_line, h ? h->field_2_type : 0);
    }
    if (gPhone >= count)
    {
        return false;
    }
    const ThreadTrigger_C& t = sm->field_188_phone_triggers[gPhone];
    const bool ok = sm->AnswerPhone_5129F0(t.field_0_ped_id, t.field_4_obj_f14);
    fprintf(stderr, "gta2: test: answered phone %d: %s\n", gPhone, ok ? "yes" : "no");
    return ok;
}
} // namespace

extern "C" void Gta2Native_ScenarioInit()
{
    const char* test = getenv("GTA2_TEST");
    if (!test)
    {
        return;
    }
    gTest = !strcmp(test, "save") ? kSave : !strcmp(test, "phone") ? kPhone : !strcmp(test, "watch") ? kWatch : kNone;
    if (const char* phone = getenv("GTA2_TEST_PHONE"))
    {
        gPhone = atoi(phone);
    }
    fprintf(stderr, "gta2: test: scenario %s\n", gTest == kNone ? "unknown, ignored" : test);
}

// Called once per loop iteration with the run's clock (ms).
extern "C" void Gta2Native_ScenarioStep(double now)
{
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

    if (gTest == kSave)
    {
        if (gStep == 0 && inGame > 3000)
        {
            fprintf(stderr, "gta2: test: score %d after adding 60000\n", gta2_dev_add_cash(60000));
            int x = 0;
            int y = 0;
            if (!SaveZone(&x, &y))
            {
                fprintf(stderr, "gta2: test: no save zone in this district's script\n");
                gStep = 99;
                return;
            }
            // One block south of the zone's centre: the centre itself can be under a roof,
            // and a teleport lands on top of the column (the church's at (159, 137)).
            const int moved = gta2_dev_teleport(x, y + 1);
            fprintf(stderr, "gta2: test: moved into the save zone: %s\n", moved ? "yes" : "no (in a car?)");
            gStep = 1;
            gStepAt = now;
        }
        // Then forward for 0.5 s and back for 0.5 s, twice.
        else if (gStep >= 1 && gStep <= 8 && now >= gStepAt + 500)
        {
            Walk((gStep - 1) % 4);
            ++gStep;
            gStepAt = now;
        }
    }
    else if (gTest == kWatch)
    {
        gStep = gStep ? gStep : 1;
    }
    else if (gTest == kPhone)
    {
        if (gStep == 0 && inGame > 3000)
        {
            AnswerPhone();
            gStep = 1;
        }
    }

    if (gStep > 0 && gStep < 99 && now >= gNextLog)
    {
        gNextLog = now + 1000;
        ScriptManager_C1EA8* sm = gScriptManager_6F8060;
        fprintf(stderr, "gta2: test: %.0f s in game, score %d, on a mission %d, loaded from a save %d\n",
                inGame / 1000.0, player->GetScore_421980(), sm && sm->IsOnMission_4C7350() ? 1 : 0,
                sm ? sm->get_field_C1E2C_475A20() : 0);
    }
}
