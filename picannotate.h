#ifndef PICANNOTATE_H
#define PICANNOTATE_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QDebug>

QT_BEGIN_NAMESPACE
namespace Ui {
class PicAnnotate;
}
QT_END_NAMESPACE

class PicAnnotate : public QMainWindow
{
    Q_OBJECT
public:
    QGraphicsPixmapItem *imageItem = new QGraphicsPixmapItem;
    QGraphicsScene *scene = new QGraphicsScene;
    QPixmap pixmap;
    //QPixmap pixmapScale;

    QPixmap originalPixmap;
    QPixmap annotatedPixmap;
    void updateAnnotatedPixmap();
    void updateView();

public:
    PicAnnotate(QWidget *parent = nullptr);
    ~PicAnnotate();

private slots:
    void on_actionOpen_Folder_triggered();

    void on_actionOpen_Image_triggered();

    void on_next_button_clicked();

    void on_pre_button_clicked();

    void on_actionExit_triggered();

    void on_listViewFiles_clicked(const QModelIndex &index);

    void on_ZoomIn_clicked();

    void on_ZoomOut_clicked();

    void on_actionRead_YAML_triggered();
    void loadImageAndAnnotations(int index);
    void initializeColorMap();
    void updateViewZoom();

private:
    Ui::PicAnnotate *ui;
    QMap<int, QColor> classColorMap;
};
#endif // PICANNOTATE_H
