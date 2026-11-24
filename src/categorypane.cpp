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
    if (m_paletteSource) {
        QPalette pal = m_paletteSource->palette();
        // The source may be hidden or disabled, so its palette can carry a
        // stale current color group (e.g. Disabled while this pane is shown).
        // These panes are display-only: use the window's active/inactive
        // state, never the Disabled group, to match a real tab widget.
        pal.setCurrentColorGroup((opt.state & QStyle::State_Active)
                                     ? QPalette::Active
                                     : QPalette::Inactive);
        opt.palette = pal;
    }
    opt.rect = rect();
    opt.tabBarRect = QRect();
    opt.selectedTabRect = QRect();
    opt.lineWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth,
                                         nullptr, this);
    opt.midLineWidth = 0;

    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_FrameTabWidget, &opt, &p, this);
}
