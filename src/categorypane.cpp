#include "include/categorypane.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOption>

CategoryPane::CategoryPane(QWidget *parent)
    : QFrame(parent)
{
}

void CategoryPane::setPaletteSource(const QWidget *source)
{
    m_paletteSource = source;
    update();
}

void CategoryPane::paintEvent(QPaintEvent *)
{
    QStyleOptionTabWidgetFrame opt;
    opt.initFrom(this);
    if (m_paletteSource)
        opt.palette = m_paletteSource->palette();
    opt.rect = rect();
    opt.tabBarRect = QRect();
    opt.selectedTabRect = QRect();
    opt.lineWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth,
                                         nullptr, this);
    opt.midLineWidth = 0;

    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_FrameTabWidget, &opt, &p, this);
}
