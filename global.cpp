#include "global.h"

QString GATE_URL_PERFIX = "";

inline std::function<void(QWidget *)> repolish = [](QWidget * w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
};