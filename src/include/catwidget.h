#ifndef CATWIDGET_H
#define CATWIDGET_H

#include <QPixmap>
#include <QWidget>

// Easter egg image: fixed height, stretched horizontally to the width the
// window provides (non-uniform scaling); centered by the surrounding layout.
class CatWidget : public QWidget
{
public:
    explicit CatWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_pixmap;
};

#endif // CATWIDGET_H
