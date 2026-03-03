/*
 * sim6502-gui — Phase 1
 *
 * Main entry point for the GUI binary.  Integrates with the simulator core
 * via the sim_api.h library interface (sim_session_t *).
 *
 * Phase 1 features:
 *   - Toolbar: Load (filename input), Step, Run/Pause, Reset, Processor selector
 *   - Status bar: state / PC+registers / filename
 *   - Registers pane: live CPU data, colour-coded flags
 *   - Disassembly pane: follow-PC disassembly
 *   - Memory pane: hex dump with address navigation
 *   - Console pane: session information
 *   - Keyboard shortcuts: F5 run, F6/Esc pause, F7 step, Ctrl+R reset, Ctrl+L focus load
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
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "cpu.h"
#include "sim_api.h"
}

/* --------------------------------------------------------------------------
 * Globals
 * -------------------------------------------------------------------------- */
static sim_session_t *g_sim        = NULL;
static bool           g_running    = false;   /* true = run mode (call sim_step each frame) */
static int            g_speed      = 5000;    /* instructions per frame when running */

/* Font / DPI state */
static float g_ui_scale      = 1.0f;  /* set once from detect_ui_scale() */
static int   g_base_font_size = 13;   /* logical font size in pixels (before scale) */
static bool  g_font_rebuild   = false;

/* SDL window handle — accessible from the INI write callback */
static SDL_Window *g_window = nullptr;

/* Toolbar state */
static char g_filename_buf[512]    = "";
static bool g_focus_filename       = false;   /* Ctrl+L sets this */

static const char *g_proc_labels[] = { "6502", "6502-undoc", "65C02", "65CE02", "45GS02" };
static const char *g_proc_ids[]    = { "6502", "6502-undoc", "65c02", "65ce02", "45gs02" };
static int         g_proc_idx      = 0;

/* Pane visibility */
static bool show_registers   = true;
static bool show_disassembly = true;
static bool show_memory      = true;
static bool show_console     = true;

/* --------------------------------------------------------------------------
 * Font rebuild
 *
 * Clears the font atlas, re-adds the default font at the current base size
 * (multiplied by the DPI scale factor), and re-uploads the GL texture.
 * Must be called outside an ImGui frame (before ImGui_ImplOpenGL3_NewFrame).
 * -------------------------------------------------------------------------- */
static void rebuild_font(void)
{
    /* This backend sets ImGuiBackendFlags_RendererHasTextures.
     * Clearing and rebuilding the atlas marks the old texture for deletion
     * and queues the new one; ImGui_ImplOpenGL3_RenderDrawData handles the
     * actual GL upload/teardown automatically on the next render call.    */
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig font_cfg;
    font_cfg.SizePixels = floorf((float)g_base_font_size * g_ui_scale);
    io.Fonts->AddFontDefault(&font_cfg);
    io.Fonts->Build();
}

/* --------------------------------------------------------------------------
 * UI scale detection
 *
 * Priority: SIM6502_SCALE > GDK_SCALE > QT_SCALE_FACTOR > SDL DPI > resolution.
 * Result is snapped to 0.25 steps for crisp rasterisation.
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
 * Toolbar
 * -------------------------------------------------------------------------- */
static void do_load(void)
{
    /* Attempt to load the file path in g_filename_buf */
    if (g_filename_buf[0] == '\0') return;
    g_running = false;
    if (sim_load_asm(g_sim, g_filename_buf) != 0)
        fprintf(stderr, "sim6502-gui: failed to load '%s'\n", g_filename_buf);
    else
        fprintf(stdout, "sim6502-gui: loaded '%s'\n", g_filename_buf);
}

static void draw_toolbar(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

    /* Filename input — takes most of the row width */
    float btn_w   = 60.0f;          /* approximate button width */
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
    if (ImGui::Button("Load") || enter_pressed)
        do_load();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    /* Step (F7) */
    bool can_step = (sim_get_state(g_sim) == SIM_READY ||
                     sim_get_state(g_sim) == SIM_PAUSED);
    if (!can_step) ImGui::BeginDisabled();
    if (ImGui::Button("Step"))
        sim_step(g_sim, 1);
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

    /* Left: execution state */
    if (g_running)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "RUNNING");
    else {
        const char *sname = sim_state_name(state);
        if (state == SIM_IDLE)     ImGui::TextDisabled("%s", sname);
        else if (state == SIM_FINISHED) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", sname);
        else                            ImGui::Text("%s", sname);
    }

    ImGui::SameLine(0, 20);

    /* Centre: register snapshot */
    if (state != SIM_IDLE && cpu) {
        ImGui::Text("PC=%04X  A=%02X  X=%02X  Y=%02X  S=%04X  Cycles=%lu",
                    cpu->pc, cpu->a, cpu->x, cpu->y, cpu->s, cpu->cycles);
    } else {
        ImGui::TextDisabled("PC=----  A=--  X=--  Y=--  S=----  Cycles=0");
    }

    /* Right: filename — right-aligned */
    const char *fname = sim_get_filename(g_sim);
    float fname_w = ImGui::CalcTextSize(fname).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - fname_w);
    ImGui::TextDisabled("%s", fname);

    ImGui::PopStyleVar();
}

