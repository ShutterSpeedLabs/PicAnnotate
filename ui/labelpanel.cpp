#include "labelpanel.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

LabelPanel::LabelPanel(QWidget *parent)
    : QWidget(parent)
    , m_listWidget(new QListWidget(this))
{
    auto *addButton = new QPushButton("Add Class", this);
    auto *removeButton = new QPushButton("Remove Class", this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_listWidget);
    layout->addWidget(addButton);
    layout->addWidget(removeButton);

    connect(addButton, &QPushButton::clicked, this, &LabelPanel::onAddClicked);
    connect(removeButton, &QPushButton::clicked, this, &LabelPanel::onRemoveClicked);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &LabelPanel::onSelectionChanged);
}

void LabelPanel::setLabelSchema(LabelSchema *schema)
{
    m_schema = schema;
    refresh();
}

void LabelPanel::refresh()
{
    m_listWidget->clear();
    if (!m_schema)
        return;

    for (const LabelClass &label : m_schema->classes()) {
        auto *item = new QListWidgetItem(label.name);
        item->setData(Qt::UserRole, label.id);
        item->setForeground(label.color);
        m_listWidget->addItem(item);
    }

    if (m_listWidget->count() > 0 && m_listWidget->currentRow() < 0)
        m_listWidget->setCurrentRow(0);
}

void LabelPanel::onAddClicked()
{
    if (!m_schema)
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, "Add Class", "Class name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const int id = m_schema->addClass(
        name.trimmed(), LabelSchema::suggestedColorForIndex(m_schema->classes().size()));
    refresh();
    emit labelsChanged();

    for (int i = 0; i < m_listWidget->count(); ++i) {
        if (m_listWidget->item(i)->data(Qt::UserRole).toInt() == id) {
            m_listWidget->setCurrentRow(i);
            break;
        }
    }
}

void LabelPanel::onRemoveClicked()
{
    if (!m_schema || m_listWidget->currentRow() < 0)
        return;

    emit removeClassRequested(m_listWidget->currentItem()->data(Qt::UserRole).toInt());
}

void LabelPanel::onSelectionChanged()
{
    emit currentLabelChanged(currentLabelId());
}

int LabelPanel::currentLabelId() const
{
    if (!m_listWidget->currentItem())
        return -1;
    return m_listWidget->currentItem()->data(Qt::UserRole).toInt();
}
