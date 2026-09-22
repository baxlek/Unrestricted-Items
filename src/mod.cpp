#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

#include <cstdint>
#include <vector>

#include "d/actor/d_a_alink.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2.h"
#include "d/d_meter2_draw.h"
#include "d/d_item_data.h"

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);

DEFINE_HOOK(&daAlink_c::checkAcceptUseItemInWater, CheckAcceptUseItemInWater);
DEFINE_HOOK(&daAlink_c::checkCastleTownUseItem, CheckCastleTownUseItem);
DEFINE_HOOK(&daAlink_c::checkNotBattleStage, CheckNotBattleStage);
DEFINE_HOOK(&daAlink_c::setStartProcInit, SetStartProcInit);
DEFINE_HOOK(&daAlink_c::checkItemAction, CheckItemAction);
DEFINE_HOOK(&daAlink_c::checkItemChangeFromButton, CheckItemChangeFromButton);
DEFINE_HOOK(&daAlink_c::swimDeleteItem, SwimDeleteItem);
DEFINE_HOOK(&daAlink_c::checkWaterInKandelaar, CheckWaterInKandelaar);
DEFINE_HOOK(&daAlink_c::checkKandelaarSwing, CheckKandelaarSwing);
DEFINE_HOOK(&daAlink_c::initKandelaarSwing, InitKandelaarSwing);
DEFINE_HOOK(&daAlink_c::checkNewItemChange, CheckNewItemChange);
DEFINE_HOOK(&daAlink_c::checkNoSubjectModeCamera, CheckNoSubjectModeCamera);
DEFINE_HOOK(&daAlink_c::checkNotHeavyBootsStage, CheckNotHeavyBootsStage);
DEFINE_HOOK(&daAlink_c::checkRoomOnly, CheckRoomOnly);
DEFINE_HOOK(&daAlink_c::procGrassWhistleWait, ProcGrassWhistleWait);
DEFINE_HOOK(&daAlink_c::setLight, SetLight);
DEFINE_HOOK(&dCamera_c::ChangeModeOK, ChangeModeOK);
DEFINE_HOOK(&dCamera_c::Run, CameraRun);
DEFINE_HOOK(&dMeter2_c::alphaAnimeKantera, AlphaAnimeKantera);

namespace {

ConfigVarHandle g_cvar_stage_first_person = 0;
ConfigVarHandle g_cvar_interior_normal_movement = 0;

bool unrestricted_items_enabled() {
    return true;
}

bool stage_first_person_enabled() {
    bool enabled = false;
    return g_cvar_stage_first_person != 0 &&
           svc_config->get_bool(mod_ctx, g_cvar_stage_first_person, &enabled) == MOD_OK && enabled;
}

bool interior_normal_movement_enabled() {
    bool enabled = false;
    return g_cvar_interior_normal_movement != 0 &&
           svc_config->get_bool(mod_ctx, g_cvar_interior_normal_movement, &enabled) == MOD_OK &&
           enabled;
}

enum daAlink_ItemProc {
    ITEM_PROC_NONE = 0,
    ITEM_PROC_BOOTS_EQUIP = 1,
    ITEM_PROC_SET_HVYBOOTS = 2,
    ITEM_PROC_BOTTLE_DRINK = 3,
    ITEM_PROC_SPINNER_READY = 4,
    ITEM_PROC_DUNGEON_WARP_READY = 5,
    ITEM_PROC_BOTTLE_OPEN = 6,
    ITEM_PROC_FISHING_FOOD = 7,
    ITEM_PROC_KANDELAAR_POUR = 8,
    ITEM_PROC_SUBJECTIVITY = 9,
    ITEM_PROC_PICK_PUT = 10,
    ITEM_PROC_OFF_KANDELAAR = 11,
    ITEM_PROC_COMMON_CHANGE_ITEM = 12,
    ITEM_PROC_BOTTLE_SWING = 13,
    ITEM_PROC_NOT_USE_ITEM = 14,
    ITEM_PROC_GRASS_WHISTLE = 15,
};

bool unrestricted_items_water_active(const daAlink_c* player) {
    return unrestricted_items_enabled() &&
           player->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE);
}

bool unrestricted_items_camera_stage() {
    return daAlink_c::checkStageName("F_SP116") || daAlink_c::checkStageName("R_SP160");
}

