
#include <math.h>

#include <QtDebug>

#include "stats.h"

void Stats::insert(double value)
{
	vals.append(value);
	min = qMin(min, value);
	max = qMax(max, value);
	accum += value;
	sorted = false;
}

void Stats::clear()
{
	vals.clear();
	min = HUGE_VAL;
	max = -HUGE_VAL;
	accum = 0;
	sorted = false;
}

double Stats::median() const
{
	// Sort the vals array, ignoring our const attribute
	((Stats*)this)->sort();

	int c = count();
	if (c & 1) {
		// Odd number of elements - just return the middle one
		return vals[c >> 1];
	} else {
		// Even number of elements - average the middle two
		return (vals[(c-1) >> 1] + vals[c >> 1]) / 2.0;
	}
}

double Stats::variance() const
{
	double var = 0;
	double avg = mean();
	for (int i = 0; i < vals.size(); i++) {
		double diff = avg - vals.at(i);
		var += diff * diff;
	}
	return var;
}

double Stats::standardDeviation() const
{
	switch (type) {
	case Population:
		Q_ASSERT(count() > 0);
		return sqrt(variance() / count());
	case Sample:
		Q_ASSERT(count() >= 2);
		return sqrt(variance() / (count() - 1));
	default:
		Q_ASSERT(0);
		return 0;
	}
}

QDebug &operator<<(QDebug &debug, const Stats &s)
{
	debug << "min" << s.minimum() << "max" << s.maximum()
		<< "med" << s.median()
		<< "avg" << s.average() << "+/-" << s.standardDeviation()
		<< "count" << s.count();
	return debug;
}

