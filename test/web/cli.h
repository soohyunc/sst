#ifndef CLI_H
#define CLI_H

#include <QRect>
#include <QList>
#include <QHash>
#include <QString>
#include <QMainWindow>
#include <QTextCursor>

#include "host.h"
#include "sim.h"

class QFile;
class QLabel;
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
	//QRect rect;
	int imgsize;

	Stream *strm;
	int pri;
	QFile *tmpf;
	bool dirty;

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
	Simulator *sim;

	QCheckBox *priocheck;
	QLabel *speedlabel;
	QTextEdit *textedit;

	QList<WebImage> images;
	QHash<Stream*,int> strms;

	Timer refresher;

public:
	WebClient(Host *host, const QHostAddress &srvaddr, int srvport,
			Simulator *sim = NULL);

private:
	void setPri(int img, int pri);

private slots:
	void reload();
	void setPriorities();
	void readyRead();
	void refreshSoon();
	void refreshNow();
	void speedSliderChanged(int value);
};


} // namespace SST

#endif	// CLI_H
