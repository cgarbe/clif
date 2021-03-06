#include "clifviewcaller.hpp"

#include <QDataStream>
#include <QProcess>

ExternalClifViewer::ExternalClifViewer(QString file, QString dataset, QString store, bool del_on_exit)
{
  _socket = new QLocalSocket(this);
  
  _file = file;
  _dataset = dataset;
  _store = store;
  _del = del_on_exit;
  
  connect(_socket, SIGNAL(connected()), this, SLOT(connected()));
  connect(_socket, SIGNAL(error(QLocalSocket::LocalSocketError)), this, SLOT(error(QLocalSocket::LocalSocketError)));
  
  _socket->connectToServer("clifview_showfile", QIODevice::ReadWrite);
}

void ExternalClifViewer::connected()
{
  QByteArray block;
  QDataStream out(&block, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_4_0);
  out << _file;
  out << _dataset;
  out << _store;
  out << _del;
  
  _socket->write(block);
  
  _file = QString();
  
  _socket->disconnectFromServer();
}

void ExternalClifViewer::error(QLocalSocket::LocalSocketError e)
{  
  printf("could not find/connect running clifview %s!\n", _socket->errorString().toUtf8().constData());
  printf("starting new instance\n");
  
  QProcess *process = new QProcess();
  QStringList args;
  if (_file.size())
    args << "-i" <<  _file;
  if (_dataset.size())
    args << "-d" << _dataset;
  if (_store.size())
    args << "-s" << _store;
  if (_del)
    args << "--delete-on-exit";
  
#if defined (_WIN32)
  QString file = "clifview.exe";
#else
  QString file = "clifview";
#endif

  process->setStandardOutputFile("debug.log");
  process->start(file, args);
}