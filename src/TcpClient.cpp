#include "TcpClient.h"

using namespace ofxAsio;

std::shared_ptr<TcpClient> TcpClient::make() {
	std::shared_ptr<TcpClient> Server(new TcpClient());
	return Server;
}

TcpClient::TcpClient() : mService(), mSocket(mService), mWork(mService),  mIsConnected(true) { //mTimer(mService), mHeartbeatTimer(mService),
	init();
}

TcpClient::~TcpClient() {
	mSocket.cancel();
	mService.stop();
	if (mServiceThread.joinable()) {
		mServiceThread.join();
	}
}

void TcpClient::connect(std::string ip_address, int port) {
	// Start the connect actor.
	mResolver = std::shared_ptr<asio::ip::tcp::resolver>(new asio::ip::tcp::resolver(mService));
	tryConnect(mResolver->resolve(asio::ip::tcp::resolver::query(ip_address, std::to_string(port))));

	// Start the deadline actor. You will note that we're not setting any
	// particular deadline here. Instead, the connect and input actors will
	// update the deadline prior to each asynchronous operation.
//	mTimer.async_wait([this](const asio::error_code& error) { checkDeadline(error); });
}

void TcpClient::disconnect() {
	std::printf("ofxAsio::TcpClient -- disconnect");
	mIsConnected = false;
	mSocket.close();
//	mTimer.cancel();
//	mHeartbeatTimer.cancel();
}

char TcpClient::getTerminator() {
	return mTerminator;
}

void TcpClient::setTerminator(char terminator) {
	mTerminator = terminator;
}

void TcpClient::send(std::string message) {
	if (!mIsConnected)
		return;

	// Start an asynchronous operation to send a heartbeat message.
	asio::async_write(mSocket, asio::buffer(&message[0], message.size()),
		[this, message](const asio::error_code error, size_t bytes_received) {
			onWrite(error, bytes_received, message);
		}
	);
}

void TcpClient::tryConnect(asio::ip::tcp::resolver::iterator resolver) {
	if (resolver != asio::ip::tcp::resolver::iterator()) {
		std::printf("ofxAsio::TcpClient -- Trying endpoint %s:%d ...\n", resolver->endpoint().address().to_string().c_str(), resolver->endpoint().port());
		// Set a deadline for the connect operation.
//		mTimer.expires_from_now(std::chrono::seconds(60));

		// Start the asynchronous connect operation.
		mSocket.async_connect(resolver->endpoint(),
			[this, resolver](const asio::error_code& error) {
				onConnect(error, resolver);
			});
	}
	else {
		// There are no more endpoints to try. Shut down the client.
		disconnect();
	}
}


void TcpClient::init() {
	mTerminator = '\0';
	mServiceThread = std::thread([&] {
		mService.run();
	});

}

/*
void TcpClient::checkDeadline(const asio::error_code& error) {
	if (!mIsConnected) {
		std::printf("ofxAsio::TcpClient -- not connected.\n");
		return;
	}

	// Check whether the deadline has passed. We compare the deadline against
	// the current time since a new asynchronous operation may have moved the
	// deadline before this actor had a chance to run.
	if (mTimer.expires_at() <= std::chrono::system_clock::now()) {
		std::printf("ofxAsio::TcpClient -- deadline has passed.\n");
		// The deadline has passed. The socket is closed so that any outstanding
		// asynchronous operations are cancelled.
		mSocket.close();

		// There is no longer an active deadline. The expiry is set to positive
		// infinity so that the actor takes no action until a new deadline is set.
		mTimer.expires_at(std::chrono::system_clock::time_point::max());
	}

	// Put the actor back to sleep.
	mTimer.async_wait([this](const asio::error_code& error) { checkDeadline(error); });
}
*/

void TcpClient::onConnect(const asio::error_code& ec,
	asio::ip::tcp::resolver::iterator endpoint_iter)
{
	if (!mIsConnected) {
		std::printf("ofxAsio::TcpClient -- not connected.\n");

		return;
	}

	// The async_connect() function automatically opens the socket at the start
	// of the asynchronous operation. If the socket is closed at this time then
	// the timeout handler must have run first.
	if (!mSocket.is_open()) {
		std::printf("ofxAsio::TcpClient -- Connect timed out\n");

		// Try the next available endpoint.
		tryConnect(++endpoint_iter);
	}

	// Check if the connect operation failed before the deadline expired.
	else if (ec) {
		std::printf("ofxAsio::TcpClient -- Connect error: %s\n", ec.message().c_str());

		// We need to close the socket used in the previous connection attempt
		// before starting a new one.
		mSocket.close();

		// Try the next available endpoint.
		tryConnect(++endpoint_iter);
	}

	// Otherwise we have successfully established a connection.
	else
	{
		std::printf("ofxAsio::TcpClient -- Connected!\n");

		// Start the input actor.
		receive();

		// Start the heartbeat actor.
		//sendHeartbeat();
	}
}

void TcpClient::receive()
{
	// Set a deadline for the read operation.
//	mTimer.expires_from_now(std::chrono::seconds(30));

	// Start an asynchronous operation to read a terminator-delimited message.
	asio::async_read_until(mSocket, input_buffer_, mTerminator,
		[this](const asio::error_code& error, size_t bytes_received) { 
			onReceive(error, bytes_received); 
		}
	);
}

void TcpClient::onReceive(const asio::error_code& ec, size_t bytes_received)
{
	if (!mIsConnected)
		return;

	if (!ec)
	{
		// Extract the newline-delimited message from the buffer.
		std::string line;
		std::istream is(&input_buffer_);
		std::getline(is, line);

		mIncomingMessage = line;
		
		for (auto& callback : mOnReceiveFns) {
			callback(mIncomingMessage);
		}

		receive();
	}
	else
	{
		std::printf("ofxAsio::TcpClient -- Error on receive: %s\n", ec.message().c_str());

		disconnect();
	}
}

void TcpClient::sendHeartbeat() {
	if (!mIsConnected) {
		return;
	}

	std::string heartbeatMessage = "\n";
	// Start an asynchronous operation to send a heartbeat message.
	asio::async_write(mSocket, asio::buffer(heartbeatMessage, 1),
		[this, heartbeatMessage](const asio::error_code error, size_t bytes_received) {
			onWrite(error, bytes_received, heartbeatMessage); 
		}
	);
}

void TcpClient::onWrite(const asio::error_code& ec, size_t bytes_received, std::string message) {
	if (!mIsConnected)
		return;

	if (!ec) {
		// Wait 10 seconds before sending the next heartbeat.
		/*
		mHeartbeatTimer.expires_from_now(std::chrono::seconds(10));
		mHeartbeatTimer.async_wait([this](const asio::error_code& error) { 
			sendHeartbeat(); 
		});
		*/
		for (auto& callback : mOnSendFns) {
			callback(message);
		}
	}
	else {
		std::printf("ofxAsio::TcpClient -- Error on heartbeat: %s\n", ec.message().c_str());

		disconnect();
	}
}

void TcpClient::addOnReceiveFn(std::function<void(std::string msg)> response) {
	mOnReceiveFns.push_back(response);
}

void TcpClient::addOnSendFn(std::function<void(std::string msg)> response) {
	mOnSendFns.push_back(response);
}