// checkRoom() = checkRoomOnly() || checkRoomSpecial() || (R_SP161 term). checkRoomSpecial()
// covers D_MN11 rooms 1 and 2 (plus the D_MN04 Lv2-dungeon special case handled separately by
// checkLv2DungeonRoomSpecial/checkNotHeavyBootsStage). These "special" no-battle rooms are
// distinct from the general checkRoomOnly() interior stages toggled by
// interiorNormalMovementEnabled, so they need their own bypass here.
bool unrestricted_items_special_no_battle_room() {
    return daAlink_c::checkRoomSpecial() ||
           (daAlink_c::checkStageName("R_SP161") && !dComIfGs_isOneZoneSwitch(14, -1));
}

bool lantern_ignores_water(const daAlink_c* player) {
    return unrestricted_items_enabled() &&
           (player->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE) ||
            player->checkModeFlg(0x40000));
}

bool water_in_kandelaar_offset(const daAlink_c* player, f32 water_y) {
    const f32 base_y_pos =
        player->checkModeFlg(0x40) ? player->mRightFootPos.y : player->current.pos.y;
    return water_y > 65.0f + base_y_pos;
}

bool lantern_in_water(const daAlink_c* player) {
    return lantern_ignores_water(player) &&
           water_in_kandelaar_offset(player, player->mWaterY);
}

int unrestricted_items_fallback_new_item_change(daAlink_c* player, u8 selected_slot,
                                                u16 selected_item) {
    if (!unrestricted_items_enabled()) {
        return ITEM_PROC_NONE;
    }

    if (player->checkSpinnerRide() || selected_item == dItemNo_BOMB_BAG_LV1_e) {
        return ITEM_PROC_NONE;
    }

    if ((player->checkModeFlg(0x40000) ||
         player->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE)) &&
        !player->checkAcceptUseItemInWater(selected_item))
    {
        return ITEM_PROC_NONE;
    }

    if (player->checkModeFlg(0x40000) && selected_item == dItemNo_WATER_BOMB_e) {
        return ITEM_PROC_NONE;
    }

    if (selected_item == dItemNo_HVY_BOOTS_e ||
        player->checkDungeonWarpItem(selected_item) ||
        player->checkTradeItem(selected_item) ||
        (player->checkBottleItem(selected_item) && selected_item != dItemNo_EMPTY_BOTTLE_e) ||
        selected_item == dItemNo_SPINNER_e ||
        selected_item == dItemNo_POKE_BOMB_e ||
        selected_item == dItemNo_HORSE_FLUTE_e ||
        selected_item == dItemNo_HAWK_EYE_e)
    {
        if (player->checkReinRide() || player->checkCanoeRide()) {
            if (player->checkDrinkBottleItem(selected_item)) {
                return ITEM_PROC_BOTTLE_DRINK;
            }

            if (daAlink_c::checkOilBottleItem(selected_item) &&
                player->checkItemSetButton(dItemNo_KANTERA_e) != 2)
            {
                return ITEM_PROC_KANDELAAR_POUR;
            }
        } else if (selected_item == dItemNo_HVY_BOOTS_e) {
            if (!player->checkBoardRide()) {
                if ((player->mLinkAcch.ChkGroundHit() && !player->checkModeFlg(0x70C52)) ||
                    (player->checkMagneBootsOn() &&
                     cBgW_CheckBGround(player->mMagneBootsTopVec.y)) ||
                    player->mProcID == daAlink_c::PROC_HANG_CLIMB)
                {
                    return ITEM_PROC_BOOTS_EQUIP;
                }
                return ITEM_PROC_SET_HVYBOOTS;
            }
        } else if (player->checkDrinkBottleItem(selected_item) && player->checkMagneBootsOn()) {
            if (cBgW_CheckBGround(player->mMagneBootsTopVec.y)) {
                return ITEM_PROC_BOTTLE_DRINK;
            }
        } else if (player->mLinkAcch.ChkGroundHit() && !player->checkModeFlg(0x70C52)) {
            if (selected_item == dItemNo_SPINNER_e) {
                return ITEM_PROC_SPINNER_READY;
            }
            if (player->checkDungeonWarpItem(selected_item)) {
                return ITEM_PROC_DUNGEON_WARP_READY;
            }
            if (player->checkItemSetButton(0x108) != 2 &&
                (selected_item == dItemNo_WORM_e || selected_item == dItemNo_BEE_CHILD_e))
            {
                const u16 rod_item =
                    dComIfGp_getSelectItem(player->checkItemSetButton(0x108));
                if (rod_item == dItemNo_WORM_ROD_e || rod_item == dItemNo_JEWEL_WORM_ROD_e) {
                    if (selected_item == dItemNo_BEE_CHILD_e) {
                        return ITEM_PROC_BOTTLE_DRINK;
                    }
                    return ITEM_PROC_NONE;
                }
                if (selected_item == dItemNo_BEE_CHILD_e &&
                    (rod_item == dItemNo_BEE_ROD_e || rod_item == dItemNo_JEWEL_BEE_ROD_e))
                {
                    return ITEM_PROC_BOTTLE_DRINK;
                }
                return ITEM_PROC_FISHING_FOOD;
            }
            if (player->checkDrinkBottleItem(selected_item)) {
                return ITEM_PROC_BOTTLE_DRINK;
            }
            if (player->checkOpenBottleItem(selected_item)) {
                return ITEM_PROC_BOTTLE_OPEN;
            }
            if (player->checkTradeItem(selected_item)) {
                return ITEM_PROC_NOT_USE_ITEM;
            }
            if (selected_item == dItemNo_HORSE_FLUTE_e) {
                return ITEM_PROC_GRASS_WHISTLE;
            }
            if (daAlink_c::checkOilBottleItem(selected_item) &&
                player->checkItemSetButton(0x48) != 2)
            {
                return ITEM_PROC_KANDELAAR_POUR;
            }
            if (selected_item == dItemNo_HAWK_EYE_e && player->acceptSubjectModeChange()) {
                return ITEM_PROC_SUBJECTIVITY;
            }
            if (selected_item == dItemNo_POKE_BOMB_e &&
                dComIfGp_getSelectItemNum(selected_slot) &&
                player->field_0x2fcf < 2)
            {
                return ITEM_PROC_PICK_PUT;
            }
        }

    } else if (selected_item != dItemNo_NONE_e && player->mEquipItem != selected_item) {
        if ((player->checkBombItem(selected_item) &&
             !dComIfGp_getSelectItemNum(selected_slot)) ||
            ((selected_item == dItemNo_NORMAL_BOMB_e ||
              selected_item == dItemNo_WATER_BOMB_e) &&
             player->mActiveBombNum >= 3) ||
            (selected_item == dItemNo_IRONBALL_e &&
             (!player->mLinkAcch.ChkGroundHit() || player->checkModeFlg(0x70C52))) ||
            (selected_item == dItemNo_KANTERA_e &&
             (player->checkEndResetFlg1(daAlink_c::ERFLG1_UNK_4) ||
              (!lantern_ignores_water(player) &&
               (player->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE) ||
                player->checkModeFlg(0x40000))))))
        {
            return ITEM_PROC_NONE;
        }

        return ITEM_PROC_COMMON_CHANGE_ITEM;
    }

    if (player->mEquipItem == selected_item &&
        player->mSelectItemId != selected_slot &&
        player->mEquipItem == dItemNo_EMPTY_BOTTLE_e)
    {
        return ITEM_PROC_BOTTLE_SWING;
    }

    return ITEM_PROC_NONE;
}

