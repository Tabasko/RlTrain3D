#include <stdio.h>
#include "saveload.h"
#include "track.h"
#include "train.h"
#include "props.h"
#include "../events/event_bus.h"
#include "../state/game_state.h"
#include "raylib.h"

#define SAVE_FILE    "savegame.rlt"
#define SAVE_VERSION 3

void SaveLoadUpdate(void) {
    if (gs.events.has(EVENT_FILE_SAVE)) {
        FILE *f = fopen(SAVE_FILE, "w");
        if (!f) {
            TraceLog(LOG_ERROR, "SAVE: cannot open %s for writing", SAVE_FILE);
            return;
        }
        fprintf(f, "VERSION %d\n\n", SAVE_VERSION);
        TrackSystemSave(f);
        fprintf(f, "\n");
        PropsSave(f);
        fprintf(f, "\n");
        TrainSystemSave(f);
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
        TrackSystemLoad(f);
        PropsLoad(f);
        TrainSystemLoad(f);
        fclose(f);
        TraceLog(LOG_INFO, "SAVE: loaded %s", SAVE_FILE);
    }
}
