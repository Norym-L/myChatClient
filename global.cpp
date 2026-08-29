#include "global.h"

extern std::function<void(QWidget *)> repolish = [](QWidget * w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
};