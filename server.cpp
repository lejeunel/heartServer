/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtBluetooth module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Digia Plc and its Subsidiary(-ies) nor the names
**     of its contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "server.h"
#include "daq.h"


Server::Server(QObject *parent)
    : QObject(parent),
      currentAdapterIndex(0)
{

    msgSize = 256;
    secsTimeout = 3; // seconds of timeout before socket is reinit
    nCharsCommand = 5; // Read 4 character commands from client
    btStart = "start";
    btStop = "stop";
    btKill = "kill";
    btClock = "clock";
    btWait = "wait";
    started = false;
    stopped = true;
    packsIn = 0; //Number of packets acquired
    ratioPacks = 50; //ratio of packets to write to buffer

    buf[18] = {0};
    servmacaddr = "00:02:72:C9:1B:25" ; //rpi
    serv_addr = {0};
    client_addr = {0};
    opt  = sizeof(client_addr);

    //Create daq object and its corresponding thread
    daq = new Daq;
    daqThread = new QThread;
    QObject::connect(this, SIGNAL(daqStartContinuous(QString)),
            daq, SLOT(startContinuous(QString)));
    QObject::connect(this, SIGNAL(daqStopContinuous()),
            daq, SLOT(stopContinuous()));
    daq->moveToThread(daqThread);
    daqThread->start(); //Starting thread (not acquisition)

    startServer();
    makeLog();
    communicate();
}

Server::~Server()
{
    stopServer();
}

void Server::getMsgDaq(QString msg){
   qDebug() << msg;
}

void Server::setTimeout(int seconds){

    //Set timeout on blocking read
    struct timeval tv;
    tv.tv_sec = seconds;  /* 15 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

}
void Server::setDaq(Daq& daqIn){
    daq = &daqIn;
}

void Server::startServer()
{

    // create a socket
    sockfd =  socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    if (sockfd < 0)
        printWriteLog("ERROR opening socket");


    serv_addr.rc_family = AF_BLUETOOTH;

    str2ba(servmacaddr,&serv_addr.rc_bdaddr);
    serv_addr.rc_channel = (uint8_t) 1;

    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
        printWriteLog("binding failed ");
    else
        printWriteLog("binding successful");
    printWriteLog("Listening for incoming connection...");
    listen(sockfd,1);

    clientConnected(sockfd);
    //setTimeout(secsTimeout);
}

void Server::restartServer()
{

    printWriteLog("Closing socket");
    close(sockfd);
    close(clientSocket);
    startServer();
}
void Server::makeLog(){
    oflog.open ("daq.log", std::ofstream::out | std::ofstream::app);
      oflog << "New log file";
}

void Server::closeLog(){
    oflog.close();
}

void Server::clientDisconnected(const QString &name)
{
    //ui->chat->insertPlainText(QString::fromLatin1("%1 has left.\n").arg(name));
}
//! [clientConnected clientDisconnected]

//! [connected]
void Server::connected(const QString &name)
{
    //ui->chat->insertPlainText(QString::fromLatin1("Joined chat with %1.\n").arg(name));
}

//! [showMessage]
void Server::showMessage(const QString &sender, const QString &message)
{
   // ui->chat->insertPlainText(QString::fromLatin1("%1: %2\n").arg(sender, message));
}
//! [showMessage]

void Server::printWriteLog(const QString &message){
   int recvOut;
   QByteArray ba;
   ba = message.toUtf8();
   bufmsg = ba.data();
   oflog << message.toStdString();
   qDebug() << "sending [" <<  QString::fromUtf8(bufmsg) << "]";
   write(clientSocket, bufmsg, msgSize);
   //qDebug() << "asdf";
}

QString Server::readSocket(char* buffer, int buffer_size)
{
    int recvout;

    bzero(buffer,buffer_size);

    while(true)
    {
        const int recvout = recv(clientSocket, buffer, buffer_size, 0);
        if(recvout == -1 || recvout == 0)
        {
            qDebug() << "Client disconnected";
            return QString::fromLatin1("Client disconnected");
        }
        else{
            qDebug() << "reading [" << QString::fromLatin1(buffer) << "]";
            return QString::fromLatin1(buffer);
        }
    }

}

void Server::getBuffer(DataOut &x){
    ++packsIn;
    if(packsIn == ratioPacks){
        sendPacket(x);
        packsIn = 0; //reinit packet counter
    }

}

void Server::sendPacket(DataOut &pack){
   write(clientSocket, (char*)&pack, sizeof(DataOut));
}
void Server::communicate()
{
    char buffer[msgSize];
    //char* buffer;

    QString msg;

    bool kill = false;
            bool gotTime = false;
            bool started = false;
            bool stopped = true;

        while(!kill){


            msg = readSocket(buffer,msgSize);
            if (btClock==msg) {
                printWriteLog("Waiting for client date/time");
                msg = readSocket(buffer,msgSize);

                printWriteLog("received client time [" + msg + "]");
                lastTime=msg;
                gotTime = true;
            }
            else if (btStart==msg) {
                printWriteLog("received [ " + msg + "]");
                //msg = readSocket(buffer,msgSize);
                if(gotTime){
                    //printWriteLog("Starting acquisition");
                    fName = "rpiData_" + lastTime + ".bin";
                    msg = readSocket(buffer,msgSize);
                    printWriteLog("Startin acquisition. File: " + fName);
                    //myFile.open(fName.toUtf8(), std::ios::out|std::ios::binary);
                    emit daqStartContinuous(fName);
                    started = true;
                    stopped = false;
                }
                else {
                    printWriteLog("Server hasn't got time. Send it first.");
                }
            }

            else if (btStop==msg) {
                printWriteLog("received [" + msg + "]");
                if(started){
                    //printWriteLog("Stopping acquisition");
                    emit daqStopContinuous();
                    started = false;
                    stopped = true;
                    gotTime = false;
                }
                else printWriteLog("Acquisition not started");
            }
            else if (btKill==msg) {
                printWriteLog("received [" + msg + "]");
                if(!stopped){
                    //printWriteLog("Stopping acquisition");
                    //daq->stopContinuous();
                    emit daqStopContinuous();
                    started = false;
                    stopped = true;
                }

                printWriteLog("Killing server");
                closeLog();
                //return 0;
            }
            else if (btWait==msg) {
                //printWriteLog("received [" + msg + "]");
                    printWriteLog("Server waiting");
            }
            else{
                qDebug() << "receiving [" << msg << "]";
                printWriteLog("received nothing or message unknown, closing connection");
                restartServer();
            }
        }

}

void Server::stopServer()
{
    // Unregister service
    //serviceInfo.unregisterService();

    // Close sockets
    //qDeleteAll(clientSocket);

    // Close server
}

//! [sendMessage]
void Server::sendMessage(const QString &message)
{
    QByteArray text = message.toUtf8() + '\n';

    //foreach (QBluetoothSocket *socket, clientSockets)
        //socket->write(text);
}
//! [sendMessage]

//! [clientConnected]
void Server::clientConnected(int &sockfd)
{
    // accept one connection
    clientSocket = accept(sockfd, (struct sockaddr *)&client_addr, &opt);

    setTimeout(secsTimeout);
    ba2str( &client_addr.rc_bdaddr, buf );

    printWriteLog("accepted connection");
}
//! [clientConnected]