struct SavedCameraModeStyle {
    dCamera_c* camera;
    int type;
    int fallback_index;
    int mode;
    s16 original_style;
};
std::vector<SavedCameraModeStyle> g_camera_run_style_stack;
struct SavedCameraTagFlags {
    dCamera_c* camera;
    u8 original_flags;
};
std::vector<SavedCameraTagFlags> g_camera_run_tag_flag_stack;

bool stage_first_person_camera_mode(s32 mode) {
    return mode == 4 || mode == 7 || mode == 8;
}

bool stage_hookshot_native_camera_proc(const daAlink_c* player) {
    switch (player->mProcID) {
    case daAlink_c::PROC_HOOKSHOT_ROOF_WAIT:
    case daAlink_c::PROC_HOOKSHOT_ROOF_SHOOT:
    case daAlink_c::PROC_HOOKSHOT_ROOF_BOOTS:
    case daAlink_c::PROC_HOOKSHOT_WALL_WAIT:
    case daAlink_c::PROC_HOOKSHOT_WALL_SHOOT:
    case daAlink_c::PROC_HOOKSHOT_FLY:
        return true;
    default:
        return false;
    }
}

bool valid_camera_type(const dCamera_c* camera, int type) {
    return type >= 0 && type < camera->mCamTypeNum;
}

int resolve_camera_style_index(const dCamera_c* camera, int type_a, int type_b) {
    return camera->mCamTypeData[type_a].field_0x18[camera->mIsWolf][0] >= 0 &&
                   camera->mCamTypeData[type_b].field_0x18[camera->mIsWolf][0] >= 0
               ? camera->mIsWolf
               : 0;
}

