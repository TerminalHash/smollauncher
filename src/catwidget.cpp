#include "include/catwidget.h"

#include <QPainter>

CatWidget::CatWidget(QWidget *parent)
    : QWidget(parent)
    , m_pixmap(QStringLiteral(":/icons/graphic/ebakot.png"))
{
    setObjectName(QStringLiteral("catWidget"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumWidth(1);
}

QSize CatWidget::sizeHint() const
{
    return m_pixmap.isNull() ? QSize(128, 128) : m_pixmap.size();
}

void CatWidget::paintEvent(QPaintEvent *)
{
    if (m_pixmap.isNull())
        return;

    // Widen the picture with the window
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(rect(), m_pixmap);
}
