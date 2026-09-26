/*
 * ui_format.c - C formatting for editor menu values
 *
 * Implementation notes:
 *
 * The renderer uses C formatting for settings and context rows.
 */

#include "ui/ui_format.h"

#include <stdio.h>

/*
 * Setting row IDs live above context-action IDs so the shared value editor
 * can dispatch either menu without ambiguous cases.
 */
enum
{
    ROW_BPM = 100,                  // Global rows begin at 100.
    ROW_REVERB,                     // Opens the reverb settings.
    ROW_SLOT,                       // Selects a card slot.
    ROW_CHECK,                      // Refreshes card status.
    ROW_SAVE,                       // Requests a score save.
    ROW_LOAD,                       // Requests a score load.
    ROW_SIZE,                       // Adjusts reverb room size.
    ROW_AMOUNT,                     // Adjusts reverb wet level.
    ROW_WAVE1 = 200,                // Sound rows begin at 200.
    ROW_WAVE2,                      // Selects the second wave.
    ROW_AMP_ATTACK,                 // Adjusts amplitude attack.
    ROW_AMP_RELEASE,                // Adjusts amplitude release.
    ROW_MIX_ATTACK,                 // Adjusts wave-mix attack.
    ROW_MIX_RELEASE,                // Adjusts wave-mix release.
    ROW_SWEEP,                      // Adjusts pitch sweep depth.
    ROW_DECAY,                      // Adjusts sweep duration.
    ROW_SEND                        // Adjusts reverb send.
};

void ui_format_row_value(const Editor* e, int id, char* b, int size)
{
    const SoundSettings* s = &e->score.sounds[e->sound_channel];
    TileValue v = e->target ? e->score.tiles[e->target].value : e->value;
    b[0] = 0;
    switch (id)
    {
    case ROW_BPM:
        snprintf(b, size, "%d", e->score.bpm);
        break;
    case ROW_SLOT:
        snprintf(b, size, "%02d", e->storage_slot);
        break;
    case ROW_SAVE:
        snprintf(b, size, "FREE %d B", e->free_bytes);
        break;
    case ROW_SIZE:
        snprintf(b, size, "%s", score_reverb_size(e->score.reverb.size));
        break;
    case ROW_AMOUNT:
        snprintf(b, size, "%d%%", e->score.reverb.amount);
        break;
    case ROW_WAVE1:
        snprintf(b, size, "%s", score_wave_name(s->wave_a));
        break;
    case ROW_WAVE2:
        snprintf(b, size, "%s", score_wave_name(s->wave_b));
        break;
    case ROW_AMP_ATTACK:
        snprintf(b, size, "%d MS", s->attack);
        break;
    case ROW_AMP_RELEASE:
        snprintf(b, size, "%d MS", s->release);
        break;
    case ROW_MIX_ATTACK:
        snprintf(b, size, "%d MS", s->mix_attack);
        break;
    case ROW_MIX_RELEASE:
        snprintf(b, size, "%d MS", s->mix_release);
        break;
    case ROW_SWEEP:
        snprintf(b, size, "%+d ST", s->sweep);
        break;
    case ROW_DECAY:
        snprintf(b, size, "%d MS", s->decay);
        break;
    case ROW_SEND:
        snprintf(b, size, "%s", s->reverb ? "ON" : "OFF");
        break;
    case ACTION_LENGTH:
        snprintf(b, size, "%d", e->score.lanes[e->lane].length);
        break;
    case ACTION_DIVISION:
        snprintf(b, size, "1/%d", score_division(&e->score, e->lane));
        break;
    case ACTION_CHANNEL:
        snprintf(b, size, "%d", score_channel(&e->score, e->lane) + 1);
        break;
    case ACTION_PITCH:
        snprintf(b, size, "%s%d", score_note_name(v.pitch), v.pitch / 12);
        break;
    case ACTION_DURATION:
        snprintf(b, size, "%d.%02d", v.length / 20, v.length % 20 * 5);
        break;
    case ACTION_PERIOD:
        snprintf(b, size, "%d", v.period);
        break;
    case ACTION_CHANCE:
        snprintf(b, size, "%d%%", v.chance);
        break;
    case ACTION_LOCK_ATTACK_ENABLE:
        snprintf(b, size, "%s", v.lock_mask & LOCK_ATTACK ? "ON" : "OFF");
        break;
    case ACTION_LOCK_RELEASE_ENABLE:
        snprintf(b, size, "%s", v.lock_mask & LOCK_RELEASE ? "ON" : "OFF");
        break;
    case ACTION_LOCK_ATTACK:
        snprintf(b, size, "%+d MS", v.attack);
        break;
    case ACTION_LOCK_RELEASE:
        snprintf(b, size, "%+d MS", v.release);
        break;
    default:
        break;
    }
}
