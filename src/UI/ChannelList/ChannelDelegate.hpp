#pragma once

#include <QtWidgets>

#include "ChannelIndent.hpp"

class QAbstractProxyModel;

namespace Acheron {
namespace UI {
class ChannelDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChannelDelegate(QAbstractProxyModel *proxyModel = nullptr, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    void setIndentScopes(ChannelIndent::Scopes scopes);

private:
    QAbstractProxyModel *proxyModel;
    ChannelIndent::Scopes indentScopes;
};
} // namespace UI
} // namespace Acheron
