#include "TcpServer.h"

using namespace ofxAsio;

std::shared_ptr<TcpServer> TcpServer::make(int port) {
	std::shared_ptr<TcpServer> Server(new TcpServer(port));
	return Server;
}

TcpServer::TcpServer(int port) : mService(), mSocket(mService), mWork(mService), mAcceptor(mService) {
	init(port);
}

TcpServer::~TcpServer() {
	mSocket.cancel();
	mService.stop();
	if (mServiceThread.joinable()) {
		mServiceThread.join();
	}
}

void TcpServer::start() {
	createSession();
}


void TcpServer::addOnReceiveFn(std::function<void(std::string msg)> response) {
	mOnReceiveFns.push_back(response);
	for (auto& session : mSessions) {
		session->addOnReceiveFn(response);
	}
}

void TcpServer::addOnSendFn(std::function<void(std::string msg)> response) {
	mOnSendFns.push_back(response);
	for (auto& session : mSessions) {
		session->addOnSendFn(response);
	}
}

void TcpServer::init(int port) {
	mLocalEndpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);

	std::printf("ofxAsio::TcpServer -- Creating endpoint %s:%d ...\n", mLocalEndpoint.address().to_string().c_str(), mLocalEndpoint.port());

	mAcceptor.open(mLocalEndpoint.protocol());
	mAcceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
	mAcceptor.bind(mLocalEndpoint);
	mAcceptor.listen();

	mServiceThread = std::thread([&] {
		mService.run();
	});

}

void TcpServer::onConnect(std::vector<std::shared_ptr<TcpSession>>::iterator session_iter, const asio::error_code& error) {
	if (!error) {
		std::printf("ofxAsio::TcpServer -- Connection from %s received!\n", (*session_iter)->getSocket().remote_endpoint().address().to_string().c_str());

		(*session_iter)->start();
		createSession();
	}
	else {
		std::printf("ofxAsio::TcpServer::onConnect -- Error receiving data. %s\n", error.message().c_str());
		mSessions.erase(session_iter);
	}
}

void TcpServer::createSession() {
	std::printf("ofxAsio::TcpServer -- Creating new session...\n");

	std::shared_ptr<TcpSession> newSession = TcpSession::make(mService);
	std::vector<std::shared_ptr<TcpSession>>::iterator iterator = mSessions.insert(mSessions.end(), newSession);

	for (auto& fn : mOnReceiveFns) {
		newSession->addOnReceiveFn(fn);
	}

	for (auto& fn : mOnSendFns) {
		newSession->addOnSendFn(fn);
	}

	mAcceptor.async_accept(newSession->getSocket(), [this, iterator](const asio::error_code &error) {
		onConnect(iterator, error);
	});
}