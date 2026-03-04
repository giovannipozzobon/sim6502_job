/*
 * sim6502-gui — Phase 2
 *
 * Main entry point for the GUI binary.  Integrates with the simulator core
 * via the sim_api.h library interface (sim_session_t *).
 *
 * Phase 1 features:
 *   - Toolbar: Load, Step, Run/Pause, Reset, Processor selector
 *   - Status bar: state / PC+registers / filename
 *   - Default docking layout: Registers | Disassembly / Memory | Console
 *   - Keyboard shortcuts: F5 run, F6/Esc pause, F7 step, Ctrl+R reset, Ctrl+L load
 *   - Persistent font size, window position and size
 *
 * Phase 2 additions:
 *   - Register pane: compact/expanded modes, diff highlighting, click-to-edit
 *   - Disassembly pane: table layout, clickable BP gutter, cycle counts, symbols
 *   - Memory pane: last-write byte highlighting, follow-PC/SP buttons
 *   - Console pane: scrollable log, command history, full CLI interpreter
 *
 * Build: make gui
 * Dependencies: SDL2, OpenGL 3.3, Dear ImGui (docking branch)
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "cpu.h"
#include "sim_api.h"
}

/* --------------------------------------------------------------------------
 * Core globals
 * -------------------------------------------------------------------------- */
static sim_session_t *g_sim     = NULL;
static bool  g_running          = false;  /* true = run mode each frame */
static int   g_speed            = 5000;   /* instructions per frame */

/* Font / DPI state */
static float g_ui_scale         = 1.0f;
static int   g_base_font_size   = 13;
static bool  g_font_rebuild     = false;

/* SDL window handle — used by INI write callback */
static SDL_Window *g_window = nullptr;

/* Toolbar state */
static char g_filename_buf[512] = "";
static bool g_focus_filename    = false;

static const char *g_proc_labels[] = { "6502", "6502-undoc", "65C02", "65CE02", "45GS02" };
static const char *g_proc_ids[]    = { "6502", "6502-undoc", "65c02", "65ce02", "45gs02" };
static int  g_proc_idx             = 0;

/* Pane visibility */
static bool show_registers   = true;
static bool show_disassembly = true;
static bool show_memory      = true;
static bool show_console     = true;

/* --------------------------------------------------------------------------
 * Phase 2 state
 * -------------------------------------------------------------------------- */

/* Register diff — snapshot CPU before each sim_step call */
static cpu_t g_prev_cpu       = {};
static bool  g_prev_cpu_valid = false;

/* Registers pane */
static bool g_regs_expanded = false;

/* Disassembly view */
static uint16_t g_disasm_addr   = 0x0200;
static bool     g_disasm_follow = true;

/* Memory view — last-write log (populated after each sim_step) */
static uint16_t g_last_writes[256];
static int      g_last_write_count = 0;

/* ---- Phase 3 pane visibility ---- */
static bool show_breakpoints = false;
static bool show_trace       = false;
static bool show_stack       = false;
static bool show_watches     = false;

/* ---- Watch list (GUI-side only) ---- */
#define WATCH_MAX 16
struct WatchEntry {
    uint16_t addr;
    char     name[32];
    uint8_t  cur_val;
    uint8_t  prev_val;
    bool     changed;
    bool     valid;   /* false = not yet read */
};
static WatchEntry g_watches[WATCH_MAX];
static int        g_watch_count = 0;

/* Console */
#define CON_MAX_LINES   1024
#define CON_MAX_HISTORY   64

struct ConLine { char text[256]; ImVec4 color; };

static ConLine g_con_lines[CON_MAX_LINES];
static int     g_con_line_count    = 0;
static char    g_con_input[256]    = "";
static bool    g_con_scroll_bottom = true;
static int     g_con_history_pos   = -1;
static char    g_con_history_buf[CON_MAX_HISTORY][256];
static int     g_con_history_count = 0;

/* Console text colours */
static const ImVec4 CON_COL_NORMAL = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
static const ImVec4 CON_COL_CMD    = ImVec4(0.6f,  0.8f,  1.0f,  1.0f);
static const ImVec4 CON_COL_OK     = ImVec4(0.4f,  1.0f,  0.4f,  1.0f);
static const ImVec4 CON_COL_ERR    = ImVec4(1.0f,  0.4f,  0.4f,  1.0f);
static const ImVec4 CON_COL_WARN   = ImVec4(1.0f,  0.85f, 0.3f,  1.0f);

/* --------------------------------------------------------------------------
 * Font rebuild
 * -------------------------------------------------------------------------- */
static void rebuild_font(void)
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig font_cfg;
    font_cfg.SizePixels = floorf((float)g_base_font_size * g_ui_scale);
    io.Fonts->AddFontDefault(&font_cfg);
    io.Fonts->Build();
}

/* --------------------------------------------------------------------------
 * UI scale detection
 * -------------------------------------------------------------------------- */
static float detect_ui_scale(int display_index)
{
    const char *env = SDL_getenv("SIM6502_SCALE");
    if (env) { float v = (float)SDL_atof(env); if (v >= 0.5f && v <= 8.0f) return v; }

    const char *gdk = SDL_getenv("GDK_SCALE");
    if (gdk) { float v = (float)SDL_atof(gdk); if (v >= 1.0f && v <= 8.0f) return v; }

    const char *qt = SDL_getenv("QT_SCALE_FACTOR");
    if (qt)  { float v = (float)SDL_atof(qt);  if (v >= 1.0f && v <= 8.0f) return v; }

    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    if (SDL_GetDisplayDPI(display_index, &ddpi, &hdpi, &vdpi) == 0
            && hdpi > 0.0f && hdpi != 96.0f)
        return floorf(hdpi / 96.0f * 4.0f + 0.5f) / 4.0f;

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(display_index, &mode) == 0) {
        if (mode.w >= 3840) return 2.00f;
        if (mode.w >= 2560) return 1.50f;
    }
    return 1.0f;
}

/* --------------------------------------------------------------------------
 * Default docking layout (applied once on first run)
 *
 *   ┌──────────┬──────────────────────┐
 *   │ Registers│                      │
 *   ├──────────┤    Disassembly       │
 *   │          │                      │
 *   │  Memory  ├──────────────────────┤
 *   │          │  Console             │
 *   └──────────┴──────────────────────┘
 * -------------------------------------------------------------------------- */
static void setup_default_layout(ImGuiID dockspace_id, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID left_col, right_col;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.28f, &left_col, &right_col);

    ImGuiID left_top, left_bot;
    ImGui::DockBuilderSplitNode(left_col, ImGuiDir_Up, 0.38f, &left_top, &left_bot);

    ImGuiID right_top, right_bot;
    ImGui::DockBuilderSplitNode(right_col, ImGuiDir_Up, 0.72f, &right_top, &right_bot);

    ImGui::DockBuilderDockWindow("Registers",   left_top);
    ImGui::DockBuilderDockWindow("Memory",      left_bot);
    ImGui::DockBuilderDockWindow("Disassembly", right_top);
    ImGui::DockBuilderDockWindow("Console",     right_bot);

    ImGui::DockBuilderFinish(dockspace_id);
}

/* --------------------------------------------------------------------------
 * do_load: assemble and load the file named in g_filename_buf
 * -------------------------------------------------------------------------- */
static void update_watches(void); /* forward declaration — defined before draw_toolbar */
static void do_load(void)
{
    if (g_filename_buf[0] == '\0') return;
    g_running = false;
    if (sim_load_asm(g_sim, g_filename_buf) != 0) {
        fprintf(stderr, "sim6502-gui: failed to load '%s'\n", g_filename_buf);
    } else {
        fprintf(stdout, "sim6502-gui: loaded '%s'\n", g_filename_buf);
        g_prev_cpu_valid   = false;
        g_last_write_count = 0;
        /* Re-seed watch values from the newly loaded program */
        for (int i = 0; i < g_watch_count; i++) g_watches[i].valid = false;
        update_watches();
    }
}

/* --------------------------------------------------------------------------
 * Console helpers
 * -------------------------------------------------------------------------- */
