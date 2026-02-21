#ifndef LCDWIDGET_H
#define LCDWIDGET_H

#include <QGraphicsView>
#include <QKeyEvent>

class LCDWidget : public QWidget
{
    Q_OBJECT

public:
    LCDWidget(QWidget *parent, Qt::WindowFlags f = Qt::WindowFlags());

public slots:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

protected:
    virtual void paintEvent(QPaintEvent *) override;
    virtual void resizeEvent(QResizeEvent *) override;

signals:
    void closed();
    void scaleChanged(int percent);

};

#endif // LCDWIDGET_H
