/* Illustrative snapshots only. Prices/effects are not a second balance engine.
 * Replace this catalog with a read-only provider in a separately authorized phase. */
#include "bp_ui.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define LINES(name, ...) static const BpUiLine name[] = { __VA_ARGS__ }
#define ROW(id,label,value,state,action,target,question,reason,lines) \
    {id,label,value,state,action,target,question,reason,lines,COUNT(lines),BP_UI_ITEM,NULL,NULL}
#define ROWV(id,label,value,state,action,target,question,reason,lines,view) \
    {id,label,value,state,action,target,question,reason,lines,COUNT(lines),BP_UI_ITEM,view,NULL}
#define SECTION(id,label,value) {id,label,value,BP_UI_AVAILABLE,BP_UI_NONE,NULL,NULL,NULL,NULL,0,BP_UI_SECTION,NULL,NULL}

LINES(poison, {"Poison resistance 73% to 76%"}, {"100 BP - 350 BP remaining"}, {"With equipped armor. Rank 1 to 2."}, {"Aya protection 10% to 20%"}, {"Does not shorten existing poison."}, {"Refunds at Wayne's Workshop."});
LINES(confusion, {"Confusion resistance 0% to 10%"}, {"75 BP - 375 BP remaining"}, {"No matching armor protection."});
LINES(darkness, {"Darkness resistance 20% to 30%"}, {"125 BP - 325 BP remaining"}, {"Reduces chance, not duration."});
LINES(stiffness, {"Lock resistance 55% to 60%"}, {"Slow resistance 73% to 76%"}, {"100 BP - 350 BP remaining"}, {"Both effects share one upgrade."});
LINES(fortitude, {"First charge softens an HP effect."}, {"200 BP - 250 BP remaining"}, {"Only Halve HP and HP to 1."}, {"Refills each battle."}, {"Ordinary attached damage remains."}, {"Progression is provisional."});
LINES(settings, {"Protect against both"}, {"Alternative: save for HP to 1"}, {"Setting preview - no live changes."});
LINES(heal, {"Healing +15% to +30%"}, {"350 BP - 100 BP remaining"}, {"Casting discount remains 15%."}, {"Applies to learned Heal powers."}, {"Future Heal powers share mastery."}, {"Full costs require live provider."});
LINES(haste, {"Duration +25% to +50%"}, {"250 BP - 200 BP remaining"}, {"Casting cost unchanged."});
LINES(slow, {"Neutral chance 50% to 60%"}, {"100 BP - 350 BP remaining"}, {"Does not bypass immunity."});
LINES(attack, {"Damage +20%. Cast cost -15%."}, {"Maximum rank already purchased."}, {"No further BP can be spent here."});
LINES(scan, {"Every Scan becomes free."}, {"150 BP - 300 BP remaining"}, {"Provisional progression."});
LINES(locked, {"Learn this power before upgrading."}, {"Name and effects remain hidden."});
LINES(preraise, {"Need 150 more BP"}, {"Next rank: 600 BP. Available: 450."}, {"Unavailable fixture, not final price."});
LINES(details, {"Use detail page controls for more."}, {"Every comparison shows totals."}, {"Nothing can be purchased in demo."});
LINES(requisition, {"Supplies for Junk."}, {"Select an item to inspect its cost."}, {"No inventory service connected."});
LINES(rebuild, {"Recover dismantled equipment."}, {"Effects are not restored."}, {"No inventory service connected."});
LINES(reset, {"Refund eligible BP for Junk."}, {"Weapon and armor BP excluded."}, {"No refunds happen in this preview."});
LINES(supplies, {"Medicine 2 - receive 1"}, {"30 Junk - 170 Junk remaining"}, {"Requires one free inventory slot."});
LINES(tool, {"Tool - receive 1"}, {"80 Junk - 120 Junk remaining"}, {"Illustrative quote only."});
LINES(gear, {"M92F with retained +bonuses"}, {"150 Junk - 50 Junk remaining"}, {"Empty effect slots. No effects back."});
LINES(stage, {"Editing build - Not applied"}, {"Stage changes without purchase prompts."}, {"No live ranks or BP are modified."});
LINES(review, {"Poison Rank 3 to 0"}, {"Heal Rank 1 to 2"}, {"BP after changes: 220"}, {"Only changed upgrades shown."}, {"Illustrative session, not editable yet."});
LINES(discard, {"Your current build stays unchanged."}, {"Today's respec remains available."}, {"Preview session only."});
LINES(vanilla, {"Resistance Off. Mastery Off."}, {"Compact BP feedback. Vanilla earning."}, {"Purchased ranks retained, inactive."}, {"No BP spent or refunded."}, {"Other mod categories unchanged."});
LINES(enhanced, {"Resistance On. Mastery On."}, {"Compact feedback. Vanilla earning."}, {"You choose where to spend BP."});
LINES(resistance_only, {"Resistance On. Mastery Off."}, {"Mastery investments retained."}, {"No new rewards or automatic spending."});
LINES(mastery_only, {"Resistance Off. Mastery On."}, {"Resistance investments retained."}, {"No change to Combat or Economy."});
LINES(custom, {"Independent BP-owned settings."}, {"Preview and explicit Apply required."}, {"Live settings are not connected."});
LINES(guide, {"Advice only - no auto allocation."}, {"Compare upgrades for your playstyle."}, {"Guide selection changes no rules."});