static void con_add(ImVec4 color, const char *fmt, ...)
{
    if (g_con_line_count >= CON_MAX_LINES) {
        memmove(g_con_lines, g_con_lines + 1,
                sizeof(g_con_lines[0]) * (CON_MAX_LINES - 1));
        g_con_line_count = CON_MAX_LINES - 1;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_con_lines[g_con_line_count].text,
              sizeof(g_con_lines[0].text), fmt, ap);
    va_end(ap);
    g_con_lines[g_con_line_count].color = color;
    g_con_line_count++;
    g_con_scroll_bottom = true;
}

/* InputText history callback */
static int con_input_cb(ImGuiInputTextCallbackData *data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        const int prev = g_con_history_pos;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (g_con_history_pos == -1)
                g_con_history_pos = g_con_history_count - 1;
            else if (g_con_history_pos > 0)
                g_con_history_pos--;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (g_con_history_pos != -1)
                if (++g_con_history_pos >= g_con_history_count)
                    g_con_history_pos = -1;
        }
        if (prev != g_con_history_pos) {
            const char *h = (g_con_history_pos >= 0)
                            ? g_con_history_buf[g_con_history_pos] : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, h);
        }
    }
    return 0;
}

/* Command interpreter */
static void con_exec(const char *cmd)
{
    con_add(CON_COL_CMD, "> %s", cmd);

    /* Add to history (deduplicate consecutive identical) */
    if (cmd[0] != '\0') {
        if (g_con_history_count == 0 ||
                strcmp(g_con_history_buf[g_con_history_count - 1], cmd) != 0) {
            if (g_con_history_count >= CON_MAX_HISTORY) {
                memmove(g_con_history_buf, g_con_history_buf + 1,
                        sizeof(g_con_history_buf[0]) * (CON_MAX_HISTORY - 1));
                g_con_history_count = CON_MAX_HISTORY - 1;
            }
            snprintf(g_con_history_buf[g_con_history_count],
                     sizeof(g_con_history_buf[0]), "%s", cmd);
            g_con_history_count++;
        }
    }
    g_con_history_pos = -1;

    /* Parse verb (first word) and args_rest (remainder of line) */
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *verb = buf;
    while (*verb == ' ' || *verb == '\t') verb++;
    char *verb_end = verb;
    while (*verb_end && *verb_end != ' ' && *verb_end != '\t') verb_end++;
    char *args_rest = verb_end;
    while (*args_rest == ' ' || *args_rest == '\t') args_rest++;
    if (*verb_end) *verb_end = '\0';
    if (verb[0] == '\0') return;

    /* ---- Commands ---- */

    if (strcmp(verb, "help") == 0) {
        con_add(CON_COL_NORMAL, "Commands:");
        con_add(CON_COL_NORMAL, "  step [n]              step n instructions (default 1)");
        con_add(CON_COL_NORMAL, "  run                   run continuously");
        con_add(CON_COL_NORMAL, "  pause                 pause execution");
        con_add(CON_COL_NORMAL, "  reset                 reset CPU to start address");
        con_add(CON_COL_NORMAL, "  regs                  print CPU registers");
        con_add(CON_COL_NORMAL, "  mem [addr] [n]        hex dump n bytes at addr");
        con_add(CON_COL_NORMAL, "  disasm [addr] [n]     disassemble n instructions");
        con_add(CON_COL_NORMAL, "  break <addr> [cond]   set breakpoint");
        con_add(CON_COL_NORMAL, "  del <addr>            remove breakpoint");
        con_add(CON_COL_NORMAL, "  breaks                list all breakpoints");
        con_add(CON_COL_NORMAL, "  load <path>           assemble and load .asm file");
        con_add(CON_COL_NORMAL, "  cls                   clear console output");
        return;
    }

    if (strcmp(verb, "cls") == 0) {
        g_con_line_count = 0;
        return;
    }

    if (strcmp(verb, "step") == 0) {
        int n = args_rest[0] ? atoi(args_rest) : 1;
        if (n < 1) n = 1;
        sim_state_t st = sim_get_state(g_sim);
        if (st != SIM_READY && st != SIM_PAUSED) {
            con_add(CON_COL_ERR, "Cannot step (state: %s)", sim_state_name(st));
            return;
        }
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (cpu) { g_prev_cpu = *cpu; g_prev_cpu_valid = true; }
        sim_step(g_sim, n);
        g_last_write_count = sim_get_last_writes(g_sim, g_last_writes, 256);
        cpu = sim_get_cpu(g_sim);
        if (cpu) {
            bool is45 = (sim_get_cpu_type(g_sim) == CPU_45GS02);
            if (is45)
                con_add(CON_COL_NORMAL,
                    "A=%02X X=%02X Y=%02X Z=%02X B=%02X  S=%04X PC=%04X  P=%02X  Cycles=%lu",
                    cpu->a, cpu->x, cpu->y, cpu->z, cpu->b,
                    cpu->s, cpu->pc, cpu->p, cpu->cycles);
            else
                con_add(CON_COL_NORMAL,
                    "A=%02X X=%02X Y=%02X  S=%04X PC=%04X  P=%02X  Cycles=%lu",
                    cpu->a, cpu->x, cpu->y, cpu->s, cpu->pc, cpu->p, cpu->cycles);
        }
        return;
    }

    if (strcmp(verb, "run") == 0) {
        sim_state_t st = sim_get_state(g_sim);
        if (st == SIM_READY || st == SIM_PAUSED) {
            g_running = true;
            con_add(CON_COL_OK, "Running...");
        } else {
            con_add(CON_COL_ERR, "Cannot run (state: %s)", sim_state_name(st));
        }
        return;
    }

    if (strcmp(verb, "pause") == 0) {
        g_running = false;
        con_add(CON_COL_WARN, "Paused.");
        return;
    }

    if (strcmp(verb, "reset") == 0) {
        g_running = false;
        sim_reset(g_sim);
        g_prev_cpu_valid   = false;
        g_last_write_count = 0;
        for (int i = 0; i < g_watch_count; i++) g_watches[i].valid = false;
        update_watches();
        con_add(CON_COL_OK, "CPU reset. State: %s", sim_state_name(sim_get_state(g_sim)));
        return;
    }

    if (strcmp(verb, "regs") == 0) {
        if (sim_get_state(g_sim) == SIM_IDLE) {
            con_add(CON_COL_ERR, "No program loaded.");
            return;
        }
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (!cpu) return;
        bool is45 = (sim_get_cpu_type(g_sim) == CPU_45GS02);
        if (is45)
            con_add(CON_COL_NORMAL,
                "A=%02X X=%02X Y=%02X Z=%02X B=%02X  S=%04X PC=%04X  P=%02X  Cycles=%lu",
                cpu->a, cpu->x, cpu->y, cpu->z, cpu->b,
                cpu->s, cpu->pc, cpu->p, cpu->cycles);
        else
            con_add(CON_COL_NORMAL,
                "A=%02X X=%02X Y=%02X  S=%04X PC=%04X  P=%02X  Cycles=%lu",
                cpu->a, cpu->x, cpu->y, cpu->s, cpu->pc, cpu->p, cpu->cycles);
        return;
    }

    if (strcmp(verb, "mem") == 0) {
        if (sim_get_state(g_sim) == SIM_IDLE) {
            con_add(CON_COL_ERR, "No program loaded.");
            return;
        }
        cpu_t *cpu = sim_get_cpu(g_sim);
        uint16_t addr = cpu ? cpu->pc : 0x0200;
        int n = 128;
        if (args_rest[0]) {
            char *endp;
            addr = (uint16_t)strtol(args_rest, &endp, 16);
            while (*endp == ' ' || *endp == '\t') endp++;
            if (*endp) n = atoi(endp);
        }
        if (n < 1) n = 1;
        if (n > 256) n = 256;
        for (int row = 0; row * 16 < n; row++) {
            uint16_t ra = (uint16_t)(addr + row * 16);
            int bytes = ((row + 1) * 16 <= n) ? 16 : n - row * 16;
            char ln[80];
            int pos = snprintf(ln, sizeof(ln), "%04X:", ra);
            for (int c = 0; c < bytes; c++) {
                uint8_t v = sim_mem_read_byte(g_sim, (uint16_t)(ra + c));
                pos += snprintf(ln + pos, (int)sizeof(ln) - pos, " %02X", v);
            }
            con_add(CON_COL_NORMAL, "%s", ln);
        }
        return;
    }

    if (strcmp(verb, "disasm") == 0) {
        if (sim_get_state(g_sim) == SIM_IDLE) {
            con_add(CON_COL_ERR, "No program loaded.");
            return;
        }
        cpu_t *cpu = sim_get_cpu(g_sim);
        uint16_t addr = cpu ? cpu->pc : 0x0200;
        int n = 8;
        if (args_rest[0]) {
            char *endp;
            addr = (uint16_t)strtol(args_rest, &endp, 16);
            while (*endp == ' ' || *endp == '\t') endp++;
            if (*endp) n = atoi(endp);
        }
        if (n < 1) n = 1;
        if (n > 64) n = 64;
        char dbuf[128];
        for (int i = 0; i < n; i++) {
            int consumed = sim_disassemble_one(g_sim, addr, dbuf, sizeof(dbuf));
            con_add(CON_COL_NORMAL, "%s", dbuf);
            if (consumed < 1) consumed = 1;
            addr = (uint16_t)(addr + consumed);
        }
        return;
    }

    if (strcmp(verb, "break") == 0) {
        if (!args_rest[0]) { con_add(CON_COL_ERR, "Usage: break <addr> [cond]"); return; }
        char *endp;
        uint16_t addr = (uint16_t)strtol(args_rest, &endp, 16);
        while (*endp == ' ' || *endp == '\t') endp++;
        const char *cond = *endp ? endp : NULL;
        if (sim_break_set(g_sim, addr, cond))
            con_add(CON_COL_OK, "Breakpoint set at $%04X%s%s",
                    addr, cond ? " if " : "", cond ? cond : "");
        else
            con_add(CON_COL_ERR, "Breakpoint table full (max 16).");
        return;
    }

    if (strcmp(verb, "del") == 0) {
        if (!args_rest[0]) { con_add(CON_COL_ERR, "Usage: del <addr>"); return; }
        uint16_t addr = (uint16_t)strtol(args_rest, NULL, 16);
        sim_break_clear(g_sim, addr);
        con_add(CON_COL_OK, "Breakpoint at $%04X removed.", addr);
        return;
    }

    if (strcmp(verb, "breaks") == 0) {
        int cnt = sim_break_count(g_sim);
        if (cnt == 0) { con_add(CON_COL_NORMAL, "No breakpoints set."); return; }
        con_add(CON_COL_NORMAL, "%d breakpoint(s):", cnt);
        for (int i = 0; i < cnt; i++) {
            uint16_t addr = 0;
            char cond[128] = "";
            sim_break_get(g_sim, i, &addr, cond, sizeof(cond));
            if (cond[0])
                con_add(CON_COL_NORMAL, "  %d: $%04X  if %s", i, addr, cond);
            else
                con_add(CON_COL_NORMAL, "  %d: $%04X", i, addr);
        }
        return;
    }

    if (strcmp(verb, "load") == 0) {
        if (!args_rest[0]) { con_add(CON_COL_ERR, "Usage: load <path>"); return; }
        char path[512];
        strncpy(path, args_rest, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        int len = (int)strlen(path);
        while (len > 0 && (path[len-1] == '\n' || path[len-1] == '\r' ||
                           path[len-1] == ' '  || path[len-1] == '\t'))
            path[--len] = '\0';
        g_running = false;
        snprintf(g_filename_buf, sizeof(g_filename_buf), "%s", path);
        do_load();
        if (sim_get_state(g_sim) != SIM_IDLE)
            con_add(CON_COL_OK, "Loaded: %s", path);
        else
            con_add(CON_COL_ERR, "Failed to load: %s", path);
        return;
    }

    con_add(CON_COL_ERR, "Unknown command '%s'. Type 'help' for list.", verb);
}

/* --------------------------------------------------------------------------
 * Helper: refresh all watch values from memory
 * -------------------------------------------------------------------------- */
static void update_watches(void)
{
    if (sim_get_state(g_sim) == SIM_IDLE) return;
    for (int i = 0; i < g_watch_count; i++) {
        uint8_t v = sim_mem_read_byte(g_sim, g_watches[i].addr);
        if (!g_watches[i].valid) {
            g_watches[i].cur_val  = v;
            g_watches[i].prev_val = v;
            g_watches[i].valid    = true;
            g_watches[i].changed  = false;
        } else {
            g_watches[i].changed  = (v != g_watches[i].cur_val);
            g_watches[i].prev_val = g_watches[i].cur_val;
            g_watches[i].cur_val  = v;
        }
    }
}

/* --------------------------------------------------------------------------
 * Pane: Breakpoint Manager (Phase 3)
 *
 * Lists all breakpoints with enable toggle and delete button.
 * Inline add-row at the top for fast entry.
 * -------------------------------------------------------------------------- */
static void draw_pane_breakpoints(void)
{
    if (!show_breakpoints) return;
    ImGui::Begin("Breakpoints", &show_breakpoints);

    /* Add row */
    static char bp_addr_buf[8] = "";
    static char bp_cond_buf[64] = "";
    ImGui::SetNextItemWidth(60);
    ImGui::InputText("Addr##bpa", bp_addr_buf, sizeof(bp_addr_buf),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    ImGui::InputText("if##bpc", bp_cond_buf, sizeof(bp_cond_buf));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add") && bp_addr_buf[0]) {
        uint16_t addr = (uint16_t)strtol(bp_addr_buf, NULL, 16);
        const char *cond = bp_cond_buf[0] ? bp_cond_buf : NULL;
        if (sim_break_set(g_sim, addr, cond)) {
            bp_addr_buf[0] = '\0';
            bp_cond_buf[0] = '\0';
        }
    }
    ImGui::Separator();

    int cnt = sim_break_count(g_sim);
    if (cnt == 0) {
        ImGui::TextDisabled("No breakpoints set.");
        ImGui::End();
        return;
    }

    const ImGuiTableFlags tf =
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_RowBg;

    if (ImGui::BeginTable("##bptab", 4, tf)) {
        ImGui::TableSetupColumn("En",        ImGuiTableColumnFlags_WidthFixed,   22.0f);
        ImGui::TableSetupColumn("Address",   ImGuiTableColumnFlags_WidthFixed,   52.0f);
        ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("",          ImGuiTableColumnFlags_WidthFixed,   30.0f);
        ImGui::TableHeadersRow();

        int to_delete = -1;
        for (int i = 0; i < cnt; i++) {
            uint16_t addr = 0;
            char cond[128] = "";
            sim_break_get(g_sim, i, &addr, cond, sizeof(cond));
            bool enabled = (sim_break_is_enabled(g_sim, i) != 0);

            ImGui::TableNextRow();

            /* Enable checkbox */
            ImGui::TableSetColumnIndex(0);
            char cb_id[16]; snprintf(cb_id, sizeof(cb_id), "##en%d", i);
            if (ImGui::Checkbox(cb_id, &enabled))
                sim_break_toggle(g_sim, i);

            /* Address */
            ImGui::TableSetColumnIndex(1);
            if (!enabled) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("$%04X", (unsigned)addr);
            if (!enabled) ImGui::PopStyleColor();

            /* Condition */
            ImGui::TableSetColumnIndex(2);
            if (cond[0]) {
                if (!enabled) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(cond);
                if (!enabled) ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("(always)");
            }

            /* Delete */
            ImGui::TableSetColumnIndex(3);
            char del_id[16]; snprintf(del_id, sizeof(del_id), "Del##%d", i);
            if (ImGui::SmallButton(del_id))
                to_delete = i;
        }
        ImGui::EndTable();

        if (to_delete >= 0) {
            uint16_t addr = 0;
            sim_break_get(g_sim, to_delete, &addr, NULL, 0);
            sim_break_clear(g_sim, addr);
        }
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Execution Trace Log (Phase 3)
 *
 * Ring buffer of the last SIM_TRACE_DEPTH instructions executed.
 * Slot 0 = most recent.  Virtualised with ImGuiListClipper.
 * -------------------------------------------------------------------------- */
static void draw_pane_trace(void)
{
    if (!show_trace) return;
    ImGui::Begin("Trace Log", &show_trace);

    /* Controls */
    bool rec = (sim_trace_is_enabled(g_sim) != 0);
    if (ImGui::Checkbox("Record", &rec))
        sim_trace_enable(g_sim, rec ? 1 : 0);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        sim_trace_clear(g_sim);
    int cnt = sim_trace_count(g_sim);
    ImGui::SameLine();
    ImGui::TextDisabled("(%d / %d)", cnt, SIM_TRACE_DEPTH);
    ImGui::Separator();

    if (cnt == 0) {
        ImGui::TextDisabled("No entries. Enable 'Record' then step or run.");
        ImGui::End();
        return;
    }

    const ImGuiTableFlags tf =
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_ScrollY        |
        ImGuiTableFlags_RowBg;

    float avail_h = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("##trace", 5, tf, ImVec2(0, avail_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",     ImGuiTableColumnFlags_WidthFixed,   38.0f);
        ImGui::TableSetupColumn("PC",    ImGuiTableColumnFlags_WidthFixed,   44.0f);
        ImGui::TableSetupColumn("Instr", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Cyc",   ImGuiTableColumnFlags_WidthFixed,   26.0f);
        ImGui::TableSetupColumn("A  X  Y", ImGuiTableColumnFlags_WidthFixed, 66.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(cnt);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                sim_trace_entry_t e;
                if (!sim_trace_get(g_sim, i, &e)) continue;

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", i);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%04X", (unsigned)e.pc);

                ImGui::TableSetColumnIndex(2);
                /* disasm = "$XXXX: %-18s %-6s %s"
                 * Skip the 7-char "$XXXX: " address prefix. */
                const char *ins = (strlen(e.disasm) > 7) ? e.disasm + 7 : e.disasm;
                ImGui::TextUnformatted(ins);

                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%d", e.cycles_delta);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%02X %02X %02X",
                            (unsigned)e.cpu.a,
                            (unsigned)e.cpu.x,
                            (unsigned)e.cpu.y);
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Stack Inspector (Phase 3)
 *
 * Shows the 6502 stack page ($0100–$01FF).
 * The current SP row is highlighted; values above SP are active stack data.
 * -------------------------------------------------------------------------- */
static void draw_pane_stack(void)
{
    if (!show_stack) return;
    ImGui::Begin("Stack", &show_stack);

    sim_state_t state = sim_get_state(g_sim);
    if (state == SIM_IDLE) {
        ImGui::TextDisabled("(no program loaded)");
        ImGui::End();
        return;
    }

    cpu_t *cpu = sim_get_cpu(g_sim);
    if (!cpu) { ImGui::End(); return; }

    uint8_t sp = (uint8_t)(cpu->s & 0xFF);   /* low byte = page offset */
    uint16_t sp_addr = (uint16_t)(0x0100 | sp);

    ImGui::Text("SP = $%04X  (depth %d)", (unsigned)sp_addr, (int)(0xFF - sp));
    ImGui::Separator();

    const ImGuiTableFlags tf =
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_ScrollY        |
        ImGuiTableFlags_RowBg;

    float avail_h = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("##stk", 3, tf, ImVec2(0, avail_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed,   52.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,   38.0f);
        ImGui::TableSetupColumn("Note",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        /* Walk from $01FF down to $0101 (skip $0100 = SP=0xFF = empty) */
        bool need_scroll = false;
        for (int offset = 0xFF; offset >= 0x01; offset--) {
            uint16_t addr = (uint16_t)(0x0100 | offset);
            uint8_t  val  = sim_mem_read_byte(g_sim, addr);
            bool is_sp    = (offset == (sp + 1) % 0x100); /* top of stack */
            bool active   = (offset > sp);                 /* pushed data */

            ImGui::TableNextRow();

            /* Highlight the top-of-stack row */
            if (is_sp) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 0.3f, 0.25f)));
                need_scroll = true;
            }

            ImGui::TableSetColumnIndex(0);
            if (active)
                ImGui::Text("$%04X", (unsigned)addr);
            else
                ImGui::TextDisabled("$%04X", (unsigned)addr);

            ImGui::TableSetColumnIndex(1);
            if (active)
                ImGui::Text("%02X", (unsigned)val);
            else
                ImGui::TextDisabled("%02X", (unsigned)val);

            ImGui::TableSetColumnIndex(2);
            if (is_sp)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "<-- SP+1");
            else if (offset == sp)
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "<-- SP");
            else if (active && offset < 0xFF && offset > sp) {
                /* Pair check: every two pushed bytes may be a return address */
                if ((offset & 1) == 1 && offset + 1 <= 0xFF) {
                    uint8_t lo = sim_mem_read_byte(g_sim, addr);
                    uint8_t hi = sim_mem_read_byte(g_sim, (uint16_t)(addr + 1));
                    uint16_t ret_addr = (uint16_t)((hi << 8) | lo);
                    /* RTS pops (stored_val+1) */
                    ImGui::TextDisabled("->$%04X ?", (unsigned)((ret_addr + 1) & 0xFFFF));
                }
            }
        }

        /* Scroll the table so SP row is visible */
        if (need_scroll) {
            float row_h = ImGui::GetTextLineHeightWithSpacing();
            int row_idx = 0xFF - (sp + 1);
            ImGui::SetScrollY((float)row_idx * row_h);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: I/O Watch (Phase 3)
 *
 * User-defined address watch list.  Values are refreshed after each step.
 * Changed values are highlighted yellow.
 * -------------------------------------------------------------------------- */
static void draw_pane_watches(void)
{
    if (!show_watches) return;
    ImGui::Begin("Watch", &show_watches);

    /* Add row */
    static char w_addr_buf[8] = "";
    static char w_name_buf[32] = "";
    ImGui::SetNextItemWidth(60);
    ImGui::InputText("Addr##wa", w_addr_buf, sizeof(w_addr_buf),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("Name##wn", w_name_buf, sizeof(w_name_buf));
    ImGui::SameLine();
    bool can_add = (g_watch_count < WATCH_MAX && w_addr_buf[0]);
    if (!can_add) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Add")) {
        WatchEntry &w = g_watches[g_watch_count++];
        w.addr    = (uint16_t)strtol(w_addr_buf, NULL, 16);
        snprintf(w.name, sizeof(w.name), "%s", w_name_buf[0] ? w_name_buf : "?");
        w.cur_val = w.prev_val = 0;
        w.changed = false;
        w.valid   = false;
        w_addr_buf[0] = '\0';
        w_name_buf[0] = '\0';
        update_watches();  /* seed the initial value */
    }
    if (!can_add) ImGui::EndDisabled();
    ImGui::Separator();

    if (g_watch_count == 0) {
        ImGui::TextDisabled("No watches. Enter address and click Add.");
        ImGui::End();
        return;
    }

    const ImGuiTableFlags tf =
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_RowBg;

    if (ImGui::BeginTable("##wtab", 5, tf)) {
        ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed,   52.0f);
        ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Hex",   ImGuiTableColumnFlags_WidthFixed,   30.0f);
        ImGui::TableSetupColumn("Dec",   ImGuiTableColumnFlags_WidthFixed,   28.0f);
        ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed,   30.0f);
        ImGui::TableHeadersRow();

        int to_del = -1;
        for (int i = 0; i < g_watch_count; i++) {
            WatchEntry &w = g_watches[i];

            /* Refresh if not yet read */
            if (!w.valid && sim_get_state(g_sim) != SIM_IDLE) {
                w.cur_val = sim_mem_read_byte(g_sim, w.addr);
                w.prev_val = w.cur_val;
                w.valid    = true;
            }

            ImGui::TableNextRow();

            ImVec4 val_col = w.changed
                ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f)
                : ImGui::GetStyleColorVec4(ImGuiCol_Text);

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("$%04X", (unsigned)w.addr);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(w.name);

            ImGui::TableSetColumnIndex(2);
            ImGui::PushStyleColor(ImGuiCol_Text, val_col);
            ImGui::Text("%02X", (unsigned)w.cur_val);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_Text, val_col);
            ImGui::Text("%3d", (int)w.cur_val);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(4);
            char del_id[16]; snprintf(del_id, sizeof(del_id), "Del##w%d", i);
            if (ImGui::SmallButton(del_id))
                to_del = i;
        }
        ImGui::EndTable();

        if (to_del >= 0) {
            /* Remove by shifting */
            for (int i = to_del; i < g_watch_count - 1; i++)
                g_watches[i] = g_watches[i + 1];
            g_watch_count--;
        }
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Toolbar
 * -------------------------------------------------------------------------- */
static void draw_toolbar(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

    float btn_w   = 60.0f;
    float combo_w = 110.0f;
    float avail   = ImGui::GetContentRegionAvail().x;
    float input_w = avail - combo_w - btn_w * 5.0f
                  - ImGui::GetStyle().ItemSpacing.x * 8.0f - 12.0f;
    if (input_w < 80.0f) input_w = 80.0f;

    if (g_focus_filename) {
        ImGui::SetKeyboardFocusHere();
        g_focus_filename = false;
    }

    ImGui::SetNextItemWidth(input_w);
    bool enter_pressed = ImGui::InputText("##filename", g_filename_buf,
                                          sizeof(g_filename_buf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Path to .asm file (Ctrl+L to focus)");

    ImGui::SameLine();
    if (ImGui::Button("Load") || enter_pressed) {
        do_load();
        if (sim_get_state(g_sim) != SIM_IDLE)
            con_add(CON_COL_OK, "Loaded: %s", g_filename_buf);
        else
            con_add(CON_COL_ERR, "Failed to load: %s", g_filename_buf);
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    /* Step (F7) */
    bool can_step = (sim_get_state(g_sim) == SIM_READY ||
                     sim_get_state(g_sim) == SIM_PAUSED);
    if (!can_step) ImGui::BeginDisabled();
    if (ImGui::Button("Step")) {
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (cpu) { g_prev_cpu = *cpu; g_prev_cpu_valid = true; }
        sim_step(g_sim, 1);
        g_last_write_count = sim_get_last_writes(g_sim, g_last_writes, 256);
        update_watches();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step 1 instruction (F7)");
    if (!can_step) ImGui::EndDisabled();

    ImGui::SameLine();

    /* Run / Pause toggle (F5 / F6) */
    bool can_run = (sim_get_state(g_sim) == SIM_READY ||
                    sim_get_state(g_sim) == SIM_PAUSED || g_running);
    if (!can_run) ImGui::BeginDisabled();
    if (g_running) {
        if (ImGui::Button("Pause")) g_running = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause execution (F6 / Esc)");
    } else {
        if (ImGui::Button(" Run ")) g_running = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run continuously (F5)");
    }
    if (!can_run) ImGui::EndDisabled();

    ImGui::SameLine();

    /* Reset (Ctrl+R) */
    bool can_reset = (sim_get_state(g_sim) != SIM_IDLE);
    if (!can_reset) ImGui::BeginDisabled();
    if (ImGui::Button("Reset")) {
        g_running = false;
        sim_reset(g_sim);
        g_prev_cpu_valid   = false;
        g_last_write_count = 0;
        for (int i = 0; i < g_watch_count; i++) g_watches[i].valid = false;
        update_watches();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset CPU (Ctrl+R)");
    if (!can_reset) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    /* Processor dropdown */
    ImGui::SetNextItemWidth(combo_w);
    if (ImGui::BeginCombo("##proc", g_proc_labels[g_proc_idx])) {
        for (int i = 0; i < (int)(sizeof(g_proc_labels) / sizeof(g_proc_labels[0])); i++) {
            bool selected = (i == g_proc_idx);
            if (ImGui::Selectable(g_proc_labels[i], selected)) {
                g_proc_idx = i;
                sim_set_processor(g_sim, g_proc_ids[i]);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Active processor");

    ImGui::PopStyleVar();
    ImGui::Separator();
}

/* --------------------------------------------------------------------------
 * Status bar
 * -------------------------------------------------------------------------- */
static void draw_statusbar(void)
{
    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

    sim_state_t state = sim_get_state(g_sim);
    cpu_t *cpu        = sim_get_cpu(g_sim);

    if (g_running)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "RUNNING");
    else {
        const char *sname = sim_state_name(state);
        if      (state == SIM_IDLE)     ImGui::TextDisabled("%s", sname);
        else if (state == SIM_FINISHED) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", sname);
        else                            ImGui::Text("%s", sname);
    }

    ImGui::SameLine(0, 20);

    if (state != SIM_IDLE && cpu) {
        ImGui::Text("PC=%04X  A=%02X  X=%02X  Y=%02X  S=%04X  Cycles=%lu",
                    cpu->pc, cpu->a, cpu->x, cpu->y, cpu->s, cpu->cycles);
    } else {
        ImGui::TextDisabled("PC=----  A=--  X=--  Y=--  S=----  Cycles=0");
    }

    const char *fname = sim_get_filename(g_sim);
    float fname_w = ImGui::CalcTextSize(fname).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - fname_w);
    ImGui::TextDisabled("%s", fname);

    ImGui::PopStyleVar();
}

/* --------------------------------------------------------------------------
 * Pane: Registers (Phase 2)
 *
 * Compact mode: coloured diff highlighting per register, flags row, cycles.
 * Expanded mode: InputScalar fields for live editing of each register.
 * Yellow text = value changed since last sim_step.
 * -------------------------------------------------------------------------- */
static void draw_pane_registers(void)
{
    if (!show_registers) return;
    ImGui::Begin("Registers", &show_registers);

    cpu_t *cpu        = sim_get_cpu(g_sim);
    sim_state_t state = sim_get_state(g_sim);
    if (state == SIM_IDLE || !cpu) {
        ImGui::TextDisabled("(no program loaded)");
        ImGui::End();
        return;
    }

    bool is_45gs02 = (sim_get_cpu_type(g_sim) == CPU_45GS02);
    cpu_t *prev    = g_prev_cpu_valid ? &g_prev_cpu : NULL;

    /* Expand/Compact toggle button — right-aligned */
    {
        const char *btn = g_regs_expanded ? "Compact" : "Expand";
        float bw = ImGui::CalcTextSize(btn).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - bw);
        if (ImGui::SmallButton(btn)) g_regs_expanded = !g_regs_expanded;
    }
    ImGui::Separator();

    /* Yellow if changed, otherwise default */
    auto diff_col = [&](bool changed) -> ImVec4 {
        return changed ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f)
                       : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    };

    if (!g_regs_expanded) {
        /* ---- Compact mode ---- */
        auto show_byte = [&](const char *name, uint8_t cur, uint8_t old_val) {
            bool ch = prev && (old_val != cur);
            ImGui::PushStyleColor(ImGuiCol_Text, diff_col(ch));
            ImGui::Text("%s %02X", name, (unsigned)cur);
            ImGui::PopStyleColor();
        };

        show_byte("A", cpu->a, prev ? prev->a : cpu->a);
        ImGui::SameLine(0, 10);
        show_byte("X", cpu->x, prev ? prev->x : cpu->x);
        ImGui::SameLine(0, 10);
        show_byte("Y", cpu->y, prev ? prev->y : cpu->y);

        if (is_45gs02) {
            show_byte("Z", cpu->z, prev ? prev->z : cpu->z);
            ImGui::SameLine(0, 10);
            show_byte("B", cpu->b, prev ? prev->b : cpu->b);
        }

        ImGui::Separator();

        {
            bool ch_s  = prev && prev->s  != cpu->s;
            bool ch_pc = prev && prev->pc != cpu->pc;
            ImGui::PushStyleColor(ImGuiCol_Text, diff_col(ch_s));
            if (is_45gs02)
                ImGui::Text("S  %04X", (unsigned)cpu->s);
            else
                ImGui::Text("S  %04X", (unsigned)(0x0100 | (cpu->s & 0xFF)));
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 10);
            ImGui::PushStyleColor(ImGuiCol_Text, diff_col(ch_pc));
            ImGui::Text("PC %04X", (unsigned)cpu->pc);
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        /* Flags — green=set, dim=clear, yellow=changed */
        struct { const char *lbl; uint8_t mask; } flags[] = {
            {"N", FLAG_N}, {"V", FLAG_V}, {"-", FLAG_U},
            {"B", FLAG_B}, {"D", FLAG_D}, {"I", FLAG_I},
            {"Z", FLAG_Z}, {"C", FLAG_C},
        };
        for (int i = 0; i < 8; i++) {
            bool set     = (cpu->p & flags[i].mask) != 0;
            bool changed = prev && ((prev->p & flags[i].mask) != (cpu->p & flags[i].mask));
            ImVec4 col;
            if      (changed) col = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
            else if (set)     col = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
            else              col = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Text("%s", flags[i].lbl);
            ImGui::PopStyleColor();
            if (i < 7) ImGui::SameLine();
        }

        ImGui::Separator();

        bool ch_cy = prev && prev->cycles != cpu->cycles;
        ImGui::PushStyleColor(ImGuiCol_Text, diff_col(ch_cy));
        ImGui::Text("Cycles  %lu", cpu->cycles);
        ImGui::PopStyleColor();

        if (is_45gs02 && cpu->eom_prefix)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[EOM]");

    } else {
        /* ---- Expanded / editable mode ---- */
        auto edit_byte = [&](const char *name, uint8_t cur, uint8_t prev_val) {
            bool ch = prev && (prev_val != cur);
            if (ch) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("%-2s", name);
            if (ch) ImGui::PopStyleColor();
            ImGui::SameLine(45);
            ImGui::SetNextItemWidth(56);
            char id[8]; snprintf(id, sizeof(id), "##%s", name);
            uint8_t edited = cur;
            if (ImGui::InputScalar(id, ImGuiDataType_U8, &edited,
                                   NULL, NULL, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal))
                sim_set_reg_byte(g_sim, name, edited);
        };

        edit_byte("A", cpu->a, prev ? prev->a : cpu->a);
        edit_byte("X", cpu->x, prev ? prev->x : cpu->x);
        edit_byte("Y", cpu->y, prev ? prev->y : cpu->y);
        if (is_45gs02) {
            edit_byte("Z", cpu->z, prev ? prev->z : cpu->z);
            edit_byte("B", cpu->b, prev ? prev->b : cpu->b);
        }

        ImGui::Separator();

        /* S — 16-bit on 45GS02, 8-bit page 1 on others */
        {
            bool ch_s = prev && prev->s != cpu->s;
            if (ch_s) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("S ");
            if (ch_s) ImGui::PopStyleColor();
            ImGui::SameLine(45);
            ImGui::SetNextItemWidth(56);
            if (is_45gs02) {
                uint16_t s_edit = cpu->s;
                if (ImGui::InputScalar("##S", ImGuiDataType_U16, &s_edit,
                                       NULL, NULL, "%04X",
                                       ImGuiInputTextFlags_CharsHexadecimal))
                    sim_set_reg_byte(g_sim, "S", (uint8_t)(s_edit & 0xFF));
            } else {
                uint8_t s_edit = (uint8_t)(cpu->s & 0xFF);
                if (ImGui::InputScalar("##S", ImGuiDataType_U8, &s_edit,
                                       NULL, NULL, "%02X",
                                       ImGuiInputTextFlags_CharsHexadecimal))
                    sim_set_reg_byte(g_sim, "S", s_edit);
            }
        }

        /* PC — 16-bit */
        {
            bool ch_pc = prev && prev->pc != cpu->pc;
            if (ch_pc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("PC");
            if (ch_pc) ImGui::PopStyleColor();
            ImGui::SameLine(45);
            ImGui::SetNextItemWidth(56);
            uint16_t pc_edit = cpu->pc;
            if (ImGui::InputScalar("##PC", ImGuiDataType_U16, &pc_edit,
                                   NULL, NULL, "%04X",
                                   ImGuiInputTextFlags_CharsHexadecimal))
                sim_set_pc(g_sim, pc_edit);
        }

        edit_byte("P", cpu->p, prev ? prev->p : cpu->p);

        ImGui::Separator();
        ImGui::Text("Cycles  %lu", cpu->cycles);
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Disassembly (Phase 2)
 *
 * Table columns: BP gutter (clickable) | PC arrow | disasm text | cycles
 * Follow-PC mode: g_disasm_addr tracks cpu->pc each frame.
 * Mouse wheel scrolls the view when not following PC.
 * -------------------------------------------------------------------------- */
static void draw_pane_disassembly(void)
{
    if (!show_disassembly) return;
    ImGui::Begin("Disassembly", &show_disassembly);

    sim_state_t state = sim_get_state(g_sim);
    if (state == SIM_IDLE) {
        ImGui::TextDisabled("(no program loaded)");
        ImGui::End();
        return;
    }

    cpu_t *cpu  = sim_get_cpu(g_sim);
    uint16_t pc = cpu ? cpu->pc : 0;

    /* Controls bar */
    ImGui::Checkbox("Follow PC", &g_disasm_follow);
    ImGui::SameLine();
    {
        char addr_buf[8];
        snprintf(addr_buf, sizeof(addr_buf), "%04X", (unsigned)g_disasm_addr);
        ImGui::SetNextItemWidth(60);
        if (ImGui::InputText("##da", addr_buf, sizeof(addr_buf),
                ImGuiInputTextFlags_CharsHexadecimal |
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            g_disasm_addr   = (uint16_t)strtol(addr_buf, NULL, 16);
            g_disasm_follow = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("PC")) {
        g_disasm_addr   = pc;
        g_disasm_follow = true;
    }
    ImGui::Separator();

    /* Follow PC: keep view aligned to current instruction */
    if (g_disasm_follow)
        g_disasm_addr = pc;

    /* Mouse wheel scrolling (when not following PC) */
    if (ImGui::IsWindowHovered() && !g_disasm_follow) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel < 0.0f) {
            /* Scroll down: advance by instruction */
            int ticks = (int)(-wheel + 0.5f);
            char tmp[128];
            uint16_t a = g_disasm_addr;
            for (int i = 0; i < ticks; i++) {
                int c = sim_disassemble_one(g_sim, a, tmp, sizeof(tmp));
                if (c < 1) c = 1;
                a = (uint16_t)(a + c);
            }
            g_disasm_addr = a;
        } else if (wheel > 0.0f) {
            /* Scroll up: approximate 3 bytes per instruction */
            int ticks = (int)(wheel + 0.5f);
            g_disasm_addr = (uint16_t)(g_disasm_addr - ticks * 3);
        }
    }

    /* Estimate rows that fit */
    float row_h  = ImGui::GetTextLineHeightWithSpacing();
    float avail_h = ImGui::GetContentRegionAvail().y;
    int rows = (avail_h > 0) ? (int)(avail_h / row_h) + 1 : 24;
    if (rows < 4)  rows = 4;
    if (rows > 80) rows = 80;

    /* Table */
    const ImGuiTableFlags tflags =
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_BordersInnerV;

    if (ImGui::BeginTable("##dasm", 4, tflags)) {
        ImGui::TableSetupColumn("BP",    ImGuiTableColumnFlags_WidthFixed, 18.0f);
        ImGui::TableSetupColumn(" ",     ImGuiTableColumnFlags_WidthFixed, 14.0f);
        ImGui::TableSetupColumn("Instr", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Cyc",   ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableHeadersRow();

        char buf[128];
        uint16_t addr = g_disasm_addr;

        for (int i = 0; i < rows; i++) {
            bool is_pc = (addr == pc);
            bool has_bp = (sim_has_breakpoint(g_sim, addr) != 0);
            int consumed = sim_disassemble_one(g_sim, addr, buf, sizeof(buf));

            ImGui::TableNextRow();

            /* BP gutter — clickable to toggle */
            ImGui::TableSetColumnIndex(0);
            {
                char btn_id[16];
                snprintf(btn_id, sizeof(btn_id), "%s##%04X",
                         has_bp ? "*" : ".", (unsigned)addr);
                if (has_bp)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 0.6f));
                if (ImGui::SmallButton(btn_id)) {
                    if (has_bp) sim_break_clear(g_sim, addr);
                    else        sim_break_set(g_sim, addr, NULL);
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(has_bp ? "Remove breakpoint at $%04X"
                                             : "Set breakpoint at $%04X",
                                      (unsigned)addr);
            }

            /* PC arrow */
            ImGui::TableSetColumnIndex(1);
            if (is_pc)
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), ">");
            else
                ImGui::TextDisabled(" ");

            /* Disassembly text */
            ImGui::TableSetColumnIndex(2);
            {
                const char *sym = sim_sym_by_addr(g_sim, addr);
                if (sym) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
                    ImGui::Text("%s:", sym);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 6);
                }
                if (is_pc)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
                else if (has_bp)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("%s", buf);
                if (is_pc || has_bp)
                    ImGui::PopStyleColor();
            }

            /* Cycles */
            ImGui::TableSetColumnIndex(3);
            int cyc = sim_get_opcode_cycles(g_sim, addr);
            if (cyc > 0)
                ImGui::TextDisabled("%d", cyc);

            if (consumed < 1) consumed = 1;
            addr = (uint16_t)(addr + consumed);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Memory View (Phase 2)
 *
 * 16-byte rows, hex + ASCII sidebar.
 * Bytes written in the most-recent sim_step are highlighted yellow.
 * -------------------------------------------------------------------------- */
static void draw_pane_memory(void)
{
    if (!show_memory) return;
    ImGui::Begin("Memory", &show_memory);

    sim_state_t state = sim_get_state(g_sim);
    if (state == SIM_IDLE) {
        ImGui::TextDisabled("(no program loaded)");
        ImGui::End();
        return;
    }

    static uint16_t mem_base  = 0x0200;
    static char     addr_buf[8] = "0200";

    /* Navigation bar */
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputText("##ma", addr_buf, sizeof(addr_buf),
            ImGuiInputTextFlags_CharsHexadecimal |
            ImGuiInputTextFlags_EnterReturnsTrue)) {
        mem_base = (uint16_t)strtol(addr_buf, NULL, 16);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("PC")) {
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (cpu) {
            mem_base = cpu->pc;
            snprintf(addr_buf, sizeof(addr_buf), "%04X", (unsigned)mem_base);
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("SP")) {
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (cpu) {
            mem_base = (uint16_t)(0x0100 | (cpu->s & 0xFF));
            snprintf(addr_buf, sizeof(addr_buf), "%04X", (unsigned)mem_base);
        }
    }
    ImGui::Separator();

    /* Is this address in the last-write log? */
    auto was_written = [&](uint16_t a) -> bool {
        for (int i = 0; i < g_last_write_count; i++)
            if (g_last_writes[i] == a) return true;
        return false;
    };

    /* Hex dump — 16 bytes per row, 16 rows */
    for (int row = 0; row < 16; row++) {
        uint16_t raddr = (uint16_t)(mem_base + row * 16);
        ImGui::Text("%04X ", (unsigned)raddr);
        for (int col = 0; col < 16; col++) {
            uint16_t ba  = (uint16_t)(raddr + col);
            uint8_t  v   = sim_mem_read_byte(g_sim, ba);
            bool written = was_written(ba);
            ImGui::SameLine(0, col == 8 ? 8 : 4);
            if (written)
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%02X", (unsigned)v);
            else
                ImGui::Text("%02X", (unsigned)v);
        }
        /* ASCII sidebar */
        ImGui::SameLine(0, 8);
        ImGui::TextDisabled("|");
        for (int col = 0; col < 16; col++) {
            uint8_t v = sim_mem_read_byte(g_sim, (uint16_t)(raddr + col));
            char ch = (v >= 0x20 && v < 0x7F) ? (char)v : '.';
            ImGui::SameLine(0, 0);
            ImGui::TextDisabled("%c", ch);
        }
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("|");
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Console (Phase 2)
 *
 * Scrollable text log with coloured output, command-line input, and
 * Up/Down history navigation.
 * -------------------------------------------------------------------------- */
static void draw_pane_console(void)
{
    if (!show_console) return;
    ImGui::Begin("Console", &show_console);

    /* Output scroll region */
    float input_h = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    ImGui::BeginChild("##con_out", ImVec2(0, -input_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (int i = 0; i < g_con_line_count; i++) {
        ImGui::PushStyleColor(ImGuiCol_Text, g_con_lines[i].color);
        ImGui::TextUnformatted(g_con_lines[i].text);
        ImGui::PopStyleColor();
    }

    if (g_con_scroll_bottom) {
        ImGui::SetScrollHereY(1.0f);
        g_con_scroll_bottom = false;
    }

    ImGui::EndChild();
    ImGui::Separator();

    /* Command input */
    ImGui::PushItemWidth(-1);
    bool reclaim = false;
    if (ImGui::InputText("##coninput", g_con_input, sizeof(g_con_input),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory,
            con_input_cb)) {
        if (g_con_input[0] != '\0') {
            con_exec(g_con_input);
            g_con_input[0] = '\0';
        }
        reclaim = true;
    }
    ImGui::PopItemWidth();

    /* Keep focus on the input field after Enter */
    if (reclaim)
        ImGui::SetKeyboardFocusHere(-1);

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */
int main(int /*argc*/, char ** /*argv*/)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const int display_index = 0;
    const float ui_scale    = detect_ui_scale(display_index);
    g_ui_scale = ui_scale;

    SDL_Rect usable = { 0, 0, 1600, 900 };
    SDL_GetDisplayUsableBounds(display_index, &usable);

    /* Pre-parse INI before SDL/ImGui init so font size and window geometry
     * are correct from frame 1 — no rebuild flash.                        */
    bool first_run;
    int  pre_win_x = -32768, pre_win_y = -32768;
    int  pre_win_w = 0,      pre_win_h = 0;
    {
        FILE *ini = fopen("sim6502-gui.ini", "r");
        first_run = (ini == nullptr);
        if (ini) {
            char ln[256];
            bool in_sec = false;
            while (fgets(ln, sizeof(ln), ini)) {
                if (strncmp(ln, "[sim6502][Settings]", 19) == 0) { in_sec = true;  continue; }
                if (ln[0] == '[')                                 { in_sec = false; continue; }
                if (!in_sec) continue;
                int v;
                if (sscanf(ln, "FontSize=%d", &v) == 1 && v >= 8  && v <= 32)  g_base_font_size = v;
                if (sscanf(ln, "WindowW=%d",  &v) == 1 && v >= 320)             pre_win_w = v;
                if (sscanf(ln, "WindowH=%d",  &v) == 1 && v >= 240)             pre_win_h = v;
                if (sscanf(ln, "WindowX=%d",  &v) == 1)                         pre_win_x = v;
                if (sscanf(ln, "WindowY=%d",  &v) == 1)                         pre_win_y = v;
            }
            fclose(ini);
        }
    }

    const int win_w = (pre_win_w > 0) ? pre_win_w : ((int)(usable.w * 0.80f) / 2) * 2;
    const int win_h = (pre_win_h > 0) ? pre_win_h : ((int)(usable.h * 0.80f) / 2) * 2;
    const bool has_saved_pos = (pre_win_x != -32768 && pre_win_y != -32768);
    const int  win_x = has_saved_pos ? pre_win_x : SDL_WINDOWPOS_CENTERED;
    const int  win_y = has_saved_pos ? pre_win_y : SDL_WINDOWPOS_CENTERED;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_Window *window = SDL_CreateWindow(
        "sim6502-gui",
        win_x, win_y,
        win_w, win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }
    g_window = window;

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    /* Custom INI handler: persists FontSize and window geometry */
    {
        ImGuiSettingsHandler h = {};
        h.TypeName   = "sim6502";
        h.TypeHash   = ImHashStr("sim6502");
        h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* {
            return (void*)(intptr_t)1;
        };
        h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
            int v;
            if (sscanf(line, "FontSize=%d", &v) == 1 && v >= 8 && v <= 32)
                if (v != g_base_font_size) { g_base_font_size = v; g_font_rebuild = true; }
        };
        h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
            int x = 0, y = 0, w = 800, wh = 600;
            if (g_window) {
                SDL_GetWindowPosition(g_window, &x, &y);
                SDL_GetWindowSize(g_window, &w, &wh);
            }
            buf->appendf("[%s][Settings]\n", handler->TypeName);
            buf->appendf("FontSize=%d\n", g_base_font_size);
            buf->appendf("WindowX=%d\n", x);
            buf->appendf("WindowY=%d\n", y);
            buf->appendf("WindowW=%d\n", w);
            buf->appendf("WindowH=%d\n", wh);
            buf->appendf("\n");
        };
        ImGui::AddSettingsHandler(&h);
    }

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = "sim6502-gui.ini";

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(ui_scale);

    ImFontConfig font_cfg;
    font_cfg.SizePixels = floorf((float)g_base_font_size * ui_scale);
    io.Fonts->AddFontDefault(&font_cfg);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    fprintf(stdout, "sim6502-gui: display %d  scale=%.2fx  font=%.0fpx  window=%dx%d%s\n",
            display_index, ui_scale, font_cfg.SizePixels,
            win_w, win_h, first_run ? "  (first run)" : "");

    /* Create simulator session */
    g_sim = sim_create("6502");
    if (!g_sim) {
        fprintf(stderr, "sim6502-gui: failed to create sim session\n");
        return 1;
    }

    /* Console welcome message */
    con_add(CON_COL_OK,     "sim6502-gui Phase 2 ready.  Type 'help' for commands.");
    con_add(CON_COL_NORMAL, "Processor: %s", sim_processor_name(g_sim));

    bool running        = true;
    bool layout_applied = false;

    while (running) {
        /* ---- Events ---- */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT)
                running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F4
                    && (ev.key.keysym.mod & KMOD_ALT))
                running = false;
        }

        /* ---- Keyboard shortcuts (when ImGui is not capturing keyboard) ---- */
        if (!io.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
                if (sim_get_state(g_sim) == SIM_READY ||
                    sim_get_state(g_sim) == SIM_PAUSED)
                    g_running = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F6) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape))
                g_running = false;
            if (ImGui::IsKeyPressed(ImGuiKey_F7)) {
                g_running = false;
                if (sim_get_state(g_sim) == SIM_READY ||
                    sim_get_state(g_sim) == SIM_PAUSED) {
                    cpu_t *cpu = sim_get_cpu(g_sim);
                    if (cpu) { g_prev_cpu = *cpu; g_prev_cpu_valid = true; }
                    sim_step(g_sim, 1);
                    g_last_write_count = sim_get_last_writes(g_sim, g_last_writes, 256);
                    update_watches();
                }
            }
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                    g_running = false;
                    sim_reset(g_sim);
                    g_prev_cpu_valid   = false;
                    g_last_write_count = 0;
                    for (int i = 0; i < g_watch_count; i++) g_watches[i].valid = false;
                    update_watches();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_L))
                    g_focus_filename = true;
            }
        }

        /* ---- Per-frame simulation step (run mode) ---- */
        if (g_running) {
            cpu_t *cpu = sim_get_cpu(g_sim);
            if (cpu) { g_prev_cpu = *cpu; g_prev_cpu_valid = true; }
            int ev_code = sim_step(g_sim, g_speed);
            g_last_write_count = sim_get_last_writes(g_sim, g_last_writes, 256);
            update_watches();
            if (ev_code > 0)
                g_running = false;
            else if (ev_code < 0)
                g_running = false;
        }

        /* ---- Font rebuild (before NewFrame) ---- */
        if (g_font_rebuild) {
            rebuild_font();
            g_font_rebuild = false;
        }

        /* ---- New ImGui frame ---- */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        /* ---- Full-viewport dockspace host window ---- */
        {
            const ImGuiViewport *vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);

            ImGuiWindowFlags host_flags =
                ImGuiWindowFlags_MenuBar               |
                ImGuiWindowFlags_NoDocking             |
                ImGuiWindowFlags_NoTitleBar            |
                ImGuiWindowFlags_NoCollapse            |
                ImGuiWindowFlags_NoResize              |
                ImGuiWindowFlags_NoMove                |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(8, 4));
            ImGui::Begin("##dockspace_host", nullptr, host_flags);
            ImGui::PopStyleVar(3);

            /* Menu bar */
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Load...", "Ctrl+L")) g_focus_filename = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Alt+F4")) running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Run")) {
                    if (ImGui::MenuItem("Step",  "F7")) {
                        g_running = false;
                        cpu_t *cpu = sim_get_cpu(g_sim);
                        if (cpu) { g_prev_cpu = *cpu; g_prev_cpu_valid = true; }
                        sim_step(g_sim, 1);
                        g_last_write_count = sim_get_last_writes(g_sim, g_last_writes, 256);
                        update_watches();
                    }
                    if (ImGui::MenuItem("Run",   "F5"))  g_running = true;
                    if (ImGui::MenuItem("Pause", "F6"))  g_running = false;
                    if (ImGui::MenuItem("Reset", "Ctrl+R")) {
                        g_running = false;
                        sim_reset(g_sim);
                        g_prev_cpu_valid   = false;
                        g_last_write_count = 0;
                        for (int i = 0; i < g_watch_count; i++) g_watches[i].valid = false;
                        update_watches();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Registers",   nullptr, &show_registers);
                    ImGui::MenuItem("Disassembly", nullptr, &show_disassembly);
                    ImGui::MenuItem("Memory",      nullptr, &show_memory);
                    ImGui::MenuItem("Console",     nullptr, &show_console);
                    ImGui::Separator();
                    ImGui::MenuItem("Breakpoints", nullptr, &show_breakpoints);
                    ImGui::MenuItem("Trace Log",   nullptr, &show_trace);
                    ImGui::MenuItem("Stack",       nullptr, &show_stack);
                    ImGui::MenuItem("Watch",       nullptr, &show_watches);
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Font Size")) {
                        static const int sizes[] = { 10, 11, 12, 13, 14, 15, 16, 18, 20, 24 };
                        for (int sz : sizes) {
                            char label[16];
                            snprintf(label, sizeof(label), "%d px", sz);
                            if (ImGui::MenuItem(label, nullptr, g_base_font_size == sz)) {
                                if (g_base_font_size != sz) {
                                    g_base_font_size = sz;
                                    g_font_rebuild   = true;
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            /* Toolbar */
            draw_toolbar();

            /* Reserve height for status bar */
            float sb_h = ImGui::GetFrameHeight()
                       + ImGui::GetStyle().ItemSpacing.y * 2.0f
                       + ImGui::GetStyle().SeparatorTextBorderSize + 2.0f;
            float ds_h = ImGui::GetContentRegionAvail().y - sb_h;
            if (ds_h < 0.0f) ds_h = 0.0f;

            /* Apply default layout on first run */
            const ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
            if (first_run && !layout_applied) {
                layout_applied = true;
                setup_default_layout(dockspace_id,
                    ImVec2(ImGui::GetContentRegionAvail().x, ds_h));
            }

            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, ds_h), ImGuiDockNodeFlags_None);

            /* Status bar */
            draw_statusbar();

            ImGui::End();
        }

        /* Phase 1+2 panes */
        draw_pane_registers();
        draw_pane_disassembly();
        draw_pane_memory();
        draw_pane_console();

        /* Phase 3 panes */
        draw_pane_breakpoints();
        draw_pane_trace();
        draw_pane_stack();
        draw_pane_watches();

        /* Render */
        ImGui::Render();
        int fb_w = 0, fb_h = 0;
        SDL_GL_GetDrawableSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.10f, 0.10f, 0.10f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    sim_destroy(g_sim);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
