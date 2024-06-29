#ifndef PICANNOTATE_H
#define PICANNOTATE_H

#include <QGraphicsView>
#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QDebug>
#include <QGraphicsPixmapItem>
#include <QMouseEvent>
#include <QStandardItemModel>

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
    QPointF startPoint;
    QPointF endPoint;
    bool isDrawing;
    int currentClassId;

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
    void updateAnnotations();
    void on_listViewClass_clicked(const QModelIndex &index);
    bool eventFilter(QObject *obj, QEvent *event) override;
protected:
    void onMousePressed(QPointF point);
    void onMouseMoved(QPointF point);
    void onMouseReleased(QPointF point);

private:
    void drawTemporaryRect();
    void finalizeBoundingBox();
private:
    Ui::PicAnnotate *ui;
    QMap<int, QColor> classColorMap;
    struct BoundingBox {
        int classId;
        QRectF rect;
    };
    QList<BoundingBox> boundingBoxes;
    void deleteBoundingBox(const QPointF &clickPoint);
    void saveBoundingBoxes();
};


#endif // PICANNOTATE_H
