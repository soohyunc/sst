// Visualization of the simulated network
#ifndef NETVIEW_H
#define NETVIEW_H

#include <QHash>
#include <QWidget>
#include <QMainWindow>

class QLabel;


namespace SST {

class ViewWidget : public QWidget
{
	Q_OBJECT

private:
	typedef QPair<QByteArray,QByteArray> Edge;

	double vscale;
	int vxofs, vyofs;

	QByteArray selNodeId;
	QHash<QByteArray,int> selNeighbors;
	QHash<Edge,int> selEdges;
	int viewaff;

public:
	ViewWidget(QWidget *parent = NULL);

	QSize minimumSizeHint() const;
	QSize sizeHint() const;

	void setAffinity(int n);
	//void setZoom(int n);

protected:
	void paintEvent(QPaintEvent *event);
	void mousePressEvent(QMouseEvent *event);
};

class ViewWindow : public QMainWindow
{
	Q_OBJECT

private:
	ViewWidget *vwig;
	QLabel *afflabel;

public:
	ViewWindow(QWidget *parent = NULL);

private slots:
	//void zoomSliderChanged(int n);
	void setAffinity(int n);
};

} // namespace SST

inline uint qHash(const QPair<QByteArray,QByteArray> &edge)
	{ return qHash(edge.first) + qHash(edge.second); }

#endif	// NETVIEW_H
