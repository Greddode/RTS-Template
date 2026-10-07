// inspector.c - The panel at the bottom of the screen.
//
// Shows whatever is selected (asked from input.c, which stores everything as
// (slot, serial), so nothing here can point at a dead unit or building):
//   one unit        type, health bar, damage / range / speed, current order
//   several units   an icon and count per type, total health
//   a building      name, health, production queue (click an icon to cancel
//                   and get the gold back), progress, and Train buttons
//   a gold node     gold left
// Selected workers also get a Build section.
//
// The Train and Build buttons are generated from UNIT_STATS (trainedAt,
// hotkey) and BUILDING_STATS (cost, hotkey), so a new unit or building type
// gets its button by adding a table row - no UI code.
//
// The button lists sit in a scroll area (ui.c): buttons that don't fit are
// clipped at the panel edge and reachable with the mouse wheel.
//
// When nothing is selected, InspectorDraw() returns straight away.

#include "inspector.h"
#include "buildings.h"
#include "config.h"
#include "economy.h"
#include "input.h"
#include "minimap.h"
#include "ui.h"
#include "units.h"

#define PANEL_W      760.0f   // reference sizes (720 px tall window), scaled by Ui()
#define PANEL_H      150.0f
#define PAD          12.0f
#define INFO_W       330.0f   // left part: info; right part: buttons
#define BUTTON_W     190.0f
#define BUTTON_H     34.0f
#define TEXT         18.0f
#define SMALL        16.0f
#define ICON         10.0f    // icon radius
#define BAR_H        12.0f
#define BUTTON_GAP   8.0f
#define SCROLLBAR    10.0f    // room kept for the scrollbar on the right

static float buttonScroll = 0.0f;          // scroll position of the button list
static unsigned int scrollOwner = 0;       // what it belongs to: reset when the selection changes

// Bottom of the screen, centred in the space right of the minimap.
static Rectangle Panel(void)
{
    float left = MINIMAP_ENABLED ? MinimapRect().x + MinimapRect().width + Ui(10.0f) : Ui(10.0f);
    float space = GetScreenWidth() - left - Ui(10.0f);
    float w = Ui(PANEL_W);
    if (w > space) w = space;
    return (Rectangle){ left + (space - w)*0.5f, GetScreenHeight() - Ui(PANEL_H) - Ui(10.0f), w, Ui(PANEL_H) };
}

static void Bar(float x, float y, float w, float frac, Color color)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    DrawRectangleRec((Rectangle){ x, y, w, Ui(BAR_H) }, DARKGRAY);
    DrawRectangleRec((Rectangle){ x, y, w*frac, Ui(BAR_H) }, color);
}

static Color HealthColor(float frac)
{
    return (frac > 0.5f) ? GREEN : (frac > 0.25f) ? ORANGE : RED;
}

static const char *OrderText(const Unit *u)
{
    if (u->attacking)    return u->attackTargetIsBuilding ? "Attacking building" : "Attacking";
    if (u->buildOrder)   return "Constructing";
    switch (u->gatherState)
    {
        case GATHER_TO_NODE: return "Going to mine";
        case GATHER_MINING:  return "Mining";
        case GATHER_TO_BASE: return "Carrying gold";
        default: break;
    }
    if (u->moving)       return u->attackMove ? "Attack-moving" : "Moving";
    if (u->holdPosition) return "Holding position";
    return "Idle";
}

// --- Button list: a grid in the right part of the panel, inside a scroll area ---
static Rectangle ButtonArea(Rectangle panel)
{
    float left = panel.x + Ui(INFO_W) + Ui(PAD);
    float top = panel.y + Ui(34.0f);
    return (Rectangle){ left, top, panel.x + panel.width - Ui(PAD) - left, panel.y + panel.height - Ui(PAD) - top };
}

static int ButtonColumns(Rectangle area)
{
    int cols = (int)((area.width - Ui(SCROLLBAR) + Ui(BUTTON_GAP))/(Ui(BUTTON_W) + Ui(BUTTON_GAP)));
    return (cols < 1) ? 1 : cols;
}

