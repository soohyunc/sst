// Visualization of the simulated network
#ifndef NETVIEW_H
#define NETVIEW_H

#include <QWidget>
#include <QMainWindow>

class QLabel;


namespace SST {

class ViewWidget : public QWidget
{
	Q_OBJECT

private:
	double vscale;
	int vxofs, vyofs;

	QByteArray selNodeId;
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

#endif	// NETVIEW_H
