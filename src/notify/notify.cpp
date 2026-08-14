#include "app/app.h"
#include "core/config.h"
#include "notify/notify.h"
#include "notify/popup.h"
#include "notify/overlay.h"

struct notifier {
    const wchar_t *id;
    void (*set)(int on);
};

static const notifier notifiers[] = {
    {L"popup", popup_break},
    {L"overlay", overlay_break},
};

void notify_break(int on)
{
    for (int i = 0; i < (int)(sizeof notifiers / sizeof notifiers[0]); i++)
        if (!lstrcmpiW(cfg.style, notifiers[i].id)) {
            notifiers[i].set(on);
            return;
        }
    overlay_break(on);
}
