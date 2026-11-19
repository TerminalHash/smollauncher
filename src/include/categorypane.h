#ifndef CATEGORYPANE_H
#define CATEGORYPANE_H

#include <QFrame>

// Plain frame that paints itself like a tab-widget pane, so pages without
// tabs (category editor, no selection) match the tabbed editor page.
class CategoryPane : public QFrame
{
public:
    explicit CategoryPane(QWidget *parent = nullptr);

    // Paint with another widget's palette so the pane fill matches it even
    // when the two widgets resolved different palette color groups.
    void setPaletteSource(const QWidget *source);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const QWidget *m_paletteSource = nullptr;
};

#endif // CATEGORYPANE_H
