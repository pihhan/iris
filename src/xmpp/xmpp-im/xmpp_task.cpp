/*
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

#include <QTimer>

#include "safedelete.h"
#include "xmpp_task.h"
#include "xmpp_client.h"
#include "xmpp_xmlcommon.h"
#include "xmpp_stanza.h"

using namespace XMPP;

class Task::TaskPrivate
{
public:
	TaskPrivate() {}

	QString id;
	bool success;
	int statusCode;
	QString statusString;
	Client *client;
	bool insig, deleteme, autoDelete;
	bool done;
        Stanza::Error *error;
};

/*! \brief Create Task from rootTask */
Task::Task(Task *parent)
:QObject(parent)
{
	init();

	d->client = parent->client();
	d->id = client()->genUniqueId();
	connect(d->client, SIGNAL(disconnected()), SLOT(clientDisconnected()));
}

Task::Task(Client *parent, bool)
:QObject(0)
{
	init();

	d->client = parent;
	connect(d->client, SIGNAL(disconnected()), SLOT(clientDisconnected()));
}

Task::~Task()
{
        if (d->error)
            delete d->error;
	delete d;
}

void Task::init()
{
	d = new TaskPrivate;
	d->success = false;
	d->insig = false;
	d->deleteme = false;
	d->autoDelete = false;
	d->done = false;
        d->error = NULL;
}

Task *Task::parent() const
{
	return (Task *)QObject::parent();
}

Client *Task::client() const
{
	return d->client;
}

QDomDocument *Task::doc() const
{
	return client()->doc();
}

QString Task::id() const
{
	return d->id;
}

bool Task::success() const
{
	return d->success;
}

int Task::statusCode() const
{
	return d->statusCode;
}

const QString & Task::statusString() const
{
	return d->statusString;
}

void Task::go(bool autoDelete)
{
	d->autoDelete = autoDelete;

	if (!client() || !&client()->stream()) {
		qWarning("Task::go(): attempted to send a task over the broken connection.");
		if (autoDelete) {
			deleteLater();
		}
	}
	else {
		onGo();
	}
}

/*! \brief Iterates on list of all tasks, and try if one of them would accept
    and process element.
    \param x XML stanza to process.
    \return True if stanza was handled by this Task, false if not.
*/
bool Task::take(const QDomElement &x)
{
	const QObjectList p = children();

	// pass along the xml
	Task *t;
	for(QObjectList::ConstIterator it = p.begin(); it != p.end(); ++it) {
		QObject *obj = *it;
		if(!obj->inherits("XMPP::Task"))
			continue;

		t = static_cast<Task*>(obj);
		if(t->take(x))
			return true;
	}

	return false;
}

/*! \brief Schedule this Task to be deleted, after it exits all functions.
    Can be called more than once, will be deleted only one time. */
void Task::safeDelete()
{
	if(d->deleteme)
		return;

	d->deleteme = true;
	if(!d->insig)
		SafeDelete::deleteSingle(this);
}

/*! \brief Reimplement this method to create and send stanza over network. 
    Usually first call some methods to configure destination and parameters,
    then using go() it will run this method to dispatch. */
void Task::onGo()
{
}

/*! \brief Reimplement if you need to be notified, if client disconnected
    before delivering stanza. */
void Task::onDisconnect()
{
	if(!d->done) {
		d->success = false;
		d->statusCode = ErrDisc;
		d->statusString = tr("Disconnected");

		// delay this so that tasks that react don't block the shutdown
		QTimer::singleShot(0, this, SLOT(done()));
	}
}

/*! \brief Send stanza using configured client. */
void Task::send(const QDomElement &x)
{
	client()->send(x);
}

/*! \brief Call this method to mark result as success.
    Usually will be called from take(), when reply is what we expected. */
void Task::setSuccess(int code, const QString &str)
{
	if(!d->done) {
		d->success = true;
		d->statusCode = code;
		d->statusString = str;
		done();
	}
}

