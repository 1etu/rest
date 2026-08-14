#include "app/app.h"
#include "core/config.h"
#include "notify/notify.h"
#include "notify/card.h"
#include "notify/overlay.h"

struct notifier {
    const wchar_t *id;
    void (*set)(int on);
};

static const notifier notifiers[] = {
    {L"overlay", overlay_break},
    {L"notification", card_notify},
    {L"popup", card_popup},
    {L"cursor", card_cursor},
};

#define N ((int)(sizeof notifiers / sizeof notifiers[0]))

static const notifier *pick(void)
{
    for (int i = 0; i < N; i++)
        if (!lstrcmpiW(cfg.style, notifiers[i].id))
            return &notifiers[i];
    return &notifiers[0];
}

const wchar_t *const *notify_styles(int *count)
{
    static const wchar_t *ids[N];
    for (int i = 0; i < N; i++)
        ids[i] = notifiers[i].id;
    *count = N;
    return ids;
}

void notify_break(int on)
{
    static const notifier *active;
    if (on) {
        active = pick();
        active->set(1);
        return;
    }
    if (active) {
        active->set(0);
        active = 0;
    }
}
