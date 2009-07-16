/*
 * xmpp_task.h
 * Copyright (C) 2003  Justin Karneges
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef XMPP_TASK_H
#define XMPP_TASK_H

#include <QObject>
#include <QString>

class QDomDocument;
class QDomElement;

namespace XMPP {
	class Client;
	class Jid;

        /** @brief Parent class for single requests.

        It does implement unique id generator and infrastrucuture for specific
        IQ request as roster manager, version requests and such.
        Usual scenario is to create task using parent task or client, then
        using get() or set() method, depending what we want to do.
        Connect finished signal to your method that does something with result,
        and send request with go(). 
        */
	class Task : public QObject
	{
		Q_OBJECT
	public:
		enum { ErrDisc };
		Task(Task *parent);
		Task(Client *, bool isRoot);
		virtual ~Task();

		Task *parent() const;
		Client *client() const;
		QDomDocument *doc() const;
		QString id() const;

		bool success() const;
		int statusCode() const;
		const QString & statusString() const;

                /** @brief Actually send a request.
                    @param autoDelete If is true, request will delete itself after finished() signal. */
		void go(bool autoDelete=false);
                /** @brief Parse reply received to our request.
                    @return true if it was handled by this task, false if stanza were not for this task and should be tried elsewhere. 
                    You should call setSuccess or setError inside. Use \see iqVerify to check that reply is for this task.
                */
		virtual bool take(const QDomElement &);
		void safeDelete();

	signals:
                /** @brief Signal that reply arrived and is parsed. */
		void finished();

	protected:
                /** @brief Reimplement this method to create stanza from
                    parameters and send it using send. */
		virtual void onGo();
		virtual void onDisconnect();
		void send(const QDomElement &);
                /** @brief Set request was successful. 
                  Expected to be called from \function take. 
                  It will emit finished signal. */
		void setSuccess(int code=0, const QString &str="");
                /** @brief Set request was not sucessful, create error stanza from reply included as parameter. 
                    It will emit finished signal. */
		void setError(const QDomElement &);
                /** @brief Set request was not successful. 
                  Expected to be called from \function take. */
		void setError(int code=0, const QString &str="");
		void debug(const char *, ...);
		void debug(const QString &);
                /** @brief Check that given stanza is what we are waiting for.
                    @param x Received reply
                    @param to Who as request sent to.
                    @param id Id of stanza, usually get from method \see id()
                    @param xmlns Namespace of this request.
                    @return false if parameters do not match our request, true if id is same, is received from target and has specified namespace.
                    Designed to be used in /function take() method.
                    */
		bool iqVerify(const QDomElement &x, const Jid &to, const QString &id, const QString &xmlns="");

	private slots:
		void clientDisconnected();
		void done();

	private:
		void init();

		class TaskPrivate;
		TaskPrivate *d;
	};
}

#endif
