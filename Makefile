CC       = gcc
CXX      = g++
CFLAGS   = -Wall -Wextra -O2 -I src
CXXFLAGS = -Wall -Wextra -O2 -I src

# ---------------------------------------------------------------------------
# CLI target: sim6502
# ---------------------------------------------------------------------------
SRCS   = src/sim6502.c \
         src/opcodes_6502.c src/opcodes_6502_undoc.c \
         src/opcodes_65c02.c src/opcodes_65ce02.c src/opcodes_45gs02.c
OBJS   = $(SRCS:.c=.o)
TARGET = sim6502

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# GUI target: sim6502-gui
#
# Dear ImGui (docking branch) is fetched automatically into gui/imgui/ on
# first `make gui` if it is not already present.  Requires: git, pkg-config,
# libsdl2-dev, libgl-dev (or equivalent mesa packages).
# ---------------------------------------------------------------------------
IMGUI_DIR  = gui/imgui
IMGUI_BACK = $(IMGUI_DIR)/backends

SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
ifeq ($(SDL2_CFLAGS),)
  SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL2_LIBS   := $(shell sdl2-config --libs   2>/dev/null)
endif

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  GL_LIBS = -framework OpenGL
else
  GL_LIBS = -lGL
endif

IMGUI_SRCS = \
	$(IMGUI_DIR)/imgui.cpp \
	$(IMGUI_DIR)/imgui_draw.cpp \
	$(IMGUI_DIR)/imgui_tables.cpp \
	$(IMGUI_DIR)/imgui_widgets.cpp \
	$(IMGUI_BACK)/imgui_impl_sdl2.cpp \
	$(IMGUI_BACK)/imgui_impl_opengl3.cpp
IMGUI_OBJS = $(IMGUI_SRCS:.cpp=.o)

GUI_CXXFLAGS = $(CXXFLAGS) $(SDL2_CFLAGS) -I $(IMGUI_DIR) -I $(IMGUI_BACK)
GUI_TARGET   = sim6502-gui

# sim6502.c compiled without main() for the GUI (exposes sim_api.h functions).
# -Wno-unused-* silences warnings about helpers only called from main().
GUI_SIM_OBJ = src/sim6502_lib.o
$(GUI_SIM_OBJ): src/sim6502.c src/sim_api.h
	$(CC) $(CFLAGS) -DSIM_LIBRARY_BUILD \
	    -Wno-unused-function -Wno-unused-variable -c -o $@ $<

# Opcode objects shared between CLI and GUI (no main(), no -DSIM_LIBRARY_BUILD needed)
OPCODE_OBJS = \
	src/opcodes_6502.o src/opcodes_6502_undoc.o \
	src/opcodes_65c02.o src/opcodes_65ce02.o src/opcodes_45gs02.o

# ImGui objects — compiled without -Wall/-Wextra to suppress upstream warnings.
# NOTE: $(IMGUI_BACK)/%.o must come first: GNU Make 3.81 lets % match '/',
# so gui/imgui/%.o would otherwise shadow the more-specific backends rule.
$(IMGUI_BACK)/%.o: $(IMGUI_BACK)/%.cpp
	$(CXX) -O2 $(SDL2_CFLAGS) -I $(IMGUI_DIR) -I $(IMGUI_BACK) -c -o $@ $<

$(IMGUI_DIR)/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) -O2 -I $(IMGUI_DIR) -I $(IMGUI_BACK) -c -o $@ $<

# GUI application object — depends on ImGui and sim_api.h being present
gui/main.o: gui/main.cpp $(IMGUI_DIR)/imgui.h src/sim_api.h
	$(CXX) $(GUI_CXXFLAGS) -c -o $@ $<

.PHONY: gui
gui: $(IMGUI_DIR)/imgui.h $(GUI_TARGET)

$(GUI_TARGET): gui/main.o $(GUI_SIM_OBJ) $(OPCODE_OBJS) $(IMGUI_OBJS)
	$(CXX) -o $@ $^ $(SDL2_LIBS) $(GL_LIBS)

# Auto-fetch ImGui (docking branch) if not present
$(IMGUI_DIR)/imgui.h:
	@echo "--- Dear ImGui not found; fetching docking branch ---"
	git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git $(IMGUI_DIR)

# ---------------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------------
clean:
	rm -f $(OBJS) $(TARGET) gui/main.o $(GUI_SIM_OBJ) $(IMGUI_OBJS) $(GUI_TARGET)

clean-gui:
	rm -f gui/main.o $(GUI_SIM_OBJ) $(IMGUI_OBJS) $(GUI_TARGET)

# Remove vendored ImGui source (re-fetched on next `make gui`)
clean-imgui:
	rm -rf $(IMGUI_DIR)

test: $(TARGET)
	./tools/run_tests.py

.PHONY: all clean clean-gui clean-imgui test
