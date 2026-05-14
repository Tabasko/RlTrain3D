CXX          = g++
CXXFLAGS     = -Wall -Wextra -O2 -std=c++17 -Isrc -MMD -MP
CXXFLAGS_DBG = -Wall -Wextra -O0 -g3 -std=c++17 -Isrc -MMD -MP
LIBS         = $(shell pkg-config --libs --cflags raylib) -lm
TARGET       = train3d
TARGET_DBG   = train3d_dbg
SRC          = $(wildcard src/*.cpp) $(wildcard src/**/*.cpp)
OBJ          = $(patsubst src/%.cpp, build/%.o,     $(SRC))
OBJ_DBG      = $(patsubst src/%.cpp, build/dbg/%.o, $(SRC))
DEP          = $(OBJ:.o=.d)
DEP_DBG      = $(OBJ_DBG:.o=.d)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LIBS)

$(TARGET_DBG): $(OBJ_DBG)
	$(CXX) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp | build
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/dbg/%.o: src/%.cpp | build
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_DBG) -c -o $@ $<

build:
	mkdir -p build

-include $(DEP)
-include $(DEP_DBG)

run: $(TARGET)
	./$(TARGET)

debug: $(TARGET_DBG)
	gdb ./$(TARGET_DBG)

clean:
	rm -rf build $(TARGET) $(TARGET_DBG)

compdb:
	$(MAKE) clean && bear -- $(MAKE)

.PHONY: run debug clean compdb
