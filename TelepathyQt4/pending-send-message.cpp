/**
 * This file is part of TelepathyQt4
 *
 * @copyright Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * @copyright Copyright (C) 2009 Nokia Corporation
 * @license LGPL 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <TelepathyQt4/PendingSendMessage>

#include "TelepathyQt4/_gen/pending-send-message.moc.hpp"

#include <TelepathyQt4/ContactMessenger>
#include <TelepathyQt4/Message>
#include <TelepathyQt4/TextChannel>

namespace Tp
{

struct TELEPATHY_QT4_NO_EXPORT PendingSendMessage::Private
{
    Private(const Message &message)
        : message(message)
    {
    }

    QString token;
    Message message;
};

PendingSendMessage::PendingSendMessage(const TextChannelPtr &channel, const Message &message)
    : PendingOperation(channel),
      mPriv(new Private(message))
{
}

PendingSendMessage::PendingSendMessage(const ContactMessengerPtr &messenger, const Message &message)
    : PendingOperation(messenger),
      mPriv(new Private(message))
{
}

PendingSendMessage::~PendingSendMessage()
{
    delete mPriv;
}

/**
 * Return the channel used to send the message if this instance was created using
 * TextChannel. If it was created using ContactMessenger, return a null TextChannelPtr.
 *
 * \return A TextChannelPtr object.
 */
TextChannelPtr PendingSendMessage::channel() const
{
    return TextChannelPtr(qobject_cast<TextChannel*>((TextChannel*) object().data()));
}

/**
 * Return the contact messenger used to send the message if this instance was created using
 * ContactMessenger. If it was created using TextChannel, return a null ContactMessengerPtr.
 *
 * \return A ContactMessengerPtr object.
 */
ContactMessengerPtr PendingSendMessage::messenger() const
{
    return ContactMessengerPtr(qobject_cast<ContactMessenger*>((ContactMessenger*) object().data()));
}

QString PendingSendMessage::sentMessageToken() const
{
    return mPriv->token;
}

Message PendingSendMessage::message() const
{
    return mPriv->message;
}

void PendingSendMessage::onTextSent(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<> reply = *watcher;

    if (reply.isError()) {
        setFinishedWithError(reply.error());
    } else {
        setFinished();
    }
    watcher->deleteLater();
}

void PendingSendMessage::onMessageSent(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString> reply = *watcher;

    if (reply.isError()) {
        setFinishedWithError(reply.error());
    } else {
        mPriv->token = reply.value();
        setFinished();
    }
    watcher->deleteLater();
}

} // Tp
