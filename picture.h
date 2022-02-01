#ifndef picture_h
#define picture_h

#include <QtGui>

class Picture
{
public:
    Picture(const QImage &image = QImage());
    bool isValid() const;
    bool isNull() const;
    void clear();
    QSize size() const;
    int width() const;
    int height() const;
    QRect rect() const;
    void rotateLeft();
    void rotateRight();
    void draw(QPainter *p, const QRect &rect);
    const QImage &image() const;
    void setImage(const QImage &image);
private:
    struct Data {
        QImage image;
        int rotation { 0 };
    } d;
};

#endif
