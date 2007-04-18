
#include <math.h>

#include <QLabel>
#include <QSlider>
#include <QPainter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>

#include "main.h"
#include "view.h"

using namespace SST;


// Some drawing parameters
static const int nodeRadius = 5;
static const int arrowRadius = 7;


////////// ViewWidget //////////

ViewWidget::ViewWidget(QWidget *parent)
:	QWidget(parent),
	viewaff(0)
{
	QPalette pal(palette());
	pal.setColor(backgroundRole(), Qt::black);
	setPalette(pal);

	setAutoFillBackground(true);
}

QSize ViewWidget::minimumSizeHint() const
{
	return QSize(10, 10);
}

QSize ViewWidget::sizeHint() const
{
	return QSize(700, 700);
}

void ViewWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	// Determine the scaling factor to translate from
	// our simulated network's world coordinates to view coordinates.
	// (We don't just scale the painter here because we'll want
	// some drawing dimensions to be in view coordinates.)
	int viewWidth = width();
	int viewHeight = height();
	if (viewWidth <= 0 || viewHeight <= 0)
		return;		// Can't paint anything in a zero-size widget!
	double vxscale = (double)viewWidth / worldWidth;
	double vyscale = (double)viewHeight / worldHeight;
	vscale = qMin(vxscale, vyscale);

	// Translate the painter to center the world on the view.
	vxofs = (viewWidth - (worldWidth * vscale)) / 2;
	vyofs = (viewHeight - (worldHeight * vscale)) / 2;
	painter.translate(vxofs, vyofs);

	// Draw a box showing the boundary of the world.
	painter.setPen(Qt::white);
	painter.drawRect(0, 0,
			(worldWidth * vscale) - 1, (worldHeight * vscale) - 1);

	// First draw all of the edges in the network.
	QPen pen(Qt::white);
	//pen.setWidth(1);
	foreach (Node *n, nodes) {
		foreach (QByteArray nnid, n->neighbors.keys()) {
			Node *nn = nodes.value(nnid);

			// Draw an arrow from node n to node nn.
			// First translate to the target's center coordinate.
			painter.save();
			double nnvx = nn->x * vscale;
			double nnvy = nn->y * vscale;
			painter.translate(nnvx, nnvy);

			// Rotate so the source node is directly to the right.
			double rad = atan2(n->y - nn->y, n->x - nn->x);
			painter.rotate(rad * (180.0 / M_PI));

			// Shade the link according to the loss rate.
			double quality = 1.0 - n->neighbors.value(nnid);
			int weight = qMax(0, qMin(255, (int)(255 * quality)));
			pen.setColor(QColor(255, 255, 255, weight));
			painter.setPen(pen);

			// Draw an off-center line with a one-sided arrowhead,
			// to show unidirectional versus bidirectional links.
			double taillen = nn->distanceTo(n) * vscale
						- arrowRadius * 2;
			double headlen = qMin(taillen, (double)arrowRadius);
			if (headlen >= 2) {
				QPointF points[3] = {
					QPointF(arrowRadius + headlen,
						1.0 + headlen),
					QPointF(arrowRadius, 1.0),
					QPointF(arrowRadius + taillen, 1.0),
				};
				painter.drawPolyline(points, 3);
			}

			painter.restore();
		}
	}

	// Now draw all the nodes.
	foreach (Node *n, nodes) {
		painter.setPen(Qt::white);

		if (selNodeId == n->id())
			painter.setBrush(Qt::yellow);
		else if (selNodeId.isEmpty() ||
				affinity(selNodeId, n->id()) >= viewaff)
			painter.setBrush(Qt::blue);
		else
			painter.setBrush(Qt::darkGray);

		painter.drawEllipse(n->x * vscale - nodeRadius,
					n->y * vscale - nodeRadius,
					nodeRadius*2+1, nodeRadius*2+1);
	}
}

void ViewWidget::mousePressEvent(QMouseEvent *event)
{
	QWidget::mousePressEvent(event);

	int mx = event->x() - vxofs;
	int my = event->y() - vyofs;
	//qDebug() << "mouse" << x << y;

	// Find the node the user clicked on, if any
	foreach (Node *n, nodes) {

		// Find on-screen distance from click to this node
		double dx = (n->x * vscale) - mx;
		double dy = (n->y * vscale) - my;
		double dist = sqrt(dx * dx + dy * dy);
		//qDebug() << "node" << n << "dist" << dist;

		// See if within radius of the circle representing the node...
		if (dist > nodeRadius)
			continue;

		// Select it!
		selNodeId = n->id();
		return update();
	}

	// Nothing selected - clear any existing selection
	selNodeId = QByteArray();
	return update();
}

void ViewWidget::setAffinity(int n)
{
	viewaff = n;
	return update();
}


////////// ViewWindow //////////

ViewWindow::ViewWindow(QWidget *parent)
:	QMainWindow(parent)
{
	QWidget *centrwig = new QWidget(this);
	setCentralWidget(centrwig);

	vwig = new ViewWidget(centrwig);

	afflabel = new QLabel(centrwig);
	QSlider *affslider = new QSlider(Qt::Horizontal, centrwig);
	affslider->setRange(0, 32);
	affslider->setTickInterval(1);
	affslider->setTickPosition(QSlider::TicksBelow);
	connect(affslider, SIGNAL(valueChanged(int)), 
		this, SLOT(setAffinity(int)));

	QHBoxLayout *sliderlayout = new QHBoxLayout;
	sliderlayout->addWidget(afflabel);
	sliderlayout->addWidget(affslider);

	QVBoxLayout *vlayout = new QVBoxLayout;
	vlayout->addWidget(vwig);
	vlayout->addLayout(sliderlayout);
	centrwig->setLayout(vlayout);

	setAffinity(0);
}

void ViewWindow::setAffinity(int n)
{
	afflabel->setText(tr("Affinity: %0").arg(n));
	vwig->setAffinity(n);
}

