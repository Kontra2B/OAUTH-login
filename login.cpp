#include <iostream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cstdlib>
#include <unistd.h>
#include <netdb.h>
#include <utime.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "file.hpp"
#include "context.hpp"
#include "code.hpp"

#include "login.hpp"

using namespace std;

Context context;

int main(int n, char** argv) {
	context.parse(n, argv);
	while (getAccess()) 
		if (context.stop()) break;
	return EXIT_SUCCESS;
}

bool getAccess()
{
	string secret;
	File access(context.home / MUTT_CONFIG / context.hint / ACCESS_TOKEN);
	if (access) {
		struct stat info;
		stat(access.getName(), &info);
		if (context.verbose) cerr << "Found access token file: " << access.getName();
		if (info.st_mtime > time(0)) {
			access >> secret;
			if (!secret.empty()) {
				if (context.verbose) cerr << endl;
				cout << secret;
				context.state = Context::State::token;
				return false;
			}
		}
		if (context.verbose) cerr << "... but token has expired" << endl;
	}

	string refresh;
	File reaccess(context.home / MUTT_CONFIG / context.hint / REFRESH_TOKEN);
	if (reaccess) {
		if (context.verbose) cerr << "Found refresh token file: " << reaccess.getName();
		reaccess >> refresh;
		if (refresh.empty() && context.verbose) cerr << "... but it is empty";
		cerr << endl;
	}

	Code crypto;
	int port;
	string code;

	if (refresh.empty()) {
		int server = socket(AF_INET, SOCK_STREAM, 0);
		if (server == -1) {
			cerr << "Could not create redirection socket: " << strerror(errno) << std::endl;
			context.state = Context::State::fail;
			return false;
		}

		struct sockaddr_in server_addr;
		std::memset(&server_addr, 0, sizeof(server_addr));

		server_addr.sin_family = AF_INET;	// IPv4
		server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		server_addr.sin_port = htons(0);	// any port

		if (bind(server, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
			cerr << "Bind error: " << strerror(errno) << std::endl;
			context.state = Context::State::fail;
			close(server);
			return false;
		}

		struct sockaddr_in addr;
		socklen_t addr_len = sizeof(addr);
		if (getsockname(server, (struct sockaddr*)&addr, &addr_len) == -1) {
			std::cerr << "Error getting port number: " << strerror(errno) << std::endl;
			context.state = Context::State::fail;
			close(server);
			return false;
		}

		port = ntohs(addr.sin_port);
		if (context.verbose) cerr << "Listening on port: " << port << endl;
		crypto.draw();
		crypto.encode();
		crypto.encrypt();
		if (context.debug) cerr << crypto;
		ostringstream command, options;
		options << "response_type=code" << '&'
			<< "redirect_uri=http://127.0.0.1:" << port << '&'
			<< "scope=https://mail.google.com/" << '&'
			<< "client_id=" CLIENT_ID << '&'
			<< "code_challenge=" << crypto.challenge() << '&'
			<< "code_challenge_method=S256" << '&'
			<< "login_hint=" << context.hint;
		command << "xdg-open '" GOOGLE_ACCOUNTS "?" << urlEncode(options.str()) << "' >/dev/null";
		if (context.debug) cerr << endl << command.str() << endl;
		system(command.str().c_str());
		listen(server, 1);
		int redir = accept(server, 0, 0);
		close(server);
		char request[1024];
		size_t n = recv(redir, request, sizeof(request), 0);
		request[n] = 0;
		if (context.debug) cerr << endl << "Redirected data: " << endl << request;
		istringstream input(request);
		getline(input, code, '=');
		getline(input, code, '&');
		if (context.debug) cerr << "Authentication code:" << code << endl;
		ostringstream output, content;
		content << "You may close the window" << endl;
		output << "HTTP/1.1 200 OK" << endl
			<< "Content-Type: text/plain; charset=UTF-8" << endl
			<< "Content-Length: " << content.str().size() << endl
			<< endl
			<< content.str() << endl;
		send(redir, output.str().c_str(), output.str().size(), 0);
		close(redir);
	}

	const char* host = GOOGLE_APIS;
	const char* service = "443";

	if (context.verbose) cerr << endl << "Getting access token..." << endl;
	struct addrinfo *res;
	if (getaddrinfo(host, service, NULL, &res)) {
		cerr << "Can not get address info for: " << host << endl
			<< strerror(errno) << endl;
		context.state = Context::State::fail;
		return false;
	}

	int client = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (client == -1) {
		cerr << "Could not create client socket: " << strerror(errno) << std::endl;
		context.state = Context::State::fail;
		return false;
	}
	if (::connect(client, res->ai_addr, res->ai_addrlen) != 0) {
		cerr << "Can not connect client socket to: " << host << endl;
		context.state = Context::State::fail;
		close(client);
		return false;
	}

	SSL_library_init();

	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) {
		cerr << "Can not create SSL context" << endl;
		context.state = Context::State::fail;
		close(client);
		return false;
	}

	SSL* ssl = SSL_new(ctx);
	SSL_set_fd(ssl, client);

	if (SSL_connect(ssl) < 1) {
		cerr << "Can not connect client SSL socket: ";
		context.state = Context::State::fail;
		ERR_print_errors_fp(stderr);
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		close(client);
		return false;
	}

	if (context.debug) cerr << "TLS/SSL handshake OK with " << host << "\n";
	ostringstream content, output;
	content << "client_id=" CLIENT_ID << '&'
		<< "client_secret=" CLIENT_SECRET << '&';
	if (refresh.empty()) {
		content << "code=" << code << '&'
		<< "code_verifier=" << crypto.base64() << '&'
		<< "redirect_uri=http://127.0.0.1:" << port << '&'
		<< "grant_type=authorization_code";
		context.state = Context::State::access;
	} else {
		content << "refresh_token=" << refresh << '&'
			<< "grant_type=refresh_token";
		context.state = Context::State::refresh;
	}
	string append = content.str();
	output << "POST /token HTTP/1.1" << endl
		<< "Host: oauth2.googleapis.com" << endl
		<< "Content-Type: application/x-www-form-urlencoded" << endl
		<< "Content-Length: " << append.size() << endl
		<< endl
		<< append;
	append = std::move(output.str());
	if (context.debug) cerr << "Sending request:\n" << append << endl;
	SSL_write(ssl, append.c_str(), append.size());
	string response;
	response.resize(2048);
	auto n = SSL_read(ssl, (void*)response.data(), response.capacity()); 
	SSL_free(ssl);
	SSL_CTX_free(ctx);
	close(client);
	response = getJson(std::move(response));
	if (context.debug) cerr << response << endl << endl;

	secret = getToken(ACCESS_TOKEN, response);
	if (secret.empty() || refresh.empty()) refresh = getToken(REFRESH_TOKEN, response);
	if (secret.empty()) return true; // repeat whole sequence
	auto time = getTime("expires_in", response);

	utimbuf times = {0, time};
	File token(context.home / MUTT_CONFIG / context.hint / ACCESS_TOKEN);
	if (context.debug) cerr << "Setting time for: " << token.getName() << endl;
	utime(token.getName(), &times);

	cout << secret;
	return false;
}