/*! \brief Call this method to mark result of task as failed.
    \param e XML stanza to create error message from.
    Usually will be called from take(), when reply did not bring data
    we requested for whatever reason. */
void Task::setError(const QDomElement &e)
{
	if(!d->done) {
		d->success = false;
		getErrorFromElement(e, d->client->streamBaseNS(), &d->statusCode, &d->statusString);
                d->error = getStanzaErrorFromElement(e, d->client->streamBaseNS());
		done();
	}
}

void Task::setError(int code, const QString &str)
{
	if(!d->done) {
		d->success = false;
		d->statusCode = code;
		d->statusString = str;
		done();
	}
}

/*! \brief Mark task finished, emit finished signal.
    If task is marked autodelete, schedule deletion of task.
*/
void Task::done()
{
	if(d->done || d->insig)
		return;
	d->done = true;

	if(d->deleteme || d->autoDelete)
		d->deleteme = true;

	d->insig = true;
	finished();
	d->insig = false;

	if(d->deleteme)
		SafeDelete::deleteSingle(this);
}

void Task::clientDisconnected()
{
	onDisconnect();
}

void Task::debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	QString str;
	str.vsprintf(fmt, ap);
	va_end(ap);
	debug(str);
}

void Task::debug(const QString &str)
{
	client()->debug(QString("%1: ").arg(metaObject()->className()) + str);
}


/**
 * \brief verifies a stanza is a IQ reply for this task
 *
 * it checks that the stanze is from the jid the request was send to and the id and the namespace (if given) match.
 *
 * it further checks that the sender jid is not empty (except if \a to is our server), it's not from
 * our bare jid (except if send to one of our resources or our server)
 * \param x the stanza to test
 * \param to the Jid this task sent an IQ to, and who should return reply
 * \param id the id of the sent IQ
 * \param xmlns the expected namespace if the reply (if non empty)
 * \return true if this is our reply, false otherwise.
*/

bool Task::iqVerify(const QDomElement &x, const Jid &to, const QString &id, const QString &xmlns)
{
	if(x.tagName() != "iq")
		return false;

	Jid from(x.attribute("from"));
	Jid local = client()->jid();
	Jid server = client()->host();

	// empty 'from' ?
	if(from.isEmpty()) {
		// allowed if we are querying the server
		if(!to.isEmpty() && !to.compare(server))
			return false;
	}
	// from ourself?
	else if(from.compare(local, false) || from.compare(local.domain(),false)) {
		// allowed if we are querying ourself or the server
		if(!to.isEmpty() && !to.compare(local, false) && !to.compare(server))
			return false;
	}
	// from anywhere else?
	else {
		if(!from.compare(to))
			return false;
	}

	if(!id.isEmpty()) {
		if(x.attribute("id") != id)
			return false;
	}

	if(!xmlns.isEmpty()) {
		if(queryNS(x) != xmlns)
			return false;
	}

	return true;
}

// TODO: move to better place
// xmpp_xmlcommon is candidate, but is somehow full
Stanza::Error * Task::getStanzaErrorFromElement(const QDomElement &e, const QString &baseNs)
{
    bool found;
    QDomElement tag = findSubTag(e, "error", &found);
    if(!found)
            return NULL;

    XMPP::Stanza::Error *err = new Stanza::Error();
    err->fromXml(tag, baseNs);
    return err;
}

/** @brief Return type of error.
    @return Numeric value of enum ErrorType, or 0 on undefined error.
    @see Stanza::Error
    It is numeric value from enum Stanza::Error::ErrorType.
*/
int Task::errorType() const
{
    if (d->error)
        return d->error->type;
    else return 0;
}

/** @brief Return error condition.
    @return Numeric value of Stanza::Error::ErrorCond, or 0 on undefined error.
    @see Stanza::Error
*/
int Task::errorCondition() const
{
    if (d->error)
        return d->error->condition;
    else
        return 0;
}