LINES(weapon_help, {"Choose a weapon, then a stat."}, {"The selected item stays remembered."}, {"Preview only. No BP is spent."});
LINES(armor_help, {"Choose armor, then a stat."}, {"Only the selected armor is affected."}, {"Preview only. No BP is spent."});
LINES(stat_help, {"Current and proposed totals are shown."}, {"Price and remaining BP stay visible."}, {"Nothing changes until confirmation."}, {"This preview never applies purchases."});
#define VIEW(name,title,before,after,benefit,note,cost,left) \
    static const BpUiPreview name = {title,before,after,benefit,note,cost,left}
VIEW(v_m92f,"M92F","Attack 21",NULL,"Range 35","Capacity 8",NULL,NULL);
VIEW(v_m84f,"M84F","Attack 18",NULL,"Range 30","Capacity 6",NULL,NULL);
VIEW(v_nvest,"N. VEST","Defense 24",NULL,"P. Energy 8","Critical 8",NULL,NULL);
VIEW(v_gun_attack,"ATTACK","21","22","+1 attack","Weapon stat total","100 BP","350 BP");
VIEW(v_gun_range,"RANGE","35","36","+1 range","Weapon stat total","100 BP","350 BP");
VIEW(v_gun_capacity,"CAPACITY","8","9","+1 capacity","Weapon stat total","100 BP","350 BP");
VIEW(v_m84_attack,"ATTACK","18","19","+1 attack","Weapon stat total","100 BP","350 BP");
VIEW(v_m84_range,"RANGE","30","31","+1 range","Weapon stat total","100 BP","350 BP");
VIEW(v_m84_capacity,"CAPACITY","6","7","+1 capacity","Weapon stat total","100 BP","350 BP");
VIEW(v_defense,"DEFENSE","24","25","+1 defense","Armor stat total","100 BP","350 BP");
VIEW(v_pe_defense,"P. ENERGY","8","9","+1 stat point","Armor stat total","100 BP","350 BP");
VIEW(v_critical,"CRITICAL","8","9","+1 stat point","Armor stat total","100 BP","350 BP");
VIEW(v_active,"ACTIVE TIME","1","2","+1 stat point","Sample stat totals","100 BP","350 BP");
VIEW(v_capacity,"ITEM CAPACITY","14","15","+1 item slot","Carry one more item","100 BP","350 BP");
VIEW(v_poison,"POISON","73%","76%","Rank 1 to 2","With equipped armor","100 BP","350 BP");
VIEW(v_confusion,"CONFUSION","0%","10%","Rank 0 to 1","No armor protection","75 BP","375 BP");
VIEW(v_darkness,"DARKNESS","20%","30%","Rank 2 to 3","Prevention chance","125 BP","325 BP");
VIEW(v_stiffness,"STIFFNESS","55%","60%","Lock resistance","Slow 73% to 76%","100 BP","350 BP");
VIEW(v_fortitude,"FORTITUDE","0","1","Protection charge","HP effects only","200 BP","250 BP");
VIEW(v_settings,"PROTECTION MODE",NULL,NULL,"Choose protection","No BP cost",NULL,NULL);
VIEW(v_heal,"HEAL","+15%","+30%","Healing bonus","All learned Heals","350 BP","100 BP");
VIEW(v_haste,"HASTE","+25%","+50%","Duration bonus","Cost unchanged","250 BP","200 BP");
VIEW(v_slow,"SLOW","50%","60%","Success chance","Neutral enemies only","100 BP","350 BP");
VIEW(v_attack,"ATTACK POWER","+20%",NULL,"Damage bonus","Maximum rank",NULL,NULL);
VIEW(v_scan,"SCAN","1 free","All free","Rank 2 to 3","Provisional path","150 BP","300 BP");
VIEW(v_preraise,"PRERAISE",NULL,NULL,"Next rank","Need 150 more BP","600 BP","Not enough");
VIEW(v_locked,"UNLEARNED",NULL,NULL,"Learn this power","Name stays hidden",NULL,NULL);
VIEW(v_medicine,"MEDICINE 2","0","1","Receive one item","Needs one free slot","30 Junk","170 Junk");
VIEW(v_tool,"TOOL","0","1","Receive one Tool","Needs one free slot","80 Junk","120 Junk");
VIEW(v_rebuild,"REBUILD M92F",NULL,NULL,"Retained +bonuses","Effects not restored","150 Junk","50 Junk");

