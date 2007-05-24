#ifndef CLI_H
#define CLI_H

#include <QRect>
#include <QList>
#include <QHash>
#include <QString>
#include <QMainWindow>
#include <QTextCursor>

#include "host.h"

class QFile;
class QCheckBox;
class QTextEdit;


namespace SST {

class Host;
class Stream;


struct WebImage
{
	QTextCursor curs;
	int before, after;
	QString name;
	QRect rect;

	Stream *strm;
	int pri;
	QFile *tmpf;

	inline WebImage()
		: strm(NULL), pri(0), tmpf(NULL) { }
};

class WebClient : public QMainWindow
{
	Q_OBJECT

private:
	Host *host;
	QHostAddress srvaddr;
	int srvport;

	QCheckBox *priocheck;
	QTextEdit *textedit;

	QList<WebImage> images;
	QHash<Stream*,int> strms;

public:
	WebClient(Host *host, const QHostAddress &srvaddr, int srvport);

private:
	void setPri(int img, int pri);

private slots:
	void reload();
	void setPriorities();
	void readyRead();
};


} // namespace SST

#endif	// CLI_H