string getJson(string&& text)
{
	string value;
	auto start = text.find('{');
	if (start == -1) return value;
	auto stop = text.find('}', start);
	if (stop == -1) return value;
	if (stop < start) return value;
	return text.substr(start, stop - start + 1);
}

string getToken(string&& token, const string& text)
{
	string value;
	auto start = text.find(token);
	if (start != -1) {
		istringstream keys(text.substr(start));
		getline(keys, value, ':');
		getline(keys, value, '"');
		getline(keys, value, '"');
	}
	File secret(context.home / MUTT_CONFIG / context.hint / token);
	if (context.debug) cerr << "Writing secret to: " << secret.getName() << endl;
	secret << value;
	return value;
}

time_t getTime(string&& token, const string& text)
{
	time_t value = 0;
	string dummy;
	auto start = text.find(token);
	if (start == -1) return value;
	istringstream keys(text.substr(start));
	keys >> dummy;
	keys >> value;
	if (context.debug) cerr << "Secret expires in [s]: " << value << endl;
	return value + time(0);
}

string urlEncode(const string& text) {
	std::ostringstream encoded;
	for (unsigned char c : text) {
		// Znaki alfanumeryczne oraz '-', '_', '.', '~' nie są kodowane
		if (isalnum(c) || c == '&' || c == '=' || c == '-' || c == '_' || c == '.' || c == '~') encoded << c;
		else encoded << '%' << uppercase << hex << setw(2) << setfill('0') << static_cast<int>(c);
	}
	return encoded.str();
}