LINES(refund_scope, {"100% of recorded refundable BP is returned."}, {"Weapons and armor are never refunded."}, {"Aya stats require Reset all eligible."}, {"Preview only. No BP or Junk changes."});
LINES(refund_aya, {"Aya's two stats are included only in a full reset."}, {"Use Reset all eligible, not a single upgrade reset."}, {"Weapons and armor are excluded."});
LINES(refund_poison_new, {"Poison Rank 3 to 0. Return 300 BP."}, {"BP 120 to 420. Junk 200 to 135."}, {"100% of refundable spend returned."}, {"Weapons and armor are excluded."});
LINES(refund_darkness_new, {"Darkness Rank 2 to 0. Return 200 BP."}, {"BP 120 to 320. Junk 200 to 140."}, {"100% returned. Weapons and armor excluded."});
LINES(refund_stiff_new, {"Stiffness Rank 1 to 0. Return 100 BP."}, {"BP 120 to 220. Junk 200 to 145."}, {"100% returned. Weapons and armor excluded."});
LINES(refund_heal_new, {"Heal Rank 2 to 0. Return 250 BP."}, {"BP 120 to 370. Junk 200 to 135."}, {"100% returned. Weapons and armor excluded."});
LINES(refund_haste_new, {"Haste Rank 1 to 0. Return 150 BP."}, {"BP 120 to 270. Junk 200 to 140."}, {"100% returned. Weapons and armor excluded."});
LINES(refund_breakdown, {"Aya stats: 200 BP. Resistance: 600 BP."}, {"PE Mastery: 400 BP. Total: 1200 BP."}, {"BP 120 to 1320. Cost 260 Junk."}, {"Need 60 more Junk. Nothing is charged."}, {"Weapons and armor are excluded."}, {"Next full-reset base: 300 Junk plus refund fee."});
VIEW(v_refund_poison_new,"RESET POISON","Rank 3","Rank 0","Return 300 BP","BP 120 to 420","65 Junk","135 Junk");
VIEW(v_refund_darkness_new,"RESET DARKNESS","Rank 2","Rank 0","Return 200 BP","BP 120 to 320","60 Junk","140 Junk");
VIEW(v_refund_stiff_new,"RESET STIFFNESS","Rank 1","Rank 0","Return 100 BP","BP 120 to 220","55 Junk","145 Junk");
VIEW(v_refund_heal_new,"RESET HEAL","Rank 2","Rank 0","Return 250 BP","BP 120 to 370","65 Junk","135 Junk");
VIEW(v_refund_haste_new,"RESET HASTE","Rank 1","Rank 0","Return 150 BP","BP 120 to 270","60 Junk","140 Junk");
VIEW(v_refund_empty,"NO INVESTMENT",NULL,NULL,"Return 0 BP","No fee charged",NULL,NULL);
VIEW(v_refund_aya,"FULL RESET ONLY",NULL,NULL,"Included in full reset","Select Reset all",NULL,NULL);
VIEW(v_refund_summary,"RESET ALL ELIGIBLE",NULL,NULL,"Return 1200 BP","BP 120 to 1320","260 Junk","Need 60 Junk");