// Start the scroll area for `count` buttons; returns the y offset for ButtonSlot.
static float ButtonsBegin(Rectangle area, int count, unsigned int owner)
{
    if (owner != scrollOwner) { scrollOwner = owner; buttonScroll = 0.0f; }   // new selection: back to the top
    int rows = (count + ButtonColumns(area) - 1)/ButtonColumns(area);
    float contentH = rows*(Ui(BUTTON_H) + Ui(BUTTON_GAP)) - Ui(BUTTON_GAP);
    return UiScrollBegin(area, contentH, &buttonScroll);
}

static Rectangle ButtonSlot(Rectangle area, int k, float offsetY)
{
    int cols = ButtonColumns(area);
    int col = k % cols, row = k / cols;
    return (Rectangle){ area.x + col*(Ui(BUTTON_W) + Ui(BUTTON_GAP)), area.y + offsetY + row*(Ui(BUTTON_H) + Ui(BUTTON_GAP)), Ui(BUTTON_W), Ui(BUTTON_H) };
}

static void SectionTitle(Rectangle panel, const char *title)
{
    UiLabel(title, panel.x + Ui(INFO_W) + Ui(PAD), panel.y + Ui(PAD), Ui(SMALL), LIGHTGRAY);
}

// --- One unit ---------------------------------------------------------------------
static void DrawOneUnit(Rectangle panel, const Unit *u)
{
    const UnitStats *s = &UNIT_STATS[u->type];
    float x = panel.x + Ui(PAD), y = panel.y + Ui(PAD);
    UnitsDrawIcon(u->type, u->team, (Vector2){ x + Ui(ICON*1.6f), y + Ui(ICON*1.6f) }, Ui(ICON*1.6f));
    UiLabel(s->name, x + Ui(44.0f), y + Ui(4.0f), Ui(22.0f), RAYWHITE);

    y += Ui(44.0f);
    Bar(x, y, Ui(200.0f), u->hp/s->hp, HealthColor(u->hp/s->hp));
    UiLabel(TextFormat("%d / %d", (int)u->hp, (int)s->hp), x + Ui(210.0f), y - Ui(2.0f), Ui(SMALL), RAYWHITE);
    y += Ui(22.0f);
    UiLabel(TextFormat("Damage %d   Range %d   Speed %d", (int)s->damage, (int)s->range, (int)s->speed), x, y, Ui(SMALL), RAYWHITE);
    y += Ui(22.0f);
    UiLabel(TextFormat("Order: %s", OrderText(u)), x, y, Ui(SMALL), GOLD);
}

// --- Several units ------------------------------------------------------------------
static void DrawManyUnits(Rectangle panel, const int *ids, int count)
{
    int perType[UNIT_TYPE_COUNT] = { 0 };
    float hp = 0.0f, maxHp = 0.0f;
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[ids[k]];
        perType[u->type]++;
        hp += u->hp;
        maxHp += UNIT_STATS[u->type].hp;
    }

    float x = panel.x + Ui(PAD), y = panel.y + Ui(PAD);
    UiLabel(TextFormat("%d units", count), x, y, Ui(22.0f), RAYWHITE);
    y += Ui(36.0f);
    float cx = x;
    for (int t = 0; t < UNIT_TYPE_COUNT; t++)
    {
        if (perType[t] == 0) continue;
        const char *label = TextFormat("%s x%d", UNIT_STATS[t].name, perType[t]);
        UnitsDrawIcon((UnitType)t, PLAYER_TEAM, (Vector2){ cx + Ui(ICON), y + Ui(ICON) }, Ui(ICON));
        UiLabel(label, cx + Ui(ICON*2.0f + 6.0f), y + Ui(2.0f), Ui(SMALL), RAYWHITE);
        cx += Ui(ICON*2.0f + 18.0f) + MeasureText(label, (int)Ui(SMALL));   // next entry after this text
    }
    y += Ui(34.0f);
    Bar(x, y, Ui(200.0f), hp/maxHp, HealthColor(hp/maxHp));
    UiLabel(TextFormat("Total HP %d / %d", (int)hp, (int)maxHp), x + Ui(210.0f), y - Ui(2.0f), Ui(SMALL), RAYWHITE);
}

