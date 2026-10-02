#include "PictureBubble.h"

#include <QLabel>
#include <qlayout.h>

PictureBubble::PictureBubble(const ChatRole role, const QPixmap& picture, QWidget* parent)
    : BubbleFrame(role, parent)
{
    auto* label = new QLabel();
    label->setScaledContents(true);
    const auto pix = picture.scaled(QSize(PICTURE_MAX_WIDTH, PICTURE_MAX_HEIGHT), Qt::KeepAspectRatio);
    label->setPixmap(pix);
    setWidget(label);

    const int margin_lift = layout()->contentsMargins().left();
    const int margin_right = layout()->contentsMargins().right();
    const int v_margin = layout()->contentsMargins().bottom();
    setFixedSize(pix.width() + margin_lift + margin_right, pix.height() + v_margin * 2);
}