void patch_camera_style_mode(dCamera_c* camera, int type, int field_type, int mode) {
    if (!valid_camera_type(camera, type) || !valid_camera_type(camera, field_type)) {
        return;
    }

    const int fallback_index = resolve_camera_style_index(camera, type, field_type);
    const s16 current_style = camera->mCamTypeData[type].field_0x18[fallback_index][mode];
    const s16 fallback_style = camera->mCamTypeData[field_type].field_0x18[fallback_index][mode];
    if (fallback_style < 0 || current_style == fallback_style) {
        return;
    }

    g_camera_run_style_stack.push_back({camera, type, fallback_index, mode, current_style});
    camera->mCamTypeData[type].field_0x18[fallback_index][mode] = fallback_style;
}

void patch_camera_mode_style(dCamera_c* camera, int type, int field_type, int mode) {
    if (!stage_first_person_camera_mode(mode)) {
        return;
    }

    patch_camera_style_mode(camera, type, field_type, mode);
}

HookAction on_camera_run_pre(ModContext*, void* args, void*, void*) {
    auto* camera = mods::arg<dCamera_c*>(args, 0);
    if (!stage_first_person_enabled() || !unrestricted_items_camera_stage()) {
        return HOOK_CONTINUE;
    }

    auto* player = static_cast<daAlink_c*>(daPy_getPlayerActorClass());
    if (player != nullptr && stage_hookshot_native_camera_proc(player) &&
        (camera->mTagCamTool.mFlags & 0x10) == 0)
    {
        g_camera_run_tag_flag_stack.push_back({camera, camera->mTagCamTool.mFlags});
        camera->mTagCamTool.mFlags |= 0x10;
    }

    const int field_type = camera->GetCameraTypeFromCameraName("FieldS");
    const int scope_type = camera->GetCameraTypeFromCameraName("Scope");
    const int hook_wall_type = camera->GetCameraTypeFromCameraName("HookWall");
    const int hook_roof_type = camera->GetCameraTypeFromCameraName("HookRoof");
    const int hook_actor_type = camera->GetCameraTypeFromCameraName("HookActor");
    if (field_type < 0) {
        return HOOK_CONTINUE;
    }

    if (player != nullptr && player->mProcID == daAlink_c::PROC_HOOKSHOT_FLY) {
        patch_camera_style_mode(camera, camera->mCurType, field_type, 9);
        patch_camera_style_mode(camera, camera->mMapToolType, field_type, 9);
        if (camera->mRoomMapTool.mCameraIndex != 0xff) {
            const int room_map_type = camera->GetCameraTypeFromToolData(&camera->mRoomMapTool.mCamData);
            patch_camera_style_mode(camera, room_map_type, field_type, 9);
        }
    }

    if (camera->mCurType == scope_type || camera->mCurType == hook_wall_type ||
        camera->mCurType == hook_roof_type || camera->mCurType == hook_actor_type)
    {
        return HOOK_CONTINUE;
    }

    static constexpr int k_stage_first_person_modes[] = {4, 7, 8};
    for (const int mode : k_stage_first_person_modes) {
        patch_camera_mode_style(camera, camera->mCurType, field_type, mode);
    }

    return HOOK_CONTINUE;
}

void on_camera_run_post(ModContext*, void* args, void*, void*) {
    auto* camera = mods::arg<dCamera_c*>(args, 0);
    while (!g_camera_run_tag_flag_stack.empty() &&
           g_camera_run_tag_flag_stack.back().camera == camera)
    {
        const SavedCameraTagFlags saved = g_camera_run_tag_flag_stack.back();
        g_camera_run_tag_flag_stack.pop_back();
        camera->mTagCamTool.mFlags = saved.original_flags;
    }

    while (!g_camera_run_style_stack.empty() &&
           g_camera_run_style_stack.back().camera == camera)
    {
        const SavedCameraModeStyle saved = g_camera_run_style_stack.back();
        g_camera_run_style_stack.pop_back();
        camera->mCamTypeData[saved.type].field_0x18[saved.fallback_index][saved.mode] =
            saved.original_style;
    }
}

HookAction on_check_water_in_kandelaar_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const f32 water_y = mods::arg<f32>(args, 1);
    if (player->mEquipItem == dItemNo_KANTERA_e &&
        unrestricted_items_water_active(player) &&
        player->checkNoResetFlg2(daAlink_c::FLG2_UNK_1) &&
        water_in_kandelaar_offset(player, water_y))
    {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void on_check_kandelaar_swing_post(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<const daAlink_c*>(args, 0);
    const int allow_unlit = mods::arg<int>(args, 1);
    auto& result = *static_cast<bool*>(retval);
    if (allow_unlit != 0 || !result) {
        return;
    }

    if (lantern_in_water(player)) {
        result = false;
    }
}

void replace_check_new_item_change(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const u8 selected_slot = mods::arg<u8>(args, 1);
    const u16 selected_item = dComIfGp_getSelectItem(selected_slot);
    auto& result = *static_cast<int*>(retval);
    if (unrestricted_items_enabled()) {
        result = unrestricted_items_fallback_new_item_change(player, selected_slot, selected_item);
        return;
    }

    result = CheckNewItemChange::g_orig(player, selected_slot);
}

std::vector<daAlink_c*> g_set_light_restore_stack;
struct SavedOilCount {
    daAlink_c* player;
    s32 oil_count;
    s32 item_oil_count;
    s32 item_now_oil;
};
std::vector<SavedOilCount> g_init_kandelaar_swing_oil_stack;

HookAction on_set_light_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (lantern_in_water(player) &&
        !player->checkNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF))
    {
        player->onNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF);
        g_set_light_restore_stack.push_back(player);
    }
    return HOOK_CONTINUE;
}

void on_set_light_post(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (!g_set_light_restore_stack.empty() &&
        g_set_light_restore_stack.back() == player)
    {
        player->offNoResetFlg2(daAlink_c::FLG2_KANDELAAR_LIGHT_OFF);
        g_set_light_restore_stack.pop_back();
    }
}

void replace_alpha_anime_kantera(ModContext*, void* args, void*, void*) {
    auto* meter = mods::arg<dMeter2_c*>(args, 0);
    auto* player = static_cast<daAlink_c*>(daPy_getPlayerActorClass());
    if (player != nullptr && lantern_in_water(player)) {
        meter->getMeterDrawPtr()->setAlphaKanteraAnimeMin();
        meter->getMeterDrawPtr()->setAlphaKanteraChange(true);
        return;
    }

    AlphaAnimeKantera::g_orig(meter);
}

void replace_check_accept_use_item_in_water(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<const daAlink_c*>(args, 0);
    const u16 item_no = mods::arg<u16>(args, 1);
    auto& result = *static_cast<BOOL*>(retval);

    if (unrestricted_items_water_active(player)) {
        result = true;
        return;
    }

    result = CheckAcceptUseItemInWater::g_orig(player, item_no) != FALSE;
}

void replace_check_castle_town_use_item(ModContext*, void* args, void* retval, void*) {
    const u16 item_no = mods::arg<u16>(args, 0);
    auto& result = *static_cast<bool*>(retval);
    if (unrestricted_items_enabled()) {
        result = true;
        return;
    }

    result = CheckCastleTownUseItem::g_orig(item_no);
}

// checkNotBattleStage() = checkRoom() || checkCastleTown(), and checkRoom() = checkRoomOnly()
// || checkRoomSpecial() || (R_SP161 term). Some third-party mods (e.g. HUD mods that add extra
// item slots) install their own add-pre hook on checkItemChangeFromButton that reimplements the
// vanilla sword-trigger logic, including its own direct call to checkNotBattleStage(). An
// add-pre hook that returns HOOK_SKIP_ORIGINAL runs instead of - and is never superseded by -
// our replace-hook on checkItemChangeFromButton, so overriding that target alone cannot fix
// Castle Town (or the special no-battle rooms below) for players using such a mod. Patching
// checkNotBattleStage() itself instead affects every caller uniformly (vanilla code, our own
// fallback, and any other mod's reimplementation), which is why we drop the Castle Town and
// special-room terms here and leave checkRoomOnly() (and the separate interior-normal-movement
// toggle it respects) untouched.
void replace_check_not_battle_stage(ModContext*, void*, void* retval, void*) {
    auto& result = *static_cast<bool*>(retval);
    if (unrestricted_items_enabled() &&
        (daAlink_c::checkCastleTown() || unrestricted_items_special_no_battle_room()))
    {
        result = daAlink_c::checkRoomOnly();
        return;
    }

    result = CheckNotBattleStage::g_orig();
}

void replace_swim_delete_item(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const bool keep_lantern_out =
        player->mEquipItem == dItemNo_KANTERA_e && unrestricted_items_water_active(player);

    if (!player->checkHookshotItem(player->mEquipItem) &&
        !keep_lantern_out &&
        (player->mEquipItem != 0x103 || !player->checkBootsOrArmorHeavy()))
    {
        player->deleteEquipItem(TRUE, TRUE);
    }

    if (!keep_lantern_out && player->checkNoResetFlg2(daAlink_c::FLG2_UNK_1)) {
        player->offKandelaarModel();
    }
}

void replace_check_no_subject_mode_camera(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    auto& result = *static_cast<bool*>(retval);
    if (stage_first_person_enabled() && unrestricted_items_camera_stage()) {
        result = player->checkCargoCarry();
        return;
    }

    result = CheckNoSubjectModeCamera::g_orig(player);
}

void replace_check_not_heavy_boots_stage(ModContext*, void*, void* retval, void*) {
    auto& result = *static_cast<bool*>(retval);
    if (unrestricted_items_enabled()) {
        result = false;
        return;
    }

    result = CheckNotHeavyBootsStage::g_orig();
}

// checkRoomOnly() marks interior stages (houses, shops, and similar rooms) that the game
// otherwise limits to walking speed, disallow climbing/hanging, and treats as non-battle
// stages (blocking sword draw, guarding, etc. via checkNotBattleStage/checkNotAutoJumpStage).
// Bypassing it here restores normal movement and combat while indoors, without touching the
// separate Castle Town and special dungeon no-battle-room restrictions.
void replace_check_room_only(ModContext*, void*, void* retval, void*) {
    auto& result = *static_cast<bool*>(retval);
    if (interior_normal_movement_enabled()) {
        result = false;
        return;
    }

    result = CheckRoomOnly::g_orig();
}

void replace_set_start_proc_init(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    auto& result = *static_cast<int*>(retval);
    result = SetStartProcInit::g_orig(player);
}

void replace_check_item_action(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const bool bypass_fishing_water_limit =
        unrestricted_items_enabled() &&
        daAlink_c::checkFishingRodItem(player->mEquipItem) &&
        player->mLinkAcch.ChkGroundHit() &&
        !player->checkNoResetFlg0(daAlink_c::FLG0_SWIM_UP);
    const f32 saved_water_y = player->mWaterY;

    if (bypass_fishing_water_limit) {
        player->mWaterY = player->current.pos.y;
    }

    auto& result = *static_cast<BOOL*>(retval);
    result = CheckItemAction::g_orig(player);
    player->mWaterY = saved_water_y;
}

void replace_check_item_change_from_button(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    auto& result = *static_cast<BOOL*>(retval);
    result = CheckItemChangeFromButton::g_orig(player);

    if (result || !unrestricted_items_enabled()) {
        return;
    }

    if (player->checkModeFlg(4) &&
        !player->checkEquipAnime() &&
        !player->checkBoomerangThrowAnime() &&
        !player->checkCopyRodThrowAnime() &&
        !player->checkKandelaarSwingAnime() &&
        !player->checkCanoeRide() &&
        (!player->checkModeFlg(0x40000) || player->checkEquipHeavyBoots()) &&
        player->mEquipItem != 0x103 &&
        player->swordTrigger() &&
        !player->checkEndResetFlg1(daAlink_c::ERFLG1_SWORD_TRIGGER_NON))
    {
        player->swordEquip(TRUE);
        result = TRUE;
    }
}

void replace_proc_grass_whistle_wait(ModContext*, void* args, void* retval, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    const bool suppress_underwater_horse_call =
        unrestricted_items_enabled() &&
        (player->mProcVar2.field_0x300c == 1 || player->mProcVar2.field_0x300c == 3) &&
        player->mProcVar0.field_0x3008 == 1 &&
        !player->checkNoResetFlg0(daAlink_c::FLG0_SWIM_UP);
    const s16 saved_whistle_type = player->mProcVar2.field_0x300c;

    if (suppress_underwater_horse_call) {
        player->mProcVar2.field_0x300c = 0;
    }

    auto& result = *static_cast<int*>(retval);
    result = ProcGrassWhistleWait::g_orig(player);
    player->mProcVar2.field_0x300c = saved_whistle_type;
}

void replace_change_mode_ok(ModContext*, void* args, void* retval, void*) {
    auto* camera = mods::arg<dCamera_c*>(args, 0);
    const s32 mode = mods::arg<s32>(args, 1);
    auto& result = *static_cast<bool*>(retval);
    result = ChangeModeOK::g_orig(camera, mode);

    if (result || !stage_first_person_camera_mode(mode) || !stage_first_person_enabled() ||
        !unrestricted_items_camera_stage())
    {
        return;
    }

    const int field_type = camera->GetCameraTypeFromCameraName("FieldS");
    if (field_type >= 0 &&
        camera->mCamTypeData[field_type]
                .field_0x18[resolve_camera_style_index(camera, camera->mCurType, field_type)][mode] >= 0)
    {
        result = true;
    }
}