// Build buttons, one per building type workers can build (cost + hotkey in BUILDING_STATS).
static bool Buildable(int t)
{
    return BUILDING_STATS[t].cost > 0 && BUILDING_STATS[t].hotkey != 0;
}

static void DrawBuildButtons(Rectangle panel)
{
    SectionTitle(panel, "Build");
    int count = 0;
    for (int t = 0; t < BUILDING_TYPE_COUNT; t++) count += Buildable(t);

    Rectangle area = ButtonArea(panel);
    float offset = ButtonsBegin(area, count, 1);   // owner 1 = "workers' build list"
    int k = 0;
    for (int t = 0; t < BUILDING_TYPE_COUNT; t++)
    {
        const BuildingStats *s = &BUILDING_STATS[t];
        if (!Buildable(t)) continue;
        bool affordable = EconomyGold(PLAYER_TEAM) >= s->cost;
        const char *label = InputIsPlacing((BuildingType)t) ? TextFormat("Placing %s...", s->name)
                                                            : TextFormat("%s  %dg  [%s]", s->name, s->cost, UiKeyName(s->hotkey));
        if (UiButtonEx(ButtonSlot(area, k++, offset), label, s->hotkey, !affordable))
        {
            if (affordable || InputIsPlacing((BuildingType)t)) InputTogglePlacement((BuildingType)t);
            else UiShowMessage("Not enough gold");
        }
    }
    UiScrollEnd();
}

// --- A building ---------------------------------------------------------------------
static void DrawBuilding(Rectangle panel, int id)
{
    const Building *b = &buildings[id];
    const BuildingStats *s = &BUILDING_STATS[b->type];
    float x = panel.x + Ui(PAD), y = panel.y + Ui(PAD);

    UiLabel(s->name, x, y, Ui(22.0f), RAYWHITE);
    y += Ui(32.0f);
    Bar(x, y, Ui(200.0f), b->hp/s->hp, HealthColor(b->hp/s->hp));
    UiLabel(TextFormat("%d / %d", (int)b->hp, (int)s->hp), x + Ui(210.0f), y - Ui(2.0f), Ui(SMALL), RAYWHITE);
    y += Ui(26.0f);

    if (b->constructing)
    {
        UiLabel(TextFormat("Under construction  %d%%", (int)(BuildingBuildProgress(id)*100.0f)), x, y, Ui(SMALL), ORANGE);
        Bar(x, y + Ui(22.0f), Ui(300.0f), BuildingBuildProgress(id), ORANGE);
        return;   // no production until it's finished
    }

    // Queue: one small button per queued unit; click to cancel (refunded).
    UiLabel(TextFormat("Queue %d/%d", b->queueCount, MAX_QUEUE), x, y, Ui(SMALL), LIGHTGRAY);
    y += Ui(20.0f);
    int cancel = -1;
    for (int q = 0; q < b->queueCount; q++)
    {
        Rectangle slot = { x + q*Ui(36.0f), y, Ui(32.0f), Ui(32.0f) };
        if (UiButton(slot, "", 0)) cancel = q;
        UnitsDrawIcon(b->queue[q], b->team, (Vector2){ slot.x + slot.width*0.5f, slot.y + slot.height*0.5f }, Ui(ICON));
    }
    if (b->queueCount > 0)
    {
        float frac = b->trainTicks/(UNIT_STATS[b->queue[0]].trainTime*TICK_RATE);
        Bar(x, y + Ui(36.0f), Ui(32.0f)*MAX_QUEUE + Ui(16.0f), frac, SKYBLUE);
    }
    if (cancel != -1) BuildingCancelQueued(id, cancel);

    // Train buttons: every unit type whose trainedAt is this building type.
    SectionTitle(panel, "Train");
    int count = 0;
    for (int t = 0; t < UNIT_TYPE_COUNT; t++) count += (UNIT_STATS[t].trainedAt == b->type);

    Rectangle area = ButtonArea(panel);
    float offset = ButtonsBegin(area, count, b->serial + 1);   // owner = this building (+1 keeps it apart from the build list)
    int k = 0;
    for (int t = 0; t < UNIT_TYPE_COUNT; t++)
    {
        const UnitStats *u = &UNIT_STATS[t];
        if (u->trainedAt != b->type) continue;
        bool affordable = EconomyGold(b->team) >= u->cost;
        const char *label = TextFormat("%s  %dg  [%s]", u->name, u->cost, UiKeyName(u->hotkey));
        if (UiButtonEx(ButtonSlot(area, k++, offset), label, u->hotkey, !affordable))
        {
            if (b->queueCount >= MAX_QUEUE) UiShowMessage("Queue full");
            else if (!BuildingQueueTrain(id, (UnitType)t)) UiShowMessage("Not enough gold");
        }
    }
    UiScrollEnd();
}