/* --------------------------------------------------------------------------
 * Pane: Registers
 * -------------------------------------------------------------------------- */
static void draw_pane_registers(void)
{
    if (!show_registers) return;
    ImGui::Begin("Registers", &show_registers);

    cpu_t *cpu = sim_get_cpu(g_sim);
    sim_state_t state = sim_get_state(g_sim);

    if (state == SIM_IDLE || !cpu) {
        ImGui::TextDisabled("(no program loaded)");
        ImGui::End();
        return;
    }

    bool is_45gs02 = (sim_get_cpu_type(g_sim) == CPU_45GS02);

    ImGui::Text("A  %02X    X  %02X    Y  %02X", cpu->a, cpu->x, cpu->y);
    if (is_45gs02)
        ImGui::Text("Z  %02X    B  %02X", cpu->z, cpu->b);
    ImGui::Separator();
    if (is_45gs02)
        ImGui::Text("S  %04X   PC %04X", cpu->s, cpu->pc);
    else
        ImGui::Text("S  %04X   PC %04X", (unsigned)(0x0100 | (cpu->s & 0xFF)), cpu->pc);
    ImGui::Separator();

    /* Status flags — green when set, dimmed when clear */
    struct { const char *label; unsigned char mask; } flags[] = {
        {"N", FLAG_N}, {"V", FLAG_V}, {"-", FLAG_U},
        {"B", FLAG_B}, {"D", FLAG_D}, {"I", FLAG_I},
        {"Z", FLAG_Z}, {"C", FLAG_C},
    };
    for (int i = 0; i < 8; i++) {
        bool set = (cpu->p & flags[i].mask) != 0;
        if (set)
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", flags[i].label);
        else
            ImGui::TextDisabled("%s", flags[i].label);
        if (i < 7) ImGui::SameLine();
    }

    ImGui::Separator();
    ImGui::Text("Cycles  %lu", cpu->cycles);
    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Disassembly
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

    cpu_t *cpu   = sim_get_cpu(g_sim);
    uint16_t pc  = cpu ? cpu->pc : 0;
    uint16_t addr = pc;
    char buf[128];

    /* Show 24 instructions starting from PC (follow-PC mode for Phase 1) */
    for (int i = 0; i < 24; i++) {
        bool is_pc = (addr == pc) && (i == 0);
        int consumed = sim_disassemble_one(g_sim, addr, buf, sizeof(buf));

        if (is_pc) {
            /* Highlight current instruction row */
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("> %s", buf);
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("  %s", buf);
        }

        if (consumed < 1) consumed = 1;
        addr = (uint16_t)(addr + consumed);
    }

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Memory View
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

    static uint16_t g_mem_addr = 0x0200;
    static char     g_addr_buf[8] = "0200";

    /* Address navigation bar */
    ImGui::SetNextItemWidth(70);
    if (ImGui::InputText("##memaddr", g_addr_buf, sizeof(g_addr_buf),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        g_mem_addr = (uint16_t)strtol(g_addr_buf, NULL, 16);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("PC")) {
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (cpu) { g_mem_addr = cpu->pc; snprintf(g_addr_buf, sizeof(g_addr_buf), "%04X", g_mem_addr); }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("SP")) {
        cpu_t *cpu = sim_get_cpu(g_sim);
        if (cpu) { g_mem_addr = (uint16_t)(0x0100 | (cpu->s & 0xFF)); snprintf(g_addr_buf, sizeof(g_addr_buf), "%04X", g_mem_addr); }
    }
    ImGui::Separator();

    /* Hex dump — 16 bytes per row, 16 rows */
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    for (int row = 0; row < 16; row++) {
        uint16_t raddr = (uint16_t)(g_mem_addr + row * 16);
        ImGui::Text("%04X ", raddr);
        for (int col = 0; col < 16; col++) {
            ImGui::SameLine(0, col == 8 ? 8 : 4);
            uint8_t v = sim_mem_read_byte(g_sim, (uint16_t)(raddr + col));
            ImGui::Text("%02X", v);
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
    ImGui::PopFont();

    ImGui::End();
}

/* --------------------------------------------------------------------------
 * Pane: Console
 * -------------------------------------------------------------------------- */
static void draw_pane_console(void)
{
    if (!show_console) return;
    ImGui::Begin("Console", &show_console);

    sim_state_t state = sim_get_state(g_sim);
    if (state == SIM_IDLE) {
        ImGui::TextDisabled("sim6502-gui ready.");
        ImGui::TextDisabled("Enter a .asm file path in the toolbar and click Load.");
        ImGui::End();
        return;
    }

    cpu_t *cpu = sim_get_cpu(g_sim);
    ImGui::Text("Loaded: %s", sim_get_filename(g_sim));
    ImGui::Text("Processor: %s", sim_processor_name(g_sim));
    ImGui::Text("State: %s", sim_state_name(state));
    ImGui::Separator();
    if (cpu) {
        ImGui::Text("PC=$%04X  A=%02X  X=%02X  Y=%02X  S=%04X  P=%02X",
                    cpu->pc, cpu->a, cpu->x, cpu->y, cpu->s, cpu->p);
        ImGui::Text("Cycles: %lu", cpu->cycles);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Full CLI console pane: Phase 2");

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

    /* Pre-parse INI before SDL/ImGui initialisation so font size and window
     * geometry are correct from the very first frame — no rebuild flash.   */
    bool first_run;
    int  pre_win_x = -32768, pre_win_y = -32768; /* sentinel = not saved   */
    int  pre_win_w = 0,      pre_win_h = 0;       /* 0 = use default 80%   */
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

    /* Use saved size if valid, else 80 % of usable display area */
    const int win_w = (pre_win_w > 0) ? pre_win_w : ((int)(usable.w * 0.80f) / 2) * 2;
    const int win_h = (pre_win_h > 0) ? pre_win_h : ((int)(usable.h * 0.80f) / 2) * 2;
    /* Use saved position if both axes were stored, else centre */
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

    /* Register custom INI handler for [sim6502][Settings].
     * ImGui calls WriteAllFn automatically when saving the INI (periodically
     * and on shutdown).  ReadLineFn only triggers a font rebuild when the
     * stored size differs from the already-pre-parsed g_base_font_size.    */
    {
        ImGuiSettingsHandler h = {};
        h.TypeName   = "sim6502";
        h.TypeHash   = ImHashStr("sim6502");
        h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* {
            return (void*)(intptr_t)1;   /* non-null = accept this section */
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

    /* Create simulator session (default: 6502) */
    g_sim = sim_create("6502");
    if (!g_sim) {
        fprintf(stderr, "sim6502-gui: failed to create sim session\n");
        return 1;
    }

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
                if (sim_get_state(g_sim) == SIM_READY || sim_get_state(g_sim) == SIM_PAUSED)
                    g_running = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F6) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                g_running = false;
            if (ImGui::IsKeyPressed(ImGuiKey_F7)) {
                g_running = false;
                if (sim_get_state(g_sim) == SIM_READY || sim_get_state(g_sim) == SIM_PAUSED)
                    sim_step(g_sim, 1);
            }
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                    g_running = false;
                    sim_reset(g_sim);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_L))
                    g_focus_filename = true;
            }
        }

        /* ---- Per-frame simulation step (run mode) ---- */
        if (g_running) {
            int ev_code = sim_step(g_sim, g_speed);
            if (ev_code > 0)
                g_running = false;   /* hit BRK, STP, or breakpoint */
            else if (ev_code < 0)
                g_running = false;   /* wrong state */
        }

        /* ---- Font rebuild (must happen before NewFrame) ---- */
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
                ImGuiWindowFlags_MenuBar              |
                ImGuiWindowFlags_NoDocking            |
                ImGuiWindowFlags_NoTitleBar           |
                ImGuiWindowFlags_NoCollapse           |
                ImGuiWindowFlags_NoResize             |
                ImGuiWindowFlags_NoMove               |
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
                    if (ImGui::MenuItem("Step",  "F7"))  { g_running = false; sim_step(g_sim, 1); }
                    if (ImGui::MenuItem("Run",   "F5"))  g_running = true;
                    if (ImGui::MenuItem("Pause", "F6"))  g_running = false;
                    if (ImGui::MenuItem("Reset", "Ctrl+R")) { g_running = false; sim_reset(g_sim); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Registers",   nullptr, &show_registers);
                    ImGui::MenuItem("Disassembly", nullptr, &show_disassembly);
                    ImGui::MenuItem("Memory",      nullptr, &show_memory);
                    ImGui::MenuItem("Console",     nullptr, &show_console);
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

            /* Default layout — applied once on first run */
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

        /* Panes */
        draw_pane_registers();
        draw_pane_disassembly();
        draw_pane_memory();
        draw_pane_console();

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