HookAction on_init_kandelaar_swing_pre(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (player->mEquipItem == dItemNo_KANTERA_e &&
        lantern_in_water(player) &&
        !player->checkEventRun())
    {
        g_init_kandelaar_swing_oil_stack.push_back(
            {player, dComIfGs_getOil(), dComIfGp_getItemOilCount(), dComIfGp_getItemNowOil()});
    }
    return HOOK_CONTINUE;
}

void on_init_kandelaar_swing_post(ModContext*, void* args, void*, void*) {
    auto* player = mods::arg<daAlink_c*>(args, 0);
    if (!g_init_kandelaar_swing_oil_stack.empty() &&
        g_init_kandelaar_swing_oil_stack.back().player == player)
    {
        const s32 saved_oil = g_init_kandelaar_swing_oil_stack.back().oil_count;
        const s32 saved_item_oil = g_init_kandelaar_swing_oil_stack.back().item_oil_count;
        const s32 saved_item_now_oil = g_init_kandelaar_swing_oil_stack.back().item_now_oil;
        g_init_kandelaar_swing_oil_stack.pop_back();
        const s32 oil_delta = saved_oil - dComIfGs_getOil();
        const s32 item_oil_delta = saved_item_oil - dComIfGp_getItemOilCount();
        if (oil_delta != 0) {
            dComIfGs_setOil(saved_oil);
        }
        if (item_oil_delta != 0) {
            dComIfGp_setItemOilCount(item_oil_delta);
        }
        dComIfGp_setItemNowOil(saved_item_now_oil);
    }
}

// Some third-party HUD/item mods install their own replace-hook on the same targets we do
// (e.g. to add extra item slots). Dusklight's default replace policy (HOOK_REPLACE_CONFLICT)
// means whichever mod installs first "wins" and the other's install call fails outright. For
// the handful of hooks that are essential to unrestricted-items' core purpose (letting the
// sword be drawn in Castle Town), we ask to unconditionally take over the target instead of
// silently losing to another mod's replace-hook based on load order.
const HookOptions* critical_replace_options() {
    static const HookOptions options = {
        sizeof(HookOptions), INT32_MAX, HOOK_REPLACE_OVERRIDE, nullptr};
    return &options;
}

