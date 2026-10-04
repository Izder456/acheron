#pragma once

#include <QFrame>

class QLabel;
class QPushButton;
class QTimer;

namespace Acheron {
namespace UI {

class UnsavedChangesBar : public QFrame
{
    Q_OBJECT
public:
    explicit UnsavedChangesBar(QWidget *parent = nullptr);

    void setSaving(bool saving);
    void showError(const QString &error);
    void flash();

signals:
    void resetClicked();
    void saveClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setMessage(const QString &text, bool isError);

    QLabel *messageLabel;
    QPushButton *resetButton;
    QPushButton *saveButton;
    QTimer *flashTimer;
    bool flashing = false;
};

} // namespace UI
} // namespace Acheron
