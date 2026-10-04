#include "UnsavedChangesBar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>

#include "Core/Theme/Manager.hpp"

namespace Acheron {
namespace UI {

using Core::Theme::Manager;
using Core::Theme::Token;

UnsavedChangesBar::UnsavedChangesBar(QWidget *parent)
    : QFrame(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 8, 8);
    layout->setSpacing(8);

    messageLabel = new QLabel(this);
    messageLabel->setWordWrap(true);
    layout->addWidget(messageLabel, 1);

    resetButton = new QPushButton(tr("Reset"), this);
    resetButton->setFlat(true);
    resetButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(resetButton);

    saveButton = new QPushButton(tr("Save Changes"), this);
    saveButton->setDefault(true);
    saveButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(saveButton);

    flashTimer = new QTimer(this);
    flashTimer->setSingleShot(true);
    flashTimer->setInterval(1200);
    connect(flashTimer, &QTimer::timeout, this, [this]() {
        flashing = false;
        update();
    });

    connect(resetButton, &QPushButton::clicked, this, &UnsavedChangesBar::resetClicked);
    connect(saveButton, &QPushButton::clicked, this, &UnsavedChangesBar::saveClicked);

    setMessage(tr("Careful — you have unsaved changes!"), false);
}

void UnsavedChangesBar::setSaving(bool saving)
{
    resetButton->setEnabled(!saving);
    saveButton->setEnabled(!saving);
    saveButton->setText(saving ? tr("Saving…") : tr("Save Changes"));
    if (saving)
        setMessage(tr("Careful — you have unsaved changes!"), false);
}

void UnsavedChangesBar::showError(const QString &error)
{
    setSaving(false);
    setMessage(error, true);
}

void UnsavedChangesBar::flash()
{
    flashing = true;
    flashTimer->start();
    update();
}

void UnsavedChangesBar::setMessage(const QString &text, bool isError)
{
    if (!isError) {
        messageLabel->setTextFormat(Qt::PlainText);
        messageLabel->setText(text);
        return;
    }

    messageLabel->setTextFormat(Qt::RichText);
    messageLabel->setText(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(Manager::instance().color(Token::ChatError).name(), text.toHtmlEscaped()));
}

void UnsavedChangesBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor background = flashing ? Manager::instance().color(Token::ChatError) : palette().color(QPalette::AlternateBase);
    if (flashing)
        background.setAlpha(90);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    painter.fillPath(path, background);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawPath(path);
}

void UnsavedChangesBar::hideEvent(QHideEvent *event)
{
    setMessage(tr("Careful — you have unsaved changes!"), false);
    QFrame::hideEvent(event);
}

} // namespace UI
} // namespace Acheron
