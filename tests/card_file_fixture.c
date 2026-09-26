/*
 * card_file_fixture.c - Console memory-card filesystem fixture
 *
 * Implementation notes:
 *
 * Run only with a private disposable card image: this fixture writes
 * logical slot 01 to exercise BIOS-backed file operations.
 */

#include "value_api.h"

#include "storage/storage.h"

#include <psxapi.h>
#include <psxetc.h>
#include <stdint.h>
#include <stdio.h>

static Storage storage;
static Score score;

static void report(const char* stage, StorageResult result)
{
    static char line[96];
    snprintf(line, sizeof(line), "CARD_FILE stage=%s result=%d free=%d\n",
             stage, result, storage.free_blocks);
    *(const char* volatile*)0x1f802084 = line;
}

static void finish(int code)
{
    *(const char* volatile*)0x1f802084 =
        code ? "CARD_FILE FAILED\n" : "CARD_FILE COMPLETE\n";
    *(volatile short*)0x1f802082 = code;
    for (;;)
    {
    }
}

int main(void)
{
    // The BIOS fixture has no audio or custom pad initialization. Timer 2 is
    // the backend's polling deadline source and has no interrupt enabled.
    TIMER_CTRL(2) = 0x0200;
    TIMER_VALUE(2) = 0;
    score_init(&score);
    test_storage_init(&storage, card_platform_backend());

    StorageResult result = storage_refresh(&storage, 1);
    report("refresh", result);
    if (result != STORAGE_EMPTY && result != STORAGE_SAVED) finish(1);

    result = storage_save(&storage, 1, &score);
    report("save", result);
    if (result != STORAGE_SAVED) finish(2);

    for (int i = 0; i < 12; i++)
    {
        result = storage_load(&storage, 1);
        if (result != STORAGE_SAVED || storage.incoming.bpm != score.bpm)
        {
            report("load", result);
            finish(3);
        }
    }

    report("load-12", result);
    finish(0);
}