// --- A gold node ----------------------------------------------------------------------
static void DrawNode(Rectangle panel, int id)
{
    float x = panel.x + Ui(PAD), y = panel.y + Ui(PAD);
    UiLabel("Gold node", x, y, Ui(22.0f), GOLD);
    y += Ui(36.0f);
    float frac = goldNodes[id].amount/(float)GOLD_NODE_AMOUNT;
    Bar(x, y, Ui(200.0f), frac, GOLD);
    UiLabel(TextFormat("%d gold left", goldNodes[id].amount), x + Ui(210.0f), y - Ui(2.0f), Ui(SMALL), RAYWHITE);
}

void InspectorDraw(void)
{
    if (!InputHasSelection()) return;   // nothing selected: no work at all

    const int *ids;
    int unitCount = InputSelectedUnits(&ids);
    int building = InputSelectedBuilding();
    int node = InputSelectedNode();
    if (unitCount == 0 && building == -1 && node == -1) return;   // everything selected has died

    Rectangle panel = Panel();
    UiPanel(panel);

    if (building != -1) DrawBuilding(panel, building);
    else if (node != -1) DrawNode(panel, node);
    else
    {
        if (unitCount == 1) DrawOneUnit(panel, &units[ids[0]]);
        else DrawManyUnits(panel, ids, unitCount);

        bool anyWorker = false;
        for (int k = 0; k < unitCount && !anyWorker; k++) anyWorker = (units[ids[k]].type == UNIT_WORKER);
        if (anyWorker) DrawBuildButtons(panel);
    }
}

// Collect every hotkey in use and warn about duplicates. Run once at startup,
// so a buyer adding a table row with a taken letter finds out immediately.
void InspectorCheckHotkeys(void)
{
    struct { int key; const char *what; } keys[64];
    int n = 0;
    keys[n].key = KEY_ATTACK_MOVE; keys[n++].what = "attack-move";
    keys[n].key = KEY_STOP;        keys[n++].what = "stop";
    keys[n].key = KEY_HOLD;        keys[n++].what = "hold";
    keys[n].key = KEY_PAUSE;       keys[n++].what = "pause";
    keys[n].key = KEY_DEBUG_WAVE;  keys[n++].what = "debug wave";
    keys[n].key = KEY_EDITOR;      keys[n++].what = "map editor";
    for (int t = 0; t < UNIT_TYPE_COUNT && n < 64; t++)
    {
        if (UNIT_STATS[t].trainedAt != BUILDING_NONE && UNIT_STATS[t].hotkey) { keys[n].key = UNIT_STATS[t].hotkey; keys[n++].what = UNIT_STATS[t].name; }
    }
    for (int t = 0; t < BUILDING_TYPE_COUNT && n < 64; t++)
    {
        if (BUILDING_STATS[t].cost > 0 && BUILDING_STATS[t].hotkey) { keys[n].key = BUILDING_STATS[t].hotkey; keys[n++].what = BUILDING_STATS[t].name; }
    }

    for (int i = 0; i < n; i++)
    {
        for (int j = i + 1; j < n; j++)
        {
            if (keys[i].key == keys[j].key)
                TraceLog(LOG_WARNING, "HOTKEY CONFLICT: %s and %s both use %s", keys[i].what, keys[j].what, UiKeyName(keys[i].key));
        }
    }
}