// A single conflicting/failed hook install must not take down every other independent fix this
// mod provides. install_hook() logs failures but always lets mod_initialize keep going, so e.g.
// a HUD mod that conflicts with an earlier, unrelated hook doesn't silently disable the Castle
// Town sword-draw fix (or any other toggle) registered later in mod_initialize.
ModResult install_hook(ModResult result, const char* name) {
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, name);
    }
    return result;
}

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Enable FPV in Castle Town";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvar_stage_first_person;
    ModResult result = svc_ui->pane_add_control(mod_ctx, panel, &control, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Move Normally in Houses";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvar_interior_normal_movement;
    return svc_ui->pane_add_control(mod_ctx, panel, &control, nullptr);
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    ModResult result = MOD_OK;

    ConfigVarDesc first_person_desc = CONFIG_VAR_DESC_INIT;
    first_person_desc.name = "stageFirstPersonEnabled";
    first_person_desc.type = CONFIG_VAR_BOOL;
    first_person_desc.default_bool = false;

    result = svc_config->register_var(mod_ctx, &first_person_desc, &g_cvar_stage_first_person);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to register stage-first-person cvar");
        return result;
    }

    ConfigVarDesc interior_movement_desc = CONFIG_VAR_DESC_INIT;
    interior_movement_desc.name = "interiorNormalMovementEnabled";
    interior_movement_desc.type = CONFIG_VAR_BOOL;
    interior_movement_desc.default_bool = false;

    result = svc_config->register_var(
        mod_ctx, &interior_movement_desc, &g_cvar_interior_normal_movement);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to register interior-normal-movement cvar");
        return result;
    }

    UiModsPanelDesc panel_desc = UI_MODS_PANEL_DESC_INIT;
    panel_desc.build = build_panel;
    result = svc_ui->register_mods_panel(mod_ctx, &panel_desc);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to register unrestricted-items mod panel");
        return result;
    }

    // From here on, hook installs are individually optional: a conflict on any one target (for
    // example, another mod's replace-hook on an unrelated function) must not prevent the rest of
    // this mod's fixes from installing. install_hook() logs each failure; we deliberately do not
    // return early so a single conflict can't silently disable every other toggle this mod
    // provides (including the Castle Town sword-draw fix, if it happens to be registered after
    // whichever hook conflicted).
    install_hook(
        mods::hook_replace<CheckAcceptUseItemInWater>(
            svc_hook, replace_check_accept_use_item_in_water),
        "failed to install CheckAcceptUseItemInWater");

    // These hooks are what let the sword be equipped/drawn while in Castle Town. Some
    // third-party HUD/item mods install their own replace-hook (or an add-pre hook that
    // reimplements the same logic) on these targets, e.g. to support extra item slots; take
    // over CheckCastleTownUseItem/SetStartProcInit unconditionally so our fix isn't silently
    // lost to HOOK_REPLACE_CONFLICT based on mod load order. CheckNotBattleStage additionally
    // guards against mods whose own add-pre hook on CheckItemChangeFromButton bypasses our
    // replace-hook there entirely (see replace_check_not_battle_stage for details).
    install_hook(
        mods::hook_replace<CheckCastleTownUseItem>(
            svc_hook, replace_check_castle_town_use_item, critical_replace_options()),
        "failed to install CheckCastleTownUseItem");

    install_hook(
        mods::hook_replace<CheckNotBattleStage>(
            svc_hook, replace_check_not_battle_stage, critical_replace_options()),
        "failed to install CheckNotBattleStage");

    install_hook(
        mods::hook_replace<SetStartProcInit>(
            svc_hook, replace_set_start_proc_init, critical_replace_options()),
        "failed to install SetStartProcInit");

    install_hook(
        mods::hook_replace<CheckItemAction>(svc_hook, replace_check_item_action),
        "failed to install CheckItemAction");

    install_hook(
        mods::hook_replace<CheckItemChangeFromButton>(
            svc_hook, replace_check_item_change_from_button, critical_replace_options()),
        "failed to install CheckItemChangeFromButton");

    install_hook(
        mods::hook_replace<SwimDeleteItem>(svc_hook, replace_swim_delete_item),
        "failed to install SwimDeleteItem");

    install_hook(
        mods::hook_add_pre<CheckWaterInKandelaar>(
            svc_hook, on_check_water_in_kandelaar_pre),
        "failed to install CheckWaterInKandelaar");

    install_hook(
        mods::hook_add_post<CheckKandelaarSwing>(
            svc_hook, on_check_kandelaar_swing_post),
        "failed to install CheckKandelaarSwing");

    install_hook(
        mods::hook_add_pre<InitKandelaarSwing>(svc_hook, on_init_kandelaar_swing_pre),
        "failed to install InitKandelaarSwing pre-hook");

    install_hook(
        mods::hook_add_post<InitKandelaarSwing>(svc_hook, on_init_kandelaar_swing_post),
        "failed to install InitKandelaarSwing post-hook");

    install_hook(
        mods::hook_replace<CheckNewItemChange>(
            svc_hook, replace_check_new_item_change),
        "failed to install CheckNewItemChange");

    install_hook(
        mods::hook_replace<CheckNoSubjectModeCamera>(
            svc_hook, replace_check_no_subject_mode_camera),
        "failed to install CheckNoSubjectModeCamera");

    install_hook(
        mods::hook_replace<CheckNotHeavyBootsStage>(
            svc_hook, replace_check_not_heavy_boots_stage),
        "failed to install CheckNotHeavyBootsStage");

    install_hook(
        mods::hook_replace<CheckRoomOnly>(svc_hook, replace_check_room_only),
        "failed to install CheckRoomOnly");

    install_hook(
        mods::hook_replace<ProcGrassWhistleWait>(svc_hook, replace_proc_grass_whistle_wait),
        "failed to install ProcGrassWhistleWait");

    install_hook(
        mods::hook_add_pre<SetLight>(svc_hook, on_set_light_pre),
        "failed to install SetLight pre-hook");

    install_hook(
        mods::hook_add_post<SetLight>(svc_hook, on_set_light_post),
        "failed to install SetLight post-hook");

    install_hook(
        mods::hook_replace<AlphaAnimeKantera>(svc_hook, replace_alpha_anime_kantera),
        "failed to install AlphaAnimeKantera");

    install_hook(
        mods::hook_replace<ChangeModeOK>(svc_hook, replace_change_mode_ok),
        "failed to install ChangeModeOK");

    install_hook(
        mods::hook_add_post<CameraRun>(svc_hook, on_camera_run_post),
        "failed to install CameraRun post-hook");

    install_hook(
        mods::hook_add_pre<CameraRun>(svc_hook, on_camera_run_pre),
        "failed to install CameraRun pre-hook");

    svc_log->info(mod_ctx, "unrestricted_items initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}
}
