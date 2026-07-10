#include "zmq_endpoint.hpp"

#include <iostream>
#include <string>

namespace {

#define CHECK( expression ) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << __func__ << ':' << __LINE__ \
				<< ": check failed: " #expression "\n"; \
			return false; \
		} \
	} while ( false )

bool BuildsIpv4HostnameAndWildcardEndpoints() {
	std::string endpoint;
	CHECK( fnql::zmq::BuildTcpEndpoint( "127.0.0.1", 28960, endpoint ) );
	CHECK( endpoint == "tcp://127.0.0.1:28960" );
	CHECK( fnql::zmq::BuildTcpEndpoint( "localhost", 1, endpoint ) );
	CHECK( endpoint == "tcp://localhost:1" );
	CHECK( fnql::zmq::BuildTcpEndpoint( "*", 65535, endpoint ) );
	CHECK( endpoint == "tcp://*:65535" );
	return true;
}

bool BracketsRawIpv6AndPreservesBracketedIpv6() {
	std::string endpoint;
	CHECK( fnql::zmq::BuildTcpEndpoint( "::1", 28960, endpoint ) );
	CHECK( endpoint == "tcp://[::1]:28960" );
	CHECK( fnql::zmq::BuildTcpEndpoint( "[::]", 28961, endpoint ) );
	CHECK( endpoint == "tcp://[::]:28961" );
	CHECK( fnql::zmq::BuildTcpEndpoint( "fe80::1%12", 28962, endpoint ) );
	CHECK( endpoint == "tcp://[fe80::1%12]:28962" );
	return true;
}

bool RejectsMalformedHostsSchemesPortsAndPorts() {
	std::string endpoint = "stale";
	CHECK( !fnql::zmq::BuildTcpEndpoint( "", 28960, endpoint ) );
	CHECK( endpoint.empty() );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "tcp://localhost", 28960, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "localhost:1234", 28960, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "[::1", 28960, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "::1]", 28960, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "[localhost]", 28960, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "localhost/path", 28960, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "localhost", 0, endpoint ) );
	CHECK( !fnql::zmq::BuildTcpEndpoint( "localhost", 65536, endpoint ) );
	return true;
}

bool AcceptsOnlyAbsoluteExplicitLibraryPaths() {
	CHECK( fnql::zmq::IsAbsoluteLibraryPath( "C:\\Program Files\\libzmq.dll" ) );
	CHECK( fnql::zmq::IsAbsoluteLibraryPath( "C:/lib/libzmq.dll" ) );
	CHECK( fnql::zmq::IsAbsoluteLibraryPath( "\\\\server\\share\\libzmq.dll" ) );
	CHECK( fnql::zmq::IsAbsoluteLibraryPath( "/usr/local/lib/libzmq.so" ) );
	CHECK( !fnql::zmq::IsAbsoluteWindowsLibraryPath( "/current-drive/libzmq.dll" ) );
	CHECK( fnql::zmq::IsAbsoluteWindowsLibraryPath( "C:\\lib\\libzmq.dll" ) );
	CHECK( !fnql::zmq::IsAbsoluteLibraryPath( "libzmq.dll" ) );
	CHECK( !fnql::zmq::IsAbsoluteLibraryPath( "..\\libzmq.dll" ) );
	constexpr char embeddedNull[] = "C:\\libzmq.dll\0suffix";
	CHECK( !fnql::zmq::IsAbsoluteLibraryPath(
		std::string_view( embeddedNull, sizeof( embeddedNull ) - 1 ) ) );
	return true;
}

} // namespace

int main() {
	return BuildsIpv4HostnameAndWildcardEndpoints() &&
		BracketsRawIpv6AndPreservesBracketedIpv6() &&
		RejectsMalformedHostsSchemesPortsAndPorts() &&
		AcceptsOnlyAbsoluteExplicitLibraryPaths() ? 0 : 1;
}
