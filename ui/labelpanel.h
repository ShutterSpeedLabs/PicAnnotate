#ifndef LABELPANEL_H
#define LABELPANEL_H

#include <QWidget>

#include "../core/labelschema.h"

class QListWidget;

class LabelPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LabelPanel(QWidget *parent = nullptr);

    void setLabelSchema(LabelSchema *schema);
    void refresh();

    int currentLabelId() const;

signals:
    void labelsChanged();
    void currentLabelChanged(int labelId);

    // Removal is a request, not something this panel performs. Deleting a class
    // also deletes every shape and track using it, which has to be confirmed and
    // has to go through the undo stack.
    void removeClassRequested(int labelId);

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onSelectionChanged();

private:
    LabelSchema *m_schema = nullptr;
    QListWidget *m_listWidget;
};

#endif // LABELPANEL_H
