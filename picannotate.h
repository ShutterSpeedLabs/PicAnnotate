#ifndef PICANNOTATE_H
#define PICANNOTATE_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class PicAnnotate;
}
QT_END_NAMESPACE

class PicAnnotate : public QMainWindow
{
    Q_OBJECT

public:
    PicAnnotate(QWidget *parent = nullptr);
    ~PicAnnotate();

private:
    Ui::PicAnnotate *ui;
};
#endif // PICANNOTATE_H
