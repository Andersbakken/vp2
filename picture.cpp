#include "picture.h"

Picture::Picture(const QImage &image)
{
    d.image = image;
}

bool Picture::isValid() const
{
    return d.image.isNull();
}

bool Picture::isNull() const
{
    return d.image.isNull();
}

QSize Picture::size() const
{
    return QSize(width(), height());
}

QRect Picture::rect() const
{
    return QRect(QPoint(0, 0), size());
}

void Picture::rotateLeft()
{
    if (!d.image.isNull()) {
        QTransform transform;
        transform.rotate(-90);
        d.image = d.image.transformed(transform);
    }
}

void Picture::rotateRight()
{
    if (!d.image.isNull()) {
        QTransform transform;
        transform.rotate(90);
        d.image = d.image.transformed(transform);
    }
}

void Picture::draw(QPainter *p, const QRect &rect)
{
    p->drawImage(rect, d.image);
    p->drawRect(rect);
}

void Picture::clear()
{
    d.image = QImage();
}
const QImage & Picture::image() const
{
    return d.image;
}

int Picture::width() const
{
    return (d.rotation % 180 == 0 ? d.image.width() : d.image.height());
}

int Picture::height() const
{
    return (d.rotation % 180 == 0 ? d.image.height() : d.image.width());
}
void Picture::setImage(const QImage &image)
{
    d.image = image;
}
