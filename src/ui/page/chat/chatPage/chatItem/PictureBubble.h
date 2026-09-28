#ifndef CHATCLIENT_PICTUREBUBBLE_H
#define CHATCLIENT_PICTUREBUBBLE_H

#include "bubbleframe.h"
#include "app/global.h"

class PictureBubble : public BubbleFrame
{
public:
    explicit PictureBubble(ChatRole role, const QPixmap& picture, QWidget* parent = nullptr);
};


#endif //CHATCLIENT_PICTUREBUBBLE_H
