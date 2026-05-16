#include <stdio.h>
#include "saveload.h"
#include "track.h"
#include "train.h"
#include "props.h"
#include "../events/event_bus.h"
#include "../state/game_state.h"
#include "raylib.h"

#define SAVE_FILE    "savegame.rlt"
#define SAVE_VERSION 4

void SaveLoadUpdate(void) {
    if (gs.events.has(EVENT_FILE_SAVE)) {
        FILE *f = fopen(SAVE_FILE, "w");
        if (!f) {
            TraceLog(LOG_ERROR, "SAVE: cannot open %s for writing", SAVE_FILE);
            return;
        }
        fprintf(f, "VERSION %d\n\n", SAVE_VERSION);
        track_system.Save(f);
        fprintf(f, "\n");
        props_system.Save(f);
        fprintf(f, "\n");
        train_system.Save(f);
        fclose(f);
        TraceLog(LOG_INFO, "SAVE: wrote %s", SAVE_FILE);
    }

    if (gs.events.has(EVENT_FILE_OPEN)) {
        FILE *f = fopen(SAVE_FILE, "r");
        if (!f) {
            TraceLog(LOG_WARNING, "SAVE: %s not found", SAVE_FILE);
            return;
        }
        int version = 0;
        if (fscanf(f, " VERSION %d", &version) != 1 || version != SAVE_VERSION) {
            TraceLog(LOG_ERROR, "SAVE: unsupported file version %d", version);
            fclose(f);
            return;
        }
        track_system.Load(f);
        props_system.Load(f);
        train_system.Load(f);
        fclose(f);
        TraceLog(LOG_INFO, "SAVE: loaded %s", SAVE_FILE);
    }
}
