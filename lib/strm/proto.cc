
#include <QtDebug>

#include "proto.h"

using namespace SST;


////////// StreamProtocol //////////

const quint32 StreamProtocol::magic;
const int StreamProtocol::mtu;

const unsigned StreamProtocol::hdrlenInit;
const unsigned StreamProtocol::hdrlenReply;
const unsigned StreamProtocol::hdrlenData;
const unsigned StreamProtocol::hdrlenDatagram;
const unsigned StreamProtocol::hdrlenReset;

const unsigned StreamProtocol::typeBits;
const unsigned StreamProtocol::typeMask;
const unsigned StreamProtocol::typeShift;
const unsigned StreamProtocol::subtypeBits;
const unsigned StreamProtocol::subtypeMask;
const unsigned StreamProtocol::subtypeShift;

const quint8 StreamProtocol::dataPushFlag;
const quint8 StreamProtocol::dataMessageFlag;
const quint8 StreamProtocol::dataCloseFlag;

const quint8 StreamProtocol::dgramBeginFlag;
const quint8 StreamProtocol::dgramEndFlag;

const StreamProtocol::StreamID StreamProtocol::sidOrigin;
const StreamProtocol::StreamID StreamProtocol::sidRoot;