static const BpUiRow gun_rows[] = {
    SECTION("guns.heading","WEAPONS",""),
    ROWV("gun.m92f","M92F","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"gun.m92f",NULL,NULL,weapon_help,&v_m92f),
    ROWV("gun.m84f","M84F","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"gun.m84f",NULL,NULL,weapon_help,&v_m84f)
};
static const BpUiRow m92f_rows[] = {
    SECTION("gun.stats","UPGRADE / BP",""),
    ROWV("m92f.attack","Attack","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.m92f.attack","Raise M92F Attack?",NULL,stat_help,&v_gun_attack),
    ROWV("m92f.range","Range","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.m92f.range","Raise M92F Range?",NULL,stat_help,&v_gun_range),
    ROWV("m92f.capacity","Capacity","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.m92f.capacity","Raise M92F Capacity?",NULL,stat_help,&v_gun_capacity)
};
static const BpUiRow m84f_rows[] = {
    SECTION("gun.stats","UPGRADE / BP",""),
    ROWV("m84f.attack","Attack","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.m84f.attack","Raise M84F Attack?",NULL,stat_help,&v_m84_attack),
    ROWV("m84f.range","Range","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.m84f.range","Raise M84F Range?",NULL,stat_help,&v_m84_range),
    ROWV("m84f.capacity","Capacity","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.m84f.capacity","Raise M84F Capacity?",NULL,stat_help,&v_m84_capacity)
};
static const BpUiRow armor_rows[] = {
    SECTION("armor.heading","ARMOR",""),
    ROWV("armor.nvest","N. Vest","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"armor.nvest",NULL,NULL,armor_help,&v_nvest)
};
static const BpUiRow nvest_rows[] = {
    SECTION("armor.stats","UPGRADE / BP",""),
    ROWV("nvest.defense","Defense","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.nvest.defense","Raise N. Vest Defense?",NULL,stat_help,&v_defense),
    ROWV("nvest.pe","P. Energy","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.nvest.pe","Raise P. Energy?",NULL,stat_help,&v_pe_defense),
    ROWV("nvest.critical","Critical","100",BP_UI_AVAILABLE,BP_UI_ASK,"buy.nvest.critical","Raise Critical?",NULL,stat_help,&v_critical)
};
static const BpUiRow aya_rows[] = {
    SECTION("aya.character","CHARACTER",""),
    ROWV("aya.active_time","Active Time","",BP_UI_AVAILABLE,BP_UI_ASK,"buy.aya.active","Raise Active Time?",NULL,stat_help,&v_active),
    ROWV("aya.capacity","Item Capacity","",BP_UI_AVAILABLE,BP_UI_ASK,"buy.aya.capacity","Raise Item Capacity?",NULL,stat_help,&v_capacity),
    SECTION("aya.resistance","RESISTANCE","RANK"),
    ROWV("resist.poison","Poison","1/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.poison","Raise Poison to Rank 2?",NULL,poison,&v_poison),
    ROWV("resist.confusion","Confusion","0/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.confusion","Raise Confusion to Rank 1?",NULL,confusion,&v_confusion),
    ROWV("resist.darkness","Darkness","2/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.darkness","Raise Darkness to Rank 3?",NULL,darkness,&v_darkness),
    ROWV("resist.stiffness","Stiffness","1/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.stiffness","Raise Stiffness to Rank 2?",NULL,stiffness,&v_stiffness),
    ROWV("resist.fortitude","Fortitude","0/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.fortitude","Raise Fortitude to Rank 1?",NULL,fortitude,&v_fortitude),
    ROWV("fortitude.settings","Mode","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"fortitude.settings",NULL,NULL,settings,&v_settings),
    ROW("aya.help","Help","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"details",NULL,NULL,details)
};
static const BpUiRow mastery_rows[] = {
    ROWV("mastery.heal","Heal","2/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.heal","Raise Heal to Rank 3?",NULL,heal,&v_heal),
    ROWV("mastery.haste","Haste","1/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.haste","Raise Haste to Rank 2?",NULL,haste,&v_haste),
    ROWV("mastery.slow","Slow","0/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.slow","Raise Slow to Rank 1?",NULL,slow,&v_slow),
    ROWV("mastery.attack","Attack","MAX",BP_UI_MAXED,BP_UI_NONE,NULL,NULL,"Maximum rank",attack,&v_attack),
    ROWV("mastery.scan","Scan","2/3",BP_UI_AVAILABLE,BP_UI_ASK,"buy.scan","Raise Scan to Rank 3?",NULL,scan,&v_scan),
    ROWV("mastery.preraise","Preraise","--",BP_UI_UNAFFORDABLE,BP_UI_NONE,NULL,NULL,"Need 150 more BP",preraise,&v_preraise),
    ROWV("mastery.unknown","???","Locked",BP_UI_LOCKED,BP_UI_NONE,NULL,NULL,"Power not learned",locked,&v_locked),
    ROW("mastery.respec","Respec","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"respec",NULL,NULL,stage)
};
static const BpUiRow workshop_rows[] = {
    ROW("service.requisition","Requisition","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"requisition",NULL,NULL,requisition),
    ROW("service.rebuild","Rebuild","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"rebuild",NULL,NULL,rebuild),
    ROW("service.bp_reset","Reset BP","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"reset",NULL,NULL,reset)
};
static const BpUiRow requisition_rows[] = {
    ROWV("good.medicine","Medicine 2","30 Junk",BP_UI_AVAILABLE,BP_UI_ASK,"buy.medicine","Buy Medicine 2?",NULL,supplies,&v_medicine),
    ROWV("good.tool","Tool","80 Junk",BP_UI_AVAILABLE,BP_UI_ASK,"buy.tool","Buy one Tool?",NULL,tool,&v_tool)
};
static const BpUiRow rebuild_rows[] = {
    ROWV("gear.m92f.001","M92F +12","150 Junk",BP_UI_AVAILABLE,BP_UI_ASK,"rebuild.m92f.001","Rebuild this M92F?",NULL,gear,&v_rebuild)
};
static const BpUiRow reset_rows[] = {
    ROW("reset.one","Reset one upgrade","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"reset.one",NULL,NULL,refund_scope),
    ROW("reset.all","Reset all eligible","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"reset.all",NULL,NULL,refund_breakdown)
};
static const BpUiRow reset_one_rows[] = {
    SECTION("refund.character","CHARACTER",""),
    ROWV("refund.active","Active Time","Full reset",BP_UI_BLOCKED,BP_UI_NONE,NULL,NULL,"Full reset only",refund_aya,&v_refund_aya),
    ROWV("refund.capacity","Item Capacity","Full reset",BP_UI_BLOCKED,BP_UI_NONE,NULL,NULL,"Full reset only",refund_aya,&v_refund_aya),
    SECTION("refund.resistance","RESISTANCE","BP BACK"),
    ROWV("refund.poison","Poison","300",BP_UI_AVAILABLE,BP_UI_ASK,"refund.poison","Reset Poison?",NULL,refund_poison_new,&v_refund_poison_new),
    ROWV("refund.confusion","Confusion","0",BP_UI_BLOCKED,BP_UI_NONE,NULL,NULL,"No BP to refund",refund_scope,&v_refund_empty),
    ROWV("refund.darkness","Darkness","200",BP_UI_AVAILABLE,BP_UI_ASK,"refund.darkness","Reset Darkness?",NULL,refund_darkness_new,&v_refund_darkness_new),
    ROWV("refund.stiffness","Stiffness","100",BP_UI_AVAILABLE,BP_UI_ASK,"refund.stiffness","Reset Stiffness?",NULL,refund_stiff_new,&v_refund_stiff_new),
    ROWV("refund.fortitude","Fortitude","0",BP_UI_BLOCKED,BP_UI_NONE,NULL,NULL,"No BP to refund",refund_scope,&v_refund_empty),
    ROW("refund.all.open","Reset all eligible","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"reset.all",NULL,NULL,refund_breakdown)
};
static const BpUiRow reset_mastery_rows[] = {
    SECTION("refund.powers","PE MASTERY","BP BACK"),
    ROWV("refund.heal","Heal","250",BP_UI_AVAILABLE,BP_UI_ASK,"refund.heal","Reset Heal mastery?",NULL,refund_heal_new,&v_refund_heal_new),
    ROWV("refund.haste","Haste","150",BP_UI_AVAILABLE,BP_UI_ASK,"refund.haste","Reset Haste mastery?",NULL,refund_haste_new,&v_refund_haste_new),
    ROWV("refund.slow","Slow","0",BP_UI_BLOCKED,BP_UI_NONE,NULL,NULL,"No BP to refund",refund_scope,&v_refund_empty),
    ROWV("refund.scan","Scan","0",BP_UI_BLOCKED,BP_UI_NONE,NULL,NULL,"No BP to refund",refund_scope,&v_refund_empty),
    ROW("refund.all.open","Reset all eligible","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"reset.all",NULL,NULL,refund_breakdown)
};
static const BpUiRow reset_all_rows[] = {
    SECTION("refund.all.heading","REFUND SCOPE","BP BACK"),
    ROW("refund.all.aya","Aya stats","200",BP_UI_AVAILABLE,BP_UI_NONE,NULL,NULL,NULL,refund_aya),
    ROW("refund.all.resistance","Resistance","600",BP_UI_AVAILABLE,BP_UI_NONE,NULL,NULL,NULL,refund_scope),
    ROW("refund.all.mastery","PE Mastery","400",BP_UI_AVAILABLE,BP_UI_NONE,NULL,NULL,NULL,refund_scope),
    ROWV("refund.all.confirm","Reset all eligible","1200 BP",BP_UI_UNAFFORDABLE,BP_UI_NONE,NULL,NULL,"Need 60 more Junk",refund_breakdown,&v_refund_summary)
};
static const BpUiRow respec_rows[] = {
    ROW("stage.poison","Poison","3/3 to 0/3",BP_UI_AVAILABLE,BP_UI_STAGE,"stage.poison",NULL,NULL,stage),
    ROW("stage.heal","Heal","1/3 to 2/3",BP_UI_AVAILABLE,BP_UI_STAGE,"stage.heal",NULL,NULL,stage),
    ROW("stage.review","Review changes","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"review",NULL,NULL,review),
    ROW("stage.discard","Discard changes","Preview",BP_UI_AVAILABLE,BP_UI_ASK,"stage.discard","Discard this build?",NULL,discard)
};
static const BpUiRow review_rows[] = {
    ROW("review.apply","Apply this build","Preview only",BP_UI_AVAILABLE,BP_UI_ASK,"respec.apply","Apply this preview build?",NULL,review)
};
static const BpUiRow preset_rows[] = {
    ROW("preset.vanilla_qol","Vanilla + QoL","Preview",BP_UI_AVAILABLE,BP_UI_ASK,"preset.vanilla_qol","Apply Vanilla + QoL rules?",NULL,vanilla),
    ROW("preset.enhanced","Enhanced","Current",BP_UI_AVAILABLE,BP_UI_ASK,"preset.enhanced","Apply Enhanced BP rules?",NULL,enhanced),
    ROW("preset.resistance","Resistance Only","Preview",BP_UI_AVAILABLE,BP_UI_ASK,"preset.resistance","Apply Resistance Only rules?",NULL,resistance_only),
    ROW("preset.mastery","Mastery Only","Preview",BP_UI_AVAILABLE,BP_UI_ASK,"preset.mastery","Apply Mastery Only rules?",NULL,mastery_only),
    ROW("preset.custom","Custom","Inspect",BP_UI_AVAILABLE,BP_UI_NONE,NULL,NULL,NULL,custom),
};
static const BpUiRow guide_rows[] = {
    ROW("guide.none","None","Advice off",BP_UI_AVAILABLE,BP_UI_SETTING,"guide.none",NULL,NULL,guide),
    ROW("guide.balanced","Balanced","Advice",BP_UI_AVAILABLE,BP_UI_SETTING,"guide.balanced",NULL,NULL,guide),
    ROW("guide.gun","Gun-focused","Advice",BP_UI_AVAILABLE,BP_UI_SETTING,"guide.gun",NULL,NULL,guide),
    ROW("guide.pe","PE-focused","Advice",BP_UI_AVAILABLE,BP_UI_SETTING,"guide.pe",NULL,NULL,guide),
    ROW("guide.survival","Survival-focused","Advice",BP_UI_AVAILABLE,BP_UI_SETTING,"guide.survival",NULL,NULL,guide)
};
static const BpUiRow setting_rows[] = {
    ROW("fortitude.both","Protect against both","Current",BP_UI_AVAILABLE,BP_UI_SETTING,"fortitude.both",NULL,NULL,settings),
    ROW("fortitude.hp1","Save for HP to 1","Preview",BP_UI_AVAILABLE,BP_UI_SETTING,"fortitude.hp1",NULL,NULL,settings)
};
static const BpUiRow detail_rows[] = {
    ROW("details.help","About this prototype","Read only",BP_UI_AVAILABLE,BP_UI_NONE,NULL,NULL,NULL,details)
};

static const BpUiRow preview_rows[] = {
    ROW("preview.bp","BP upgrades","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"guns",NULL,NULL,details),
    ROW("preview.wayne","Workshop","Open",BP_UI_AVAILABLE,BP_UI_ROUTE,"workshop",NULL,NULL,requisition)
};

#define PAGE(id,title,context,wallet,prev,next,rows) {id,title,context,wallet,prev,next,rows,COUNT(rows),NULL,NULL}
#define TABPAGE(id,title,subject,tab,rows) {id,title,"PREVIEW ONLY","BP 450",NULL,NULL,rows,COUNT(rows),tab,subject}
static const BpUiPage pages[] = {
    TABPAGE("guns","BONUS POINTS / GUNS","Choose a weapon","guns",gun_rows),
    TABPAGE("gun.m92f","BONUS POINTS / GUNS","M92F","guns",m92f_rows),
    TABPAGE("gun.m84f","BONUS POINTS / GUNS","M84F","guns",m84f_rows),
    TABPAGE("armor","BONUS POINTS / ARMOR","Choose armor","armor",armor_rows),
    TABPAGE("armor.nvest","BONUS POINTS / ARMOR","N. Vest","armor",nvest_rows),
    TABPAGE("aya","BONUS POINTS / AYA","Character and resistance","aya",aya_rows),
    TABPAGE("mastery","BONUS POINTS / PE MASTERY","Choose a power","mastery",mastery_rows),
    PAGE("workshop","WAYNE'S WORKSHOP","PREVIEW ONLY","Junk 200",NULL,NULL,workshop_rows),
    PAGE("requisition","WORKSHOP / REQUISITION","PREVIEW ONLY","Junk 200",NULL,NULL,requisition_rows),
    PAGE("rebuild","WORKSHOP / REBUILD","PREVIEW ONLY","Junk 200",NULL,NULL,rebuild_rows),
    PAGE("reset","WORKSHOP / RESET","Equipment BP excluded","Junk 200",NULL,NULL,reset_rows),
    {"reset.one","WORKSHOP / BP REFUND","Weapon/armor BP excluded","Junk 200",NULL,NULL,reset_one_rows,COUNT(reset_one_rows),"refund.aya","Weapon/armor BP excluded"},
    {"reset.mastery","WORKSHOP / BP REFUND","Weapon/armor BP excluded","Junk 200",NULL,NULL,reset_mastery_rows,COUNT(reset_mastery_rows),"refund.mastery","Weapon/armor BP excluded"},
    PAGE("reset.all","RESET ALL ELIGIBLE","Weapon/armor BP excluded","Junk 200",NULL,NULL,reset_all_rows),
    PAGE("respec","RESPEC PREVIEW","Editing - not applied","BP 220",NULL,NULL,respec_rows),
    PAGE("review","REVIEW CHANGES","Not applied","BP 220",NULL,NULL,review_rows),
    PAGE("fortitude.settings","FORTITUDE MODE","PREVIEW ONLY","No BP cost",NULL,NULL,setting_rows),
    PAGE("details","HELP","PREVIEW ONLY","No spending",NULL,NULL,detail_rows),
    PAGE("preview","UI PREVIEW","PREVIEW ONLY","Sample data",NULL,NULL,preview_rows)
};
static const BpUiTab tabs[] = {
    {"guns","GUNS","guns",NULL}, {"armor","ARMOR","armor",NULL},
    {"aya","AYA","aya",NULL}, {"mastery","PE MASTERY","mastery",NULL},
    {"refund.aya","AYA","reset.one","refund"},
    {"refund.mastery","PE MASTERY","reset.mastery","refund"}
};
static const BpUiCatalog demo = { pages, COUNT(pages), tabs, COUNT(tabs), NULL };
const BpUiCatalog *bp_ui_demo_catalog(void) { return &demo; }

/* Settings definitions are owned by F1, outside the native menu catalog. */
static const BpUiPage f1_pages[] = {
    PAGE("presets","BP presets","PREVIEW ONLY","BP rules: Enhanced",NULL,NULL,preset_rows),
    PAGE("guides","Playstyle guidance","Advice only","No automatic purchases",NULL,NULL,guide_rows)
};
static const BpUiCatalog f1_demo = { f1_pages, COUNT(f1_pages), NULL, 0, NULL };
const BpUiCatalog *bp_ui_demo_f1_catalog(void) { return &f1_demo; }